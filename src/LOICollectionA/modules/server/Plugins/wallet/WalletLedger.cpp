#include "LOICollectionA/include/server/Plugins/wallet/WalletDetail.h"

namespace LOICollection::server::Plugins {
    ll::Expected<long long> WalletPlugin::getFeePool() {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->db->get("WalletFee", "total", "amount", "0")
            .transform([](const std::string& value) -> long long {
                return SystemUtils::toLongLong(value, 0);
            });
    }

    ll::Expected<void> WalletPlugin::accumulateFee(long long amount) {
        if (amount <= 0)
            return {};

        return this->mImpl->db->get("WalletFee", "total", "amount", "0")
            .and_then([this, amount](const std::string& value) -> ll::Expected<void> {
                long long total = SystemUtils::toLongLong(value, 0);

                return this->mImpl->db->set("WalletFee", "total", "amount", std::to_string(total + amount));
            });
    }

    ll::Expected<std::vector<std::string>> WalletPlugin::getRedEnvelopeDailyStats() {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        constexpr long long NS_PER_DAY = 86400LL * 1000000000LL;

        long long nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        long long todayStartNs = (nowNs / NS_PER_DAY) * NS_PER_DAY;

        auto sendIds = this->mImpl->db->find("WalletLedger", {
            { "type", "redenvelope_send" }
        });
        if (!sendIds.has_value())
            return ll::Unexpected(sendIds.error());

        long long sendCount = 0;
        long long sendTotal = 0;
        std::unordered_map<std::string, long long> senderTotal;

        for (const auto& id : sendIds.value()) {
            auto row = this->mImpl->db->get("WalletLedger", id);
            if (!row.has_value())
                continue;

            const auto& fields = row.value();
            if (!fields.contains("time_ns") || SystemUtils::toLongLong(fields.at("time_ns"), 0) < todayStartNs)
                continue;

            long long amount = SystemUtils::toLongLong(fields.contains("amount") ? fields.at("amount") : "0", 0);
            sendCount += 1;
            sendTotal += amount;

            std::string sender = fields.contains("from_name") ? fields.at("from_name") : "?";
            senderTotal[sender] += amount;
        }

        auto grabIds = this->mImpl->db->find("WalletLedger", {
            { "type", "redenvelope_grab" }
        });
        if (!grabIds.has_value())
            return ll::Unexpected(grabIds.error());

        std::unordered_map<std::string, long long> grabTotal;
        for (const auto& id : grabIds.value()) {
            auto row = this->mImpl->db->get("WalletLedger", id);
            if (!row.has_value())
                continue;

            const auto& fields = row.value();
            if (!fields.contains("time_ns") || SystemUtils::toLongLong(fields.at("time_ns"), 0) < todayStartNs)
                continue;

            std::string grabber = fields.contains("to_name") ? fields.at("to_name") : "?";
            grabTotal[grabber] += SystemUtils::toLongLong(fields.contains("amount") ? fields.at("amount") : "0", 0);
        }

        std::vector<std::pair<std::string, long long>> topGrabbers(grabTotal.begin(), grabTotal.end());
        std::sort(topGrabbers.begin(), topGrabbers.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });
        if (topGrabbers.size() > 5)
            topGrabbers.resize(5);

        std::string topSender;
        long long topSenderAmount = 0;
        for (const auto& [name, amount] : senderTotal)
            if (amount > topSenderAmount) {
                topSenderAmount = amount;
                topSender = name;
            }

        std::vector<std::string> result;
        result.emplace_back(tr({}, "wallet.rstat.header"));
        result.emplace_back(fmt::format(fmt::runtime(tr({}, "wallet.rstat.count")), sendCount));
        result.emplace_back(fmt::format(fmt::runtime(tr({}, "wallet.rstat.total")), sendTotal));
        result.emplace_back(fmt::format(fmt::runtime(tr({}, "wallet.rstat.generous")), topSender, topSenderAmount));

        for (size_t i = 0; i < topGrabbers.size(); ++i)
            result.emplace_back(fmt::format(fmt::runtime(tr({}, "wallet.rstat.grabber")),
                i + 1, topGrabbers.at(i).first, topGrabbers.at(i).second));

