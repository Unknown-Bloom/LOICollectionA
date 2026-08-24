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
    class MarketWanted {
    public:
        using BlacklistProvider = std::function<ll::Expected<std::vector<std::string>>(const std::string&)>;

        MarketWanted(
            std::shared_ptr<SQLiteStorage> db,
            std::shared_ptr<SQLiteStorage> settingsDb,
            const Config::C_Market& options,
            std::shared_ptr<ll::io::Logger> logger,
            TimerManager& timerManager,
            BlacklistProvider blacklistProvider
        );

        ~MarketWanted();

        MarketWanted(MarketWanted const&) = delete;
        MarketWanted(MarketWanted&&) = delete;
        MarketWanted& operator=(MarketWanted const&) = delete;
        MarketWanted& operator=(MarketWanted&&) = delete;

    public:
        LOICOLLECTION_A_NDAPI ll::Expected<void> createTables();
        LOICOLLECTION_A_API   void startSweep();

        LOICOLLECTION_A_NDAPI ll::Expected<bool> createWanted(Player& player, int slot, const std::string& name, int unitPrice, int amount);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> cancelWanted(Player& player, const std::string& id);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> fillWanted(Player& player, const std::string& id, int amount);

        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getWantedList();
        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getWantedItems(Player& player);
        LOICOLLECTION_A_NDAPI ll::Expected<std::unordered_map<std::string, std::string>> getWantedData(const std::string& id);

    private:
        bool isValid() const;

        ll::Expected<std::string> commitWantedFill(const std::string& id, const std::unordered_map<std::string, std::string>& data, int amount, int pay, int tax, Player& seller);
        ll::Expected<void> restoreWantedFill(const std::string& id, const std::unordered_map<std::string, std::string>& data, int amount, const std::string& saleKey);

        ll::Expected<void> deleteWanted(const std::string& id);
        ll::Expected<void> restoreWanted(const std::string& id, const std::unordered_map<std::string, std::string>& data);

        ll::Expected<void> sweepExpired();

        ll::Expected<void> refundBuyer(const std::string& buyerUuid, int score, const std::string& scoreboard);
        ll::Expected<void> collectTax(int tax);

        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
