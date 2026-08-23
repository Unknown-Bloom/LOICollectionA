#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/include/server/Plugins/market/MarketType.h"

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
    class MarketStore {
    public:
        using BlacklistProvider = std::function<ll::Expected<std::vector<std::string>>(const std::string&)>;

        MarketStore(
            std::shared_ptr<SQLiteStorage> db,
            std::shared_ptr<SQLiteStorage> settingsDb,
            const Config::C_Market& options,
            std::shared_ptr<ll::io::Logger> logger,
            TimerManager& timerManager,
            BlacklistProvider blacklistProvider
        );

        ~MarketStore();

        MarketStore(MarketStore const&) = delete;
        MarketStore(MarketStore&&) = delete;
        MarketStore& operator=(MarketStore const&) = delete;
        MarketStore& operator=(MarketStore&&) = delete;

    public:
        LOICOLLECTION_A_NDAPI ll::Expected<void> createTables();

        LOICOLLECTION_A_NDAPI ll::Expected<bool> createStore(Player& player, const std::string& name, const std::string& icon, const std::string& introduce);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> dissolveStore(Player& player);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> uploadStoreItem(Player& player, int slot, const std::string& name, const std::string& icon, const std::string& intr, int score);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> offshelfStoreItem(Player& player, const std::string& id, bool returnItem = false);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> buyStoreItem(Player& player, const std::string& id);

        LOICOLLECTION_A_NDAPI ll::Expected<bool> addReview(Player& player, const std::string& storeId, int rating, const std::string& content);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> auditReview(Player& player, const std::string& id, bool approve);

        LOICOLLECTION_A_NDAPI ll::Expected<std::unordered_map<std::string, std::string>> getStore(const std::string& id);
        LOICOLLECTION_A_NDAPI ll::Expected<std::unordered_map<std::string, std::string>> getStore(Player& player);
        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getStoreRanking();
        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getStoreItems(const std::string& storeId);
        LOICOLLECTION_A_NDAPI ll::Expected<std::unordered_map<std::string, std::string>> getStoreItemData(const std::string& id);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> hasPurchasedInStore(Player& player, const std::string& storeId);

        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getReviews(const std::string& storeId, MarketStoreReviewStatus status);
        LOICOLLECTION_A_NDAPI ll::Expected<std::unordered_map<std::string, std::string>> getReviewData(const std::string& id);

        LOICOLLECTION_A_NDAPI static double computeStoreScore(const StoreScoreInput& input, const Config::C_Market& options);

        LOICOLLECTION_A_API   void clearRankCache();
        LOICOLLECTION_A_API   void startRankRefresh();

    private:
        bool isValid() const;

        ll::Expected<std::string> commitStoreSale(Player& player, const std::string& id, const std::unordered_map<std::string, std::string>& data, const std::string& storeId, const std::string& ownerUuid);
        ll::Expected<void> restoreStoreSale(const std::string& id, const std::unordered_map<std::string, std::string>& data, const std::string& saleKey);
        ll::Expected<void> settleSeller(const std::string& ownerUuid, const std::string& itemName, int score, const std::string& scoreboard);

        struct RankData;
        struct Impl;

        std::unique_ptr<Impl> mImpl;
    };
}
