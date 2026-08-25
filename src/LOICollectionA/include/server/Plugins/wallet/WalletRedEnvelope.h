#pragma once

#include <functional>
#include <memory>
#include <string>
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

    class WalletRedEnvelope {
    public:
        using TransferProvider = std::function<ll::Expected<void>(const std::string& target, int score)>;

        WalletRedEnvelope(
            std::shared_ptr<SQLiteStorage> db,
            const Config::C_Wallet& options,
            std::shared_ptr<ll::io::Logger> logger,
            TimerManager& timerManager,
            WalletLedger& ledger,
            TransferProvider transferProvider
        );

        ~WalletRedEnvelope();

        WalletRedEnvelope(WalletRedEnvelope const&) = delete;
        WalletRedEnvelope(WalletRedEnvelope&&) = delete;
        WalletRedEnvelope& operator=(WalletRedEnvelope const&) = delete;
        WalletRedEnvelope& operator=(WalletRedEnvelope&&) = delete;

    public:
        LOICOLLECTION_A_NDAPI ll::Expected<void> createTables();

        LOICOLLECTION_A_NDAPI ll::Expected<void> tryGrab(Player& player, const std::string& message);

        LOICOLLECTION_A_NDAPI ll::Expected<void> send(Player& player, const std::string& key, int score, int count, const std::vector<std::string>& targets = {});

        LOICOLLECTION_A_NDAPI ll::Expected<void> sweepExpired();

        LOICOLLECTION_A_NDAPI ll::Expected<void> refundAll();

        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getEnvelopeStats(const std::string& id);

        LOICOLLECTION_A_NDAPI static int computeGiftAmount(int remainingCapacity, int remainingPeople);

    private:
        bool isValid() const;

        ll::Expected<bool> grabEnvelope(Player& player, const std::string& uuid, RedEnvelopeEntry& entry);
        void broadcastContent(Player& sender, const std::string& key, const std::string& id, int score, int count);
        void broadcastReceive(const RedEnvelopeEntry& entry, Player& player, int amount, int people);
        void announceKing(RedEnvelopeEntry& entry);
        void announceTimeout(const std::string& id);
        ll::Expected<void> deleteEnvelope(const std::string& id);
        ll::Expected<bool> refundEnvelope(const std::string& id);
        ll::Expected<void> scheduleRefund(const std::string& id);
        ll::Expected<std::vector<std::string>> resolveTargetUuids(const std::vector<std::string>& names);

        struct Impl;

        std::unique_ptr<Impl> mImpl;
    };
}