        return result;
    }

    ll::Expected<void> WalletPlugin::appendLedger(const std::string& fromUuid, const std::string& fromName, const std::string& toUuid, const std::string& toName, long long amount, long long fee, const std::string& type) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        if (!this->mImpl->options.WalletHistoryEnabled)
            return {};

        long long nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        std::string id = std::to_string(nowNs) + "_" + std::to_string(this->mImpl->mLedgerSeq.fetch_add(1, std::memory_order_relaxed));

        std::unordered_map<std::string, std::string> row = {
            { "from_uuid", fromUuid },
            { "from_name", fromName },
            { "to_uuid", toUuid },
            { "to_name", toName },
            { "amount", std::to_string(amount) },
            { "fee", std::to_string(fee) },
            { "type", type },
            { "time_ns", std::to_string(nowNs) },
            { "time", SystemUtils::getNowTime() }
        };

        return this->mImpl->db->set("WalletLedger", id, row);
    }

    ll::Expected<std::vector<std::string>> WalletPlugin::getPlayerLedger(const std::string& uuid, int limit) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->db->find("WalletLedger", {
            { "from_uuid", uuid },
            { "to_uuid", uuid }
        }, SQLiteStorage::FindCondition::OR)
            .and_then([this, uuid, limit](const std::vector<std::string>& ids) -> ll::Expected<std::vector<std::string>> {
                if (ids.empty())
                    return std::vector<std::string>{};

                return this->mImpl->db->get("WalletLedger", ids)
                    .transform([this, uuid, limit](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> rows) -> std::vector<std::string> {
                        std::vector<std::pair<std::string, std::string>> sorted;
                        sorted.reserve(rows.size());

                        for (const auto& [id, row] : rows) {
                            std::string type = row.contains("type") ? row.at("type") : "";
                            std::string fromName = row.contains("from_name") ? row.at("from_name") : "";
                            std::string toName = row.contains("to_name") ? row.at("to_name") : "";
                            std::string amount = row.contains("amount") ? row.at("amount") : "0";
                            std::string fee = row.contains("fee") ? row.at("fee") : "0";
                            std::string timeNs = row.contains("time_ns") ? row.at("time_ns") : "";
                            std::string timeStr = row.contains("time") ? row.at("time") : "";

                            bool isOut = row.contains("from_uuid") && row.at("from_uuid") == uuid;
                            std::string direction = tr({}, isOut ? "wallet.history.type.out" : "wallet.history.type.in");

                            std::string display = fmt::format(fmt::runtime(tr({}, "wallet.history.row")),
                                timeStr, direction, fromName, toName, amount, fee);

                            sorted.emplace_back(timeNs, display);
                        }

                        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
                            return a.first > b.first;
                        });

                        if (limit > 0 && sorted.size() > static_cast<size_t>(limit))
                            sorted.resize(static_cast<size_t>(limit));

                        std::vector<std::string> result;
                        result.reserve(sorted.size());
                        for (const auto& [t, d] : sorted)
                            result.emplace_back(d);

                        return result;
                    });
            });
    }

    void WalletPlugin::scheduleLedgerCleanup() {
        this->mImpl->mTimerManager->schedule("wallet_ledger_cleanup", std::chrono::hours(24), [this]() -> void {
            this->cleanupLedger();
        });
    }

    void WalletPlugin::cleanupLedger() {
        long long cutoff = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()
            - static_cast<long long>(this->mImpl->options.WalletHistoryRetentionDays) * 86400LL * 1000000000LL;

        this->mImpl->db->exec(fmt::format("DELETE FROM {} WHERE time_ns < {}", "WalletLedger", cutoff))
            .or_else(modules::defaultErrorHandler<WalletPlugin>);

        this->scheduleLedgerCleanup();
    }

    ll::Expected<void> WalletPlugin::sendHistory(Player& receiver, const std::string& uuid, const std::string& name, int limit) {
        return this->getPlayerLedger(uuid, limit)
            .and_then([&receiver, name](const std::vector<std::string>& lines) -> ll::Expected<void> {
                if (lines.empty()) {
                    receiver.sendMessage(tr({}, "wallet.history.empty"));

                    return {};
                }

                receiver.sendMessage(fmt::format(fmt::runtime(tr({}, "wallet.history.header")), name));

                for (const auto& line : lines)
                    receiver.sendMessage(line);

                return {};
            });
    }
}
