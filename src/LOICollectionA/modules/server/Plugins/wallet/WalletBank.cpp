#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>

#include <ll/api/Expected.h>
#include <ll/api/service/Bedrock.h>

#include <mc/deps/core/string/HashedString.h>

#include <mc/world/level/Level.h>
#include <mc/world/actor/player/Player.h>

#include "LOICollectionA/coro/TimerManager.h"

#include "LOICollectionA/utils/mc-server/ScoreboardUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/data/SQLiteStorage.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/wallet/WalletBank.h"
#include "LOICollectionA/include/server/Plugins/wallet/WalletLedger.h"
#include "LOICollectionA/include/server/Plugins/wallet/WalletType.h"
#include "LOICollectionA/include/server/Plugins/wallet/WalletPlugin.h"

namespace LOICollection::server::Plugins {
    struct WalletBank::Impl {
        std::shared_ptr<SQLiteStorage> db;
        const Config::C_Wallet& options;
        std::shared_ptr<ll::io::Logger> logger;
        TimerManager& timerManager;

        WalletLedger& ledger;

        mutable std::mutex mRankMutex;
        std::vector<WealthEntry> mWealthRank;
        std::unordered_map<std::string, size_t> mRankOf;

        Impl(
            std::shared_ptr<SQLiteStorage> db_,
            const Config::C_Wallet& options_,
            std::shared_ptr<ll::io::Logger> logger_,
            TimerManager& timerManager_,
            WalletLedger& ledger_
        ) : db(std::move(db_)),
            options(options_),
            logger(std::move(logger_)),
            timerManager(timerManager_),
            ledger(ledger_) {}
    };

    WalletBank::WalletBank(
        std::shared_ptr<SQLiteStorage> db,
        const Config::C_Wallet& options,
        std::shared_ptr<ll::io::Logger> logger,
        TimerManager& timerManager,
        WalletLedger& ledger
    ) : mImpl(std::make_unique<Impl>(std::move(db), options, std::move(logger), timerManager, ledger)) {}

    WalletBank::~WalletBank() = default;

    bool WalletBank::isValid() const {
        return this->mImpl->db != nullptr;
    }

    ll::Expected<void> WalletBank::createTables() {
        return this->mImpl->db->create("WalletBank", [](SQLiteStorage::ColumnCallback ctor) -> void {
            ctor("principal");
            ctor("deposit_at");
            ctor("name");
        });
    }

    ll::Expected<void> WalletBank::deposit(Player& player, int amount) {
        if (!this->isValid())
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::Invalid));

        if (amount <= 0 || (this->mImpl->options.WalletBankMinDeposit > 0 && amount < this->mImpl->options.WalletBankMinDeposit))
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::BelowMinDeposit));

        std::string uuid = player.getUuid().asString();
        std::string mScoreboard = this->mImpl->options.TargetScoreboard;

        if (ScoreboardUtils::getScore(player, mScoreboard) < amount)
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::Invalid));

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

        this->mImpl->ledger.record(uuid, player.getRealName(), "", "", amount, 0, "bank_deposit");

        this->mImpl->db->set("Wallet", uuid, "balance", std::to_string(static_cast<long long>(ScoreboardUtils::getScore(player, mScoreboard))))
            .or_else([this](ll::Error e) -> ll::Expected<void> {
                e.log(*this->mImpl->logger);

                return {};
            });

        return {};
    }

    ll::Expected<long long> WalletBank::computeInterest(const std::string& uuid, long long principal, long long depositAt) {
        if (principal <= 0)
            return 0;

        long long nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        long long elapsedSeconds = std::max<long long>(0, (nowNs - depositAt) / 1000000000LL);
        long long days = elapsedSeconds / 86400LL;

        return static_cast<long long>(std::floor(
            static_cast<double>(principal) * this->mImpl->options.WalletBankDailyRate * static_cast<double>(days)
        ));
    }

    ll::Expected<void> WalletBank::withdraw(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::Invalid));

        std::string uuid = player.getUuid().asString();

        auto data = this->mImpl->db->get("WalletBank", uuid);
        if (!data.has_value())
            return ll::Unexpected(data.error());

        auto row = data.value();
        if (row.empty() || !row.contains("principal") || SystemUtils::toLongLong(row.at("principal"), 0) <= 0)
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::BankEmpty));

        long long principal = SystemUtils::toLongLong(row.at("principal"), 0);
        long long depositAt = row.contains("deposit_at") ? SystemUtils::toLongLong(row.at("deposit_at"), 0) : 0;

        auto interest = this->computeInterest(uuid, principal, depositAt);
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

        this->mImpl->ledger.record(uuid, playerName, "", "", principal, 0, "bank_withdraw");

        if (paidInterest > 0)
            this->mImpl->ledger.record("", "", uuid, playerName, paidInterest, interestTax, "bank_interest");

        this->mImpl->db->set("Wallet", uuid, "balance", std::to_string(static_cast<long long>(ScoreboardUtils::getScore(player, this->mImpl->options.TargetScoreboard))))
            .or_else([this](ll::Error e) -> ll::Expected<void> {
                e.log(*this->mImpl->logger);

                return {};
            });

        return {};
    }

    ll::Expected<long long> WalletBank::getPrincipal(const std::string& uuid) {
        if (!this->isValid())
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->db->get("WalletBank", uuid, "principal", "0")
            .transform([](const std::string& value) -> long long {
                return SystemUtils::toLongLong(value, 0);
            });
    }

    ll::Expected<long long> WalletBank::getInterest(const std::string& uuid) {
        if (!this->isValid())
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->db->get("WalletBank", uuid)
            .and_then([this, uuid](std::unordered_map<std::string, std::string> row) -> ll::Expected<long long> {
                if (row.empty() || !row.contains("principal"))
                    return 0;

                long long principal = SystemUtils::toLongLong(row.at("principal"), 0);
                long long depositAt = row.contains("deposit_at") ? SystemUtils::toLongLong(row.at("deposit_at"), 0) : 0;

                return this->computeInterest(uuid, principal, depositAt);
            });
    }

    ll::Expected<std::vector<WealthEntry>> WalletBank::computeWealthRanking() {
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

    ll::Expected<void> WalletBank::rebuildWealthRanking() {
        if (!this->isValid())
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::Invalid));

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

    ll::Expected<std::vector<std::pair<std::string, long long>>> WalletBank::getWealthRanking(int limit) {
        if (!this->isValid())
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::Invalid));

        std::lock_guard<std::mutex> lock(this->mImpl->mRankMutex);

        std::vector<std::pair<std::string, long long>> result;
        result.reserve(this->mImpl->mWealthRank.size());

        for (const auto& entry : this->mImpl->mWealthRank)
            result.emplace_back(entry.name, entry.balance);

        if (limit > 0 && result.size() > static_cast<size_t>(limit))
            result.resize(static_cast<size_t>(limit));

        return result;
    }

    ll::Expected<std::pair<int, long long>> WalletBank::getWealthRank(const std::string& uuid) {
        if (!this->isValid())
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::Invalid));

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

    void WalletBank::startWealthRefresh() {
        if (this->mImpl->options.WealthRefreshMinutes <= 0)
            return;

        this->mImpl->timerManager.loopSchedule("wallet_wealth_refresh", std::chrono::minutes(this->mImpl->options.WealthRefreshMinutes), [this]() -> void {
            this->rebuildWealthRanking()
                .or_else([this](ll::Error e) -> ll::Expected<void> {
                    e.log(*this->mImpl->logger);

                    return {};
                });
        });
    }
}
