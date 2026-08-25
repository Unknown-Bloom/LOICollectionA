#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/include/server/Plugins/wallet/WalletType.h"

class Player;
class SQLiteStorage;
class TimerManager;

namespace ll {
    namespace io {
        class Logger;
    }
}

namespace Config {
    struct C_Wallet;
}

namespace LOICollection::server::Plugins {
    class WalletLedger;

    class WalletBank {
    public:
        WalletBank(
            std::shared_ptr<SQLiteStorage> db,
            const Config::C_Wallet& options,
            std::shared_ptr<ll::io::Logger> logger,
            TimerManager& timerManager,
            WalletLedger& ledger
        );

        ~WalletBank();

        WalletBank(WalletBank const&) = delete;
        WalletBank(WalletBank&&) = delete;
        WalletBank& operator=(WalletBank const&) = delete;
        WalletBank& operator=(WalletBank&&) = delete;

    public:
        LOICOLLECTION_A_NDAPI ll::Expected<void> createTables();

        LOICOLLECTION_A_NDAPI ll::Expected<void> deposit(Player& player, int amount);

        LOICOLLECTION_A_NDAPI ll::Expected<void> withdraw(Player& player);

        LOICOLLECTION_A_NDAPI ll::Expected<long long> getPrincipal(const std::string& uuid);

        LOICOLLECTION_A_NDAPI ll::Expected<long long> getInterest(const std::string& uuid);

        LOICOLLECTION_A_NDAPI ll::Expected<void> rebuildWealthRanking();

        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::pair<std::string, long long>>> getWealthRanking(int limit);

        LOICOLLECTION_A_NDAPI ll::Expected<std::pair<int, long long>> getWealthRank(const std::string& uuid);

        LOICOLLECTION_A_API   void startWealthRefresh();

    private:
        bool isValid() const;

        ll::Expected<long long> computeInterest(const std::string& uuid, long long principal, long long depositAt);
        ll::Expected<std::vector<WealthEntry>> computeWealthRanking();

        struct Impl;

        std::unique_ptr<Impl> mImpl;
    };
}
