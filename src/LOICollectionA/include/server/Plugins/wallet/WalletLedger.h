#pragma once

#include <memory>
#include <string>
#include <vector>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"

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
    class WalletLedger {
    public:
        WalletLedger(
            std::shared_ptr<SQLiteStorage> db,
            const Config::C_Wallet& options,
            std::shared_ptr<ll::io::Logger> logger,
            TimerManager& timerManager
        );

        ~WalletLedger();

        WalletLedger(WalletLedger const&) = delete;
        WalletLedger(WalletLedger&&) = delete;
        WalletLedger& operator=(WalletLedger const&) = delete;
        WalletLedger& operator=(WalletLedger&&) = delete;

    public:
        LOICOLLECTION_A_NDAPI ll::Expected<void> createTables();

        LOICOLLECTION_A_NDAPI void record(const std::string& fromUuid, const std::string& fromName, const std::string& toUuid, const std::string& toName, long long amount, long long fee, const std::string& type);

        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getPlayerLedger(const std::string& uuid, int limit);

        LOICOLLECTION_A_NDAPI ll::Expected<void> sendHistory(Player& receiver, const std::string& uuid, const std::string& name, int limit);

        LOICOLLECTION_A_NDAPI ll::Expected<long long> getFeePool();

        LOICOLLECTION_A_NDAPI ll::Expected<void> accumulateFee(long long amount);

        LOICOLLECTION_A_NDAPI ll::Expected<long long> getTodayOutgoing(const std::string& uuid);

        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getRedEnvelopeDailyStats();

        LOICOLLECTION_A_API   void startCleanupSchedule();

    private:
        bool isValid() const;

        void scheduleCleanup();
        void cleanup();

        struct Impl;

        std::unique_ptr<Impl> mImpl;
    };
}
