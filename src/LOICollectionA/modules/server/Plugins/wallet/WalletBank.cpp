#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <optional>
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

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"
#include "LOICollectionA/include/server/Plugins/types/wallet/WalletSchema.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/wallet/WalletBank.h"
#include "LOICollectionA/include/server/Plugins/wallet/WalletLedger.h"
#include "LOICollectionA/include/server/Plugins/types/wallet/WalletCommonType.h"
#include "LOICollectionA/include/server/Plugins/wallet/WalletPlugin.h"

namespace LOICollection::server::Plugins {
    struct WalletBank::Impl {
        std::shared_ptr<BlockRepository> db;
        std::optional<WalletBankTable> bank;
        std::optional<WalletFeeTable> fee;
        std::optional<WalletTable> wallet;

        const Config::C_Wallet& options;
        std::shared_ptr<ll::io::Logger> logger;
        TimerManager& timerManager;

        WalletLedger& ledger;

        mutable std::mutex mRankMutex;
        std::vector<WealthEntry> mWealthRank;
        std::unordered_map<std::string, size_t> mRankOf;

        Impl(
            std::shared_ptr<BlockRepository> db_,
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
        std::shared_ptr<BlockRepository> db,
        const Config::C_Wallet& options,
        std::shared_ptr<ll::io::Logger> logger,
        TimerManager& timerManager,
        WalletLedger& ledger
    ) : mImpl(std::make_unique<Impl>(std::move(db), options, std::move(logger), timerManager, ledger)) {}

    WalletBank::~WalletBank() = default;

    bool WalletBank::isValid() const {
        return this->mImpl->db != nullptr && this->mImpl->bank.has_value()
            && this->mImpl->fee.has_value() && this->mImpl->wallet.has_value();
    }

    ll::Expected<void> WalletBank::createTables() {
        return WalletBankTable::open(*this->mImpl->db, "WalletBank")
            .and_then([this](WalletBankTable table) -> ll::Expected<WalletFeeTable> {
                this->mImpl->bank.emplace(std::move(table));

                return WalletFeeTable::open(*this->mImpl->db, "WalletFee");
            })
            .and_then([this](WalletFeeTable table) -> ll::Expected<WalletTable> {
                this->mImpl->fee.emplace(std::move(table));

                return WalletTable::open(*this->mImpl->db, "Wallet");
            })
            .transform([this](WalletTable table) -> void {
                this->mImpl->wallet.emplace(std::move(table));
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

        auto& bank = *this->mImpl->bank;

        auto principal = bank.get<long long>(uuid, "principal", 0);
        if (!principal.has_value()) {
            ScoreboardUtils::addScore(player, mScoreboard, amount);

            return ll::Unexpected(principal.error());
        }

        long long nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

        auto setBank = bank.set(uuid, "principal", principal.value() + amount)
            .and_then([&bank, &uuid, nowNs]() -> ll::Expected<void> {
                return bank.set(uuid, "deposit_at", nowNs);
            })
            .and_then([&bank, &uuid, &player]() -> ll::Expected<void> {
                return bank.set(uuid, "name", player.getRealName());
            });
        if (!setBank.has_value()) {
            ScoreboardUtils::addScore(player, mScoreboard, amount);

            return ll::Unexpected(setBank.error());
        }

        this->mImpl->ledger.record(uuid, player.getRealName(), "", "", amount, 0, "bank_deposit");

        return this->mImpl->wallet->set(uuid, "balance", static_cast<long long>(ScoreboardUtils::getScore(player, mScoreboard)))
            .or_else([this](ll::Error e) -> ll::Expected<void> {
                e.log(*this->mImpl->logger);

                return {};
            });
    }

    ll::Expected<long long> WalletBank::computeInterest(long long principal, long long depositAt) {
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

        auto& bank = *this->mImpl->bank;
        auto& fee = *this->mImpl->fee;

        auto principal = bank.get<long long>(uuid, "principal", 0);
        if (!principal.has_value())
            return ll::Unexpected(principal.error());
        if (principal.value() <= 0)
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::BankEmpty));

        auto depositAt = bank.get<long long>(uuid, "deposit_at", 0);
        if (!depositAt.has_value())
            return ll::Unexpected(depositAt.error());

        auto interest = this->computeInterest(principal.value(), depositAt.value());
        if (!interest.has_value())
            return ll::Unexpected(interest.error());

        long long paidInterest = interest.value();
        long long interestTax = 0;

        if (this->mImpl->options.WalletInterestFromPool) {
            auto pool = fee.get<long long>("total", "amount", 0);
            if (!pool.has_value())
                return ll::Unexpected(pool.error());

            paidInterest = std::min(paidInterest, pool.value());

            if (paidInterest > 0) {
                auto setPool = fee.set("total", "amount", pool.value() - paidInterest);
                if (!setPool.has_value())
                    return ll::Unexpected(setPool.error());
            }
        } else {
            interestTax = static_cast<long long>(std::floor(
                static_cast<double>(paidInterest) * this->mImpl->options.WalletInterestTaxRate
            ));

            if (interestTax > 0) {
                auto pool = fee.get<long long>("total", "amount", 0);
                if (!pool.has_value())
                    return ll::Unexpected(pool.error());

                auto setPool = fee.set("total", "amount", pool.value() + interestTax);
                if (!setPool.has_value())
                    return ll::Unexpected(setPool.error());
            }
        }

        auto delBank = bank.del(uuid);
        if (!delBank.has_value())
            return ll::Unexpected(delBank.error());

        long long credit = principal.value() + paidInterest;
        ScoreboardUtils::addScore(player, this->mImpl->options.TargetScoreboard, static_cast<int>(credit));

        std::string playerName = player.getRealName();

        this->mImpl->ledger.record(uuid, playerName, "", "", principal.value(), 0, "bank_withdraw");

        if (paidInterest > 0)
            this->mImpl->ledger.record("", "", uuid, playerName, paidInterest, interestTax, "bank_interest");

        return this->mImpl->wallet->set(uuid, "balance", static_cast<long long>(ScoreboardUtils::getScore(player, this->mImpl->options.TargetScoreboard)))
            .or_else([this](ll::Error e) -> ll::Expected<void> {
                e.log(*this->mImpl->logger);

                return {};
            });
    }

