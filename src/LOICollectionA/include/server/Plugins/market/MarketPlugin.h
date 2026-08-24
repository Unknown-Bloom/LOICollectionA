#pragma once

#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <utility>
#include <unordered_map>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/include/ModuleBase.h"
#include "LOICollectionA/include/ModManager.h"

#include "LOICollectionA/include/server/Plugins/market/MarketType.h"
#include "LOICollectionA/include/server/Plugins/market/MarketQuote.h"

class Player;
class ItemStack;
class SQLiteStorage;

namespace Config {
    struct C_Market;
}

namespace ll {
    namespace io {
        class Logger;
    }

    namespace coro {
        class Executor;
    }
}

namespace LOICollection::server::Plugins {
    enum class MarketPluginErrorCode : int {
        Invalid = 1,
        TradeNotFound = 2,
        RequestNotFound = 3,
        ItemNotFound = 4,
        BlacklistNotFound = 5,
        StoreNotFound = 6,
        StoreAlreadyExists = 7,
        StoreItemNotFound = 8,
        StoreReviewNotFound = 9,
        StoreCostInsufficient = 10,
        WantedNotFound = 11,
        WantedExpired = 12,
        WantedFilled = 13,
        WantedFrozenFundFailed = 14,
        AuctionBidTooLow = 15,
        AuctionOutbidRefundFailed = 16,
        CompensationRequired = 17,
        AuctionNotFound = 18
    };

    struct MarketPluginErrorCategory : std::error_category {
        [[nodiscard]] const char* name() const noexcept override {
            return "MarketPluginError";
        }

        [[nodiscard]] std::string message(int ev) const override {
            switch (static_cast<MarketPluginErrorCode>(ev)) {
                case MarketPluginErrorCode::Invalid: return "Plugin is invalid";
                case MarketPluginErrorCode::TradeNotFound: return "Trade not found";
                case MarketPluginErrorCode::RequestNotFound: return "Trade request not found";
                case MarketPluginErrorCode::ItemNotFound: return "Item data not found";
                case MarketPluginErrorCode::BlacklistNotFound: return "Blacklist data not found";
                case MarketPluginErrorCode::StoreNotFound: return "Store not found";
                case MarketPluginErrorCode::StoreAlreadyExists: return "Store already exists";
                case MarketPluginErrorCode::StoreItemNotFound: return "Store item not found";
                case MarketPluginErrorCode::StoreReviewNotFound: return "Store review not found";
                case MarketPluginErrorCode::StoreCostInsufficient: return "Store creation cost insufficient";
                case MarketPluginErrorCode::WantedNotFound: return "Wanted order not found";
                case MarketPluginErrorCode::WantedExpired: return "Wanted order has expired";
                case MarketPluginErrorCode::WantedFilled: return "Wanted order is already filled";
                case MarketPluginErrorCode::WantedFrozenFundFailed: return "Failed to freeze funds for wanted order";
                case MarketPluginErrorCode::AuctionBidTooLow: return "Bid is lower than the minimum increment";
                case MarketPluginErrorCode::AuctionOutbidRefundFailed: return "Failed to refund the outbid bidder";
                case MarketPluginErrorCode::CompensationRequired: return "Game state update failed after commit, compensation required";
                case MarketPluginErrorCode::AuctionNotFound: return "Auction not found";
                default:
                    return "Unknown";
            }
        }
    };

