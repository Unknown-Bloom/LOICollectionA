#include "LOICollectionA/include/server/Plugins/wallet/WalletDetail.h"

namespace LOICollection::server::Plugins {
    ll::Expected<void> WalletPlugin::bankDeposit(Player& player, int amount) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        if (amount <= 0 || (this->mImpl->options.WalletBankMinDeposit > 0 && amount < this->mImpl->options.WalletBankMinDeposit))
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::BelowMinDeposit));

        std::string uuid = player.getUuid().asString();
        std::string mScoreboard = this->mImpl->options.TargetScoreboard;

        if (ScoreboardUtils::getScore(player, mScoreboard) < amount)
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        ScoreboardUtils::reduceScore(player, mScoreboard, amount);

        long long nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

        auto transaction = SQLiteStorageTransaction::create(*this->mImpl->db);
        if (!transaction.has_value()) {
            ScoreboardUtils::addScore(player, mScoreboard, amount);

            return ll::Unexpected(transaction.error());
        }

        auto conn = transaction.value().connection();

        auto current = this->mImpl->db->get(conn, "WalletBank", uuid);
        if (!current.has_value()) {
            ScoreboardUtils::addScore(player, mScoreboard, amount);

            return ll::Unexpected(current.error());
        }

        long long principal = current.value().contains("principal")
            ? SystemUtils::toLongLong(current.value().at("principal"), 0)
            : 0;

        auto setBank = this->mImpl->db->set(conn, "WalletBank", uuid, {
            { "principal", std::to_string(principal + amount) },
            { "deposit_at", std::to_string(nowNs) },
            { "name", player.getRealName() }
        });
        if (!setBank.has_value()) {
            ScoreboardUtils::addScore(player, mScoreboard, amount);

            return ll::Unexpected(setBank.error());
        }

        auto commit = transaction.value().commit();
        if (!commit.has_value()) {
            ScoreboardUtils::addScore(player, mScoreboard, amount);

            return ll::Unexpected(commit.error());
        }

        this->appendLedger(uuid, player.getRealName(), "", "", amount, 0, "bank_deposit")
            .or_else(modules::defaultErrorHandler<WalletPlugin>);

        this->emitWalletTransfer(uuid, player.getRealName(), "", "", amount, 0, "bank_deposit");

        this->updateBalanceSnapshot(uuid, static_cast<long long>(ScoreboardUtils::getScore(player, mScoreboard)))
            .or_else(modules::defaultErrorHandler<WalletPlugin>);

        return {};
    }

    ll::Expected<long long> WalletPlugin::computeBankInterest(const std::string& uuid, long long principal, long long depositAt) {
        if (principal <= 0)
            return 0;

        long long nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        long long elapsedSeconds = std::max<long long>(0, (nowNs - depositAt) / 1000000000LL);
        long long days = elapsedSeconds / 86400LL;

        return static_cast<long long>(std::floor(
            static_cast<double>(principal) * this->mImpl->options.WalletBankDailyRate * static_cast<double>(days)
        ));
    }

    ll::Expected<void> WalletPlugin::bankWithdraw(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        std::string uuid = player.getUuid().asString();

        auto data = this->mImpl->db->get("WalletBank", uuid);
        if (!data.has_value())
            return ll::Unexpected(data.error());

        auto row = data.value();
        if (row.empty() || !row.contains("principal") || SystemUtils::toLongLong(row.at("principal"), 0) <= 0)
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::BankEmpty));

        long long principal = SystemUtils::toLongLong(row.at("principal"), 0);
        long long depositAt = row.contains("deposit_at") ? SystemUtils::toLongLong(row.at("deposit_at"), 0) : 0;

        auto interest = this->computeBankInterest(uuid, principal, depositAt);
        if (!interest.has_value())
            return ll::Unexpected(interest.error());

        long long paidInterest = interest.value();
        long long interestTax = 0;

        auto transaction = SQLiteStorageTransaction::create(*this->mImpl->db);
        if (!transaction.has_value())
            return ll::Unexpected(transaction.error());

        auto conn = transaction.value().connection();

        if (this->mImpl->options.WalletInterestFromPool) {
            auto pool = this->mImpl->db->get(conn, "WalletFee", "total", "amount", "0");
            if (!pool.has_value())
                return ll::Unexpected(pool.error());

            long long available = SystemUtils::toLongLong(pool.value(), 0);
            paidInterest = std::min(paidInterest, available);

            if (paidInterest > 0) {
                auto setPool = this->mImpl->db->set(conn, "WalletFee", "total", "amount", std::to_string(available - paidInterest));
                if (!setPool.has_value())
                    return ll::Unexpected(setPool.error());
            }
        } else {
            interestTax = static_cast<long long>(std::floor(
                static_cast<double>(paidInterest) * this->mImpl->options.WalletInterestTaxRate
            ));

            if (interestTax > 0) {
                auto pool = this->mImpl->db->get(conn, "WalletFee", "total", "amount", "0");
                if (!pool.has_value())
                    return ll::Unexpected(pool.error());

                long long available = SystemUtils::toLongLong(pool.value(), 0);

                auto setPool = this->mImpl->db->set(conn, "WalletFee", "total", "amount", std::to_string(available + interestTax));
                if (!setPool.has_value())
                    return ll::Unexpected(setPool.error());
            }
        }

        auto delBank = this->mImpl->db->del(conn, "WalletBank", uuid);
        if (!delBank.has_value())
            return ll::Unexpected(delBank.error());

        auto commit = transaction.value().commit();
        if (!commit.has_value())
            return ll::Unexpected(commit.error());

        long long credit = principal + paidInterest;
        ScoreboardUtils::addScore(player, this->mImpl->options.TargetScoreboard, static_cast<int>(credit));

        std::string playerName = player.getRealName();

        this->appendLedger(uuid, playerName, "", "", principal, 0, "bank_withdraw")
            .or_else(modules::defaultErrorHandler<WalletPlugin>);

        if (paidInterest > 0)
            this->appendLedger("", "", uuid, playerName, paidInterest, interestTax, "bank_interest")
                .or_else(modules::defaultErrorHandler<WalletPlugin>);

        this->emitWalletTransfer(uuid, playerName, "", "", principal, 0, "bank_withdraw");

        if (paidInterest > 0)
            this->emitWalletTransfer("", "", uuid, playerName, paidInterest, interestTax, "bank_interest");

        this->updateBalanceSnapshot(uuid, static_cast<long long>(ScoreboardUtils::getScore(player, this->mImpl->options.TargetScoreboard)))
            .or_else(modules::defaultErrorHandler<WalletPlugin>);

        return {};
    }

    ll::Expected<long long> WalletPlugin::getBankPrincipal(const std::string& uuid) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->db->get("WalletBank", uuid, "principal", "0")
            .transform([](const std::string& value) -> long long {
                return SystemUtils::toLongLong(value, 0);
            });
    }

    ll::Expected<long long> WalletPlugin::getBankInterest(const std::string& uuid) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->db->get("WalletBank", uuid)
            .and_then([this, uuid](std::unordered_map<std::string, std::string> row) -> ll::Expected<long long> {
                if (row.empty() || !row.contains("principal"))
                    return 0;

                long long principal = SystemUtils::toLongLong(row.at("principal"), 0);
                long long depositAt = row.contains("deposit_at") ? SystemUtils::toLongLong(row.at("deposit_at"), 0) : 0;

                return this->computeBankInterest(uuid, principal, depositAt);
            });
    }

    ll::Expected<std::vector<WalletPlugin::WealthEntry>> WalletPlugin::computeWealthRanking() {
        auto ids = this->mImpl->db->list("Wallet");
        if (!ids.has_value())
            return ll::Unexpected(ids.error());

        if (ids.value().empty())
            return std::vector<WealthEntry>{};

        auto rows = this->mImpl->db->get("Wallet", ids.value());
        if (!rows.has_value())
            return ll::Unexpected(rows.error());

        std::vector<WealthEntry> entries;
        entries.reserve(rows.value().size());

        std::string mScoreboard = this->mImpl->options.TargetScoreboard;

        for (const auto& [uuid, row] : rows.value()) {
            std::string name = row.contains("name") ? row.at("name") : "Unknown";

            long long balance = 0;
            if (Player* player = ll::service::getLevel()->getPlayer(mce::UUID::fromString(uuid)); player)
                balance = ScoreboardUtils::getScore(*player, mScoreboard);
            else
                balance = SystemUtils::toLongLong(row.contains("balance") ? row.at("balance") : "0", 0);

            entries.push_back({ uuid, name, balance });
        }

        std::sort(entries.begin(), entries.end(), [](const WealthEntry& left, const WealthEntry& right) -> bool {
            if (left.balance != right.balance)
                return left.balance > right.balance;

            return left.name < right.name;
        });

        return entries;
    }

    ll::Expected<void> WalletPlugin::rebuildWealthRanking() {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->computeWealthRanking()
            .transform([this](std::vector<WealthEntry> entries) -> void {
                std::lock_guard<std::mutex> lock(this->mImpl->mRankMutex);

                this->mImpl->mWealthRank = std::move(entries);

                this->mImpl->mRankOf.clear();
                this->mImpl->mRankOf.reserve(this->mImpl->mWealthRank.size());
                for (size_t i = 0; i < this->mImpl->mWealthRank.size(); ++i)
                    this->mImpl->mRankOf[this->mImpl->mWealthRank[i].uuid] = i;
            });
    }

    ll::Expected<std::vector<std::pair<std::string, long long>>> WalletPlugin::getWealthRanking(int limit) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        std::lock_guard<std::mutex> lock(this->mImpl->mRankMutex);

        std::vector<std::pair<std::string, long long>> result;
        result.reserve(this->mImpl->mWealthRank.size());

        for (const auto& entry : this->mImpl->mWealthRank)
            result.emplace_back(entry.name, entry.balance);

        if (limit > 0 && result.size() > static_cast<size_t>(limit))
            result.resize(static_cast<size_t>(limit));

        return result;
    }

    ll::Expected<std::pair<int, long long>> WalletPlugin::getWealthRank(const std::string& uuid) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        std::lock_guard<std::mutex> lock(this->mImpl->mRankMutex);

        auto it = this->mImpl->mRankOf.find(uuid);
        if (it == this->mImpl->mRankOf.end())
            return std::make_pair(-1, 0);

        const WealthEntry& entry = this->mImpl->mWealthRank[it->second];

        long long balance = entry.balance;
        if (Player* player = ll::service::getLevel()->getPlayer(mce::UUID::fromString(uuid)); player)
            balance = ScoreboardUtils::getScore(*player, this->mImpl->options.TargetScoreboard);

        return std::make_pair(static_cast<int>(it->second) + 1, balance);
    }

    void WalletPlugin::scheduleWealthRefresh() {
        if (this->mImpl->options.WealthRefreshMinutes <= 0)
            return;

        this->mImpl->mTimerManager->loopSchedule("wallet_wealth_refresh", std::chrono::minutes(this->mImpl->options.WealthRefreshMinutes), [this]() -> void {
            this->rebuildWealthRanking().or_else(modules::defaultErrorHandler<WalletPlugin>);
        });
    }

}
