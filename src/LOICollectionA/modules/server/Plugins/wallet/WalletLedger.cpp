#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>

#include <fmt/core.h>

#include <ll/api/Expected.h>
#include <ll/api/event/EventBus.h>

#include <mc/world/actor/player/Player.h>

#include "LOICollectionA/include/server/Events/modules/WalletTransferEvent.h"

#include "LOICollectionA/coro/TimerManager.h"

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/data/SQLiteStorage.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/wallet/WalletLedger.h"
#include "LOICollectionA/include/server/Plugins/wallet/WalletPlugin.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    struct WalletLedger::Impl {
        std::shared_ptr<SQLiteStorage> db;
        const Config::C_Wallet& options;
        std::shared_ptr<ll::io::Logger> logger;
        TimerManager& timerManager;

        std::atomic<uint64_t> mLedgerSeq{ 0 };

        Impl(
            std::shared_ptr<SQLiteStorage> db_,
            const Config::C_Wallet& options_,
            std::shared_ptr<ll::io::Logger> logger_,
            TimerManager& timerManager_
        ) : db(std::move(db_)),
            options(options_),
            logger(std::move(logger_)),
            timerManager(timerManager_) {}
    };

    WalletLedger::WalletLedger(
        std::shared_ptr<SQLiteStorage> db,
        const Config::C_Wallet& options,
        std::shared_ptr<ll::io::Logger> logger,
        TimerManager& timerManager
    ) : mImpl(std::make_unique<Impl>(std::move(db), options, std::move(logger), timerManager)) {}

    WalletLedger::~WalletLedger() = default;

    bool WalletLedger::isValid() const {
        return this->mImpl->db != nullptr;
    }

    ll::Expected<void> WalletLedger::createTables() {
        return this->mImpl->db->create("WalletLedger", [](SQLiteStorage::ColumnCallback ctor) -> void {
            ctor("from_uuid");
            ctor("from_name");
            ctor("to_uuid");
            ctor("to_name");
            ctor("amount");
            ctor("fee");
            ctor("type");
            ctor("time_ns");
            ctor("time");
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->db->create("WalletFee", [](SQLiteStorage::ColumnCallback ctor) -> void {
                ctor("amount");
            });
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->db->exec("CREATE INDEX IF NOT EXISTS idx_WalletLedger_time_ns ON WalletLedger(time_ns);");
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->db->exec("CREATE INDEX IF NOT EXISTS idx_WalletLedger_from_uuid ON WalletLedger(from_uuid);");
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->db->exec("CREATE INDEX IF NOT EXISTS idx_WalletLedger_to_uuid ON WalletLedger(to_uuid);");
        });
    }

    void WalletLedger::record(const std::string& fromUuid, const std::string& fromName, const std::string& toUuid, const std::string& toName, long long amount, long long fee, const std::string& type) {
        ll::event::EventBus::getInstance().publish(LOICollection::server::Events::WalletTransferEvent(
            fromUuid,
            fromName,
            toUuid,
            toName,
            amount,
            fee,
            type,
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        ));

        if (!this->isValid() || !this->mImpl->options.WalletHistoryEnabled)
            return;

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

        this->mImpl->db->set("WalletLedger", id, row)
            .or_else([this](ll::Error e) -> ll::Expected<void> {
                e.log(*this->mImpl->logger);

                return {};
            });
    }

    ll::Expected<std::vector<std::string>> WalletLedger::getPlayerLedger(const std::string& uuid, int limit) {
        if (!this->isValid())
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->db->find("WalletLedger", {
            { "from_uuid", uuid },
            { "to_uuid", uuid }
        }, SQLiteStorage::FindCondition::OR)
            .and_then([this, uuid, limit](const std::vector<std::string>& ids) -> ll::Expected<std::vector<std::string>> {
                if (ids.empty())
                    return std::vector<std::string>{};

                return this->mImpl->db->get("WalletLedger", ids)
                    .transform([uuid, limit](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> rows) -> std::vector<std::string> {
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

    ll::Expected<void> WalletLedger::sendHistory(Player& receiver, const std::string& uuid, const std::string& name, int limit) {
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

    ll::Expected<long long> WalletLedger::getFeePool() {
        if (!this->isValid())
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->db->get("WalletFee", "total", "amount", "0")
            .transform([](const std::string& value) -> long long {
                return SystemUtils::toLongLong(value, 0);
            });
    }

    ll::Expected<void> WalletLedger::accumulateFee(long long amount) {
        if (amount <= 0)
            return {};

        return this->mImpl->db->get("WalletFee", "total", "amount", "0")
            .and_then([this, amount](const std::string& value) -> ll::Expected<void> {
                long long total = SystemUtils::toLongLong(value, 0);

                return this->mImpl->db->set("WalletFee", "total", "amount", std::to_string(total + amount));
            });
    }

    ll::Expected<long long> WalletLedger::getTodayOutgoing(const std::string& uuid) {
        if (!this->isValid())
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::Invalid));

        constexpr long long NS_PER_DAY = 86400LL * 1000000000LL;

        auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        long long todayStartNs = (nowNs / NS_PER_DAY) * NS_PER_DAY;

        auto ids = this->mImpl->db->find("WalletLedger", std::vector<std::pair<std::string, std::string>>{ { "from_uuid", uuid } }, SQLiteStorage::FindCondition::AND);
        if (!ids.has_value())
            return ll::Unexpected(ids.error());

        long long total = 0;
        for (const auto& id : ids.value()) {
            auto row = this->mImpl->db->get("WalletLedger", id);
            if (!row.has_value())
                continue;

            const auto& fields = row.value();
            if (!fields.contains("type") || fields.at("type") != "transfer")
                continue;
            if (!fields.contains("time_ns") || SystemUtils::toLongLong(fields.at("time_ns"), 0) < todayStartNs)
                continue;

            total += SystemUtils::toLongLong(fields.at("amount"), 0) + SystemUtils::toLongLong(fields.at("fee"), 0);
        }

        return total;
    }

    ll::Expected<std::vector<std::string>> WalletLedger::getRedEnvelopeDailyStats() {
        if (!this->isValid())
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::Invalid));

        constexpr long long NS_PER_DAY = 86400LL * 1000000000LL;

        long long nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        long long todayStartNs = (nowNs / NS_PER_DAY) * NS_PER_DAY;

        auto sendIds = this->mImpl->db->find("WalletLedger", {
            { "type", "redenvelope_send" }
        }, SQLiteStorage::FindCondition::AND);
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
        }, SQLiteStorage::FindCondition::AND);
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

    void WalletLedger::startCleanupSchedule() {
        if (this->mImpl->options.WalletHistoryRetentionDays <= 0)
            return;

        this->scheduleCleanup();
    }

    void WalletLedger::scheduleCleanup() {
        this->mImpl->timerManager.schedule("wallet_ledger_cleanup", std::chrono::hours(24), [this]() -> void {
            this->cleanup();
        });
    }

    void WalletLedger::cleanup() {
        long long cutoff = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()
            - static_cast<long long>(this->mImpl->options.WalletHistoryRetentionDays) * 86400LL * 1000000000LL;

        this->mImpl->db->exec(fmt::format("DELETE FROM {} WHERE time_ns < {}", "WalletLedger", cutoff))
            .or_else([this](ll::Error e) -> ll::Expected<void> {
                e.log(*this->mImpl->logger);

                return {};
            });

        this->scheduleCleanup();
    }
}