    class MarketPlugin : public std::enable_shared_from_this<MarketPlugin>,
                         public modules::ModuleBase,
                         public modules::AutoRegister<MarketPlugin> {
    public:
        ~MarketPlugin();

        MarketPlugin(MarketPlugin const&) = delete;
        MarketPlugin(MarketPlugin&&) = delete;
        MarketPlugin& operator=(MarketPlugin const&) = delete;
        MarketPlugin& operator=(MarketPlugin&&) = delete;

    public:
        LOICOLLECTION_A_NDAPI static std::shared_ptr<MarketPlugin> getShared();
        LOICOLLECTION_A_NDAPI static std::error_code makeErrorCode(MarketPluginErrorCode e);

        LOICOLLECTION_A_NDAPI std::shared_ptr<SQLiteStorage> getDatabase();
        LOICOLLECTION_A_NDAPI std::shared_ptr<ll::io::Logger> getLogger();
        LOICOLLECTION_A_NDAPI const Config::C_Market& getOptions() const;

        LOICOLLECTION_A_NDAPI ll::Expected<bool> buyItem(Player& player, const std::string& id);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> offshelfItem(Player& player, const std::string& id, bool returnItem = false);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> sellItem(Player& player, int slot, const std::string& name, const std::string& icon, const std::string& intr, int score);

        LOICOLLECTION_A_NDAPI ll::Expected<void> addBlacklist(Player& player, Player& target);
        LOICOLLECTION_A_NDAPI ll::Expected<void> addItem(Player& player, ItemStack& item, const std::string& name, const std::string& icon, const std::string& intr, int score);
        LOICOLLECTION_A_NDAPI ll::Expected<void> delBlacklist(Player& player, const std::string& id);
        LOICOLLECTION_A_NDAPI ll::Expected<void> delItem(const std::string& id);

        LOICOLLECTION_A_NDAPI ll::Expected<void> setExecutor(const ll::coro::Executor& executor);

        LOICOLLECTION_A_NDAPI ll::Expected<bool> acceptRequest(Player& player);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> rejectRequest(Player& player);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> cancelRequest(Player& player);

        LOICOLLECTION_A_NDAPI ll::Expected<bool> acceptTrade(Player& player, int slot, int score);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> cancelTrade(Player& player);

        LOICOLLECTION_A_NDAPI ll::Expected<void> sendRequest(Player& player, Player& target, MarketTradeType type);
        LOICOLLECTION_A_NDAPI ll::Expected<void> sendTrade(Player& player, Player& target, MarketTradeType type);

        LOICOLLECTION_A_NDAPI ll::Expected<bool> hasTrade(Player& player);

        LOICOLLECTION_A_NDAPI ll::Expected<std::string> getBlacklist(Player& player, Player& target);

        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getBlacklist(Player& player);
        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getBlacklist(const std::string& target);
        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getItems();
        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getItems(Player& player);

        LOICOLLECTION_A_NDAPI ll::Expected<std::unordered_map<std::string, std::string>> getItemData(const std::string& id);
        LOICOLLECTION_A_NDAPI ll::Expected<std::unordered_map<std::string, std::string>> getBlacklistData(const std::string& id);
        LOICOLLECTION_A_NDAPI ll::Expected<std::unordered_map<std::string, std::unordered_map<std::string, std::string>>> getItemsData(const std::vector<std::string>& ids);

        LOICOLLECTION_A_NDAPI ll::Expected<bool> hasItem(const std::string& id);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> hasBlacklist(Player& player, const std::string& id);

        LOICOLLECTION_A_NDAPI ll::Expected<bool> createStore(Player& player, const std::string& name, const std::string& icon, const std::string& introduce);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> dissolveStore(Player& player);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> uploadStoreItem(Player& player, int slot, const std::string& name, const std::string& icon, const std::string& intr, int score);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> offshelfStoreItem(Player& player, const std::string& id, bool returnItem = false);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> buyStoreItem(Player& player, const std::string& id, int count = 1);

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

        LOICOLLECTION_A_NDAPI ll::Expected<std::optional<QuoteInfo>> getQuote(const std::string& itemName);
        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::pair<std::string, long long>>> getTopVolume(int limit, int days = 30);
        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::pair<std::string, long long>>> getTopTurnover(int limit, int days = 30);
        LOICOLLECTION_A_NDAPI ll::Expected<QuoteReport> getReport(int days);

        LOICOLLECTION_A_NDAPI ll::Expected<bool> createWanted(Player& player, int slot, const std::string& name, int unitPrice, int amount);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> cancelWanted(Player& player, const std::string& id);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> fillWanted(Player& player, const std::string& id, int amount);
        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getWantedList();
        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getWantedItems(Player& player);
        LOICOLLECTION_A_NDAPI ll::Expected<std::unordered_map<std::string, std::string>> getWantedData(const std::string& id);

        LOICOLLECTION_A_NDAPI ll::Expected<bool> createAuction(Player& player, int slot, const std::string& name, int startPrice, int durationSeconds);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> bidAuction(Player& player, const std::string& id, int price);
        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getAuctionList();
        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getAuctionItems(Player& player);
        LOICOLLECTION_A_NDAPI ll::Expected<std::unordered_map<std::string, std::string>> getAuctionData(const std::string& id);

        LOICOLLECTION_A_API   void clearStoreRankCache();
        LOICOLLECTION_A_API   void startStoreRankRefresh();

        LOICOLLECTION_A_NDAPI static double computeStoreScore(const StoreScoreInput& input, const Config::C_Market& options);

        LOICOLLECTION_A_NDAPI static int computeTax(int price, double rate);

        LOICOLLECTION_A_NDAPI bool isValid();

    public:
        LOICOLLECTION_A_NDAPI std::vector<std::string> getProhibitedItems();

        LOICOLLECTION_A_NDAPI int getBlacklistUpload();
        LOICOLLECTION_A_NDAPI int getMaximumUpload();

    public:
        LOICOLLECTION_A_NDAPI std::string getName() override;

        LOICOLLECTION_A_NDAPI modules::ModulePriority getPriority() override;

        LOICOLLECTION_A_API   ll::Expected<bool> load() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unload() override;
        LOICOLLECTION_A_API   ll::Expected<bool> registry() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unregistry() override;

    private:
        MarketPlugin();

        ll::Expected<void> registeryUI();

        void registeryCommand();
        void listenEvent();
        void unlistenEvent();

        struct TradeEntry;
        struct StoreRankData;
        struct operation;

        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
