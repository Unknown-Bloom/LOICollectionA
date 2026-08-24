#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

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
    struct C_Market;
}

namespace LOICollection::server::Plugins {
    class MarketAuction {
    public:
        using BlacklistProvider = std::function<ll::Expected<std::vector<std::string>>(const std::string&)>;
        using TaxRateProvider = std::function<double()>;

        MarketAuction(
            std::shared_ptr<SQLiteStorage> db,
            std::shared_ptr<SQLiteStorage> settingsDb,
            const Config::C_Market& options,
            std::shared_ptr<ll::io::Logger> logger,
            TimerManager& timerManager,
            BlacklistProvider blacklistProvider,
            TaxRateProvider taxRateProvider = nullptr
        );

        ~MarketAuction();

        MarketAuction(MarketAuction const&) = delete;
        MarketAuction(MarketAuction&&) = delete;
        MarketAuction& operator=(MarketAuction const&) = delete;
        MarketAuction& operator=(MarketAuction&&) = delete;

    public:
        LOICOLLECTION_A_NDAPI ll::Expected<void> createTables();
        LOICOLLECTION_A_API   void startSweep();

        LOICOLLECTION_A_NDAPI ll::Expected<bool> createAuction(Player& player, int slot, const std::string& name, int startPrice, int durationSeconds);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> bidAuction(Player& player, const std::string& id, int price);

        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getAuctionList();
        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getAuctionItems(Player& player);
        LOICOLLECTION_A_NDAPI ll::Expected<std::unordered_map<std::string, std::string>> getAuctionData(const std::string& id);

    private:
        bool isValid() const;

        ll::Expected<void> sweepExpired();

        ll::Expected<void> finalizeWin(const std::string& id, const std::unordered_map<std::string, std::string>& data);
        ll::Expected<void> finalizeLose(const std::string& id, const std::unordered_map<std::string, std::string>& data);

        ll::Expected<void> restoreAuction(const std::string& id, const std::unordered_map<std::string, std::string>& data, const std::string& saleKey);

        ll::Expected<void> refundScore(const std::string& uuid, int score, const std::string& scoreboard);
        ll::Expected<void> collectTax(int tax);

        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