    ll::Expected<long long> WalletBank::getPrincipal(const std::string& uuid) {
        if (!this->isValid())
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->bank->get<long long>(uuid, "principal", 0);
    }

    ll::Expected<long long> WalletBank::getInterest(const std::string& uuid) {
        if (!this->isValid())
            return ll::makeErrorCodeError(WalletPlugin::makeErrorCode(WalletPluginErrorCode::Invalid));

        auto principal = this->mImpl->bank->get<long long>(uuid, "principal", 0);
        if (!principal.has_value())
            return ll::Unexpected(principal.error());

        auto depositAt = this->mImpl->bank->get<long long>(uuid, "deposit_at", 0);
        if (!depositAt.has_value())
            return ll::Unexpected(depositAt.error());

        return this->computeInterest(principal.value(), depositAt.value());
    }

    ll::Expected<std::vector<WealthEntry>> WalletBank::computeWealthRanking() {
        auto& wallet = *this->mImpl->wallet;

        auto ids = wallet.list();
        if (!ids.has_value())
            return ll::Unexpected(ids.error());

        std::vector<WealthEntry> entries;
        entries.reserve(ids.value().size());

        std::string mScoreboard = this->mImpl->options.TargetScoreboard;

        for (const auto& uuid : ids.value()) {
            auto name = wallet.get<std::string>(uuid, "name", "Unknown");
            if (!name.has_value())
                return ll::Unexpected(name.error());

            long long balance = 0;
            if (Player* player = ll::service::getLevel()->getPlayer(mce::UUID::fromString(uuid)); player)
                balance = ScoreboardUtils::getScore(*player, mScoreboard);
            else {
                auto snapshot = wallet.get<long long>(uuid, "balance", 0);
                if (!snapshot.has_value())
                    return ll::Unexpected(snapshot.error());

                balance = snapshot.value();
            }

            entries.push_back({ uuid, name.value(), balance });
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
