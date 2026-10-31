#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <ll/api/io/LoggerRegistry.h>

#include "LOICollectionA/coro/TimerManager.h"
#include "LOICollectionA/data/sqlite/block/BlockRepository.h"
#include "LOICollectionA/include/server/Plugins/types/market/MarketSchema.h"
#include "LOICollectionA/include/server/Plugins/market/MarketStore.h"

#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "common/coro/MockExecutor.h"
#include "common/data/StorageTestSupport.h"

using namespace LOICollection::server::Plugins;

namespace market_store_window_support {
    constexpr long long kDaySeconds = 86'400LL;

    std::string at(long long secondsAgo) {
        return SystemUtils::toFormatEpoch(
            std::to_string(SystemUtils::getEpochSeconds() - secondsAgo));
    }

    bool seedStore(StoreTable& stores, const std::string& id, const std::string& createdAt) {
        return stores.set(id, StoreCol::name, id).has_value()
            && stores.set(id, StoreCol::introduce, std::string("intro")).has_value()
            && stores.set(id, StoreCol::icon, std::string("icon")).has_value()
            && stores.set(id, StoreCol::owner_uuid, id + "-owner").has_value()
            && stores.set(id, StoreCol::owner_name, id + "-name").has_value()
            && stores.set(id, StoreCol::store_created_at, createdAt).has_value();
    }

    bool seedItem(StoreItemTable& items, const std::string& key, const std::string& storeId) {
        return items.set(key, StoreItemCol::store_id, storeId).has_value()
            && items.set(key, StoreItemCol::name, std::string("widget")).has_value()
            && items.set(key, StoreItemCol::icon, std::string("icon")).has_value()
            && items.set(key, StoreItemCol::introduce, std::string("intro")).has_value()
            && items.set(key, StoreItemCol::score, 10LL).has_value()
            && items.set(key, StoreItemCol::data, std::string("{}")).has_value();
    }

    bool seedSale(
        StoreSaleTable& sales,
        const std::string& key,
        const std::string& storeId,
        long long price,
        const std::string& time
    ) {
        return sales.set(key, StoreSaleCol::store_id, storeId).has_value()
            && sales.set(key, StoreSaleCol::item_name, std::string("widget")).has_value()
            && sales.set(key, StoreSaleCol::price, price).has_value()
            && sales.set(key, StoreSaleCol::tax, 0LL).has_value()
            && sales.set(key, StoreSaleCol::buyer_uuid, std::string("buyer-" + storeId)).has_value()
            && sales.set(key, StoreSaleCol::buyer_name, std::string("buyer")).has_value()
            && sales.set(key, StoreSaleCol::seller_uuid, std::string(storeId + "-owner")).has_value()
            && sales.set(key, StoreSaleCol::time, time).has_value()
            && sales.set(key, StoreSaleCol::source, std::string("store")).has_value();
    }
}

class MarketStoreWindowTest : public testing::Test {
protected:
    void SetUp() override {
        this->mTimerManager = std::make_shared<TimerManager>(this->mExecutor);
        this->mPath = storage_test_support::tempDb("market-store", "window");
        this->mSettingsPath = storage_test_support::tempDb("market-store", "window-settings");

        auto repo = BlockRepository::open(this->mPath.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        this->mRepo = repo.value();

        auto settings = BlockRepository::open(this->mSettingsPath.string(), 2);
        ASSERT_TRUE(settings.has_value()) << settings.error().message();
        this->mSettings = settings.value();

        this->mLogger = ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA");
        ASSERT_NE(this->mLogger, nullptr);

        this->mOptions.StoreTransactionWindowDays = 30;
        this->mOptions.StoreRankExpression = "transactions30";
        this->mOptions.StoreRepeatTradeLimit = 0;
    }

    void TearDown() override {
        this->mRepo.reset();
        this->mSettings.reset();
        storage_test_support::dropDb(this->mPath);
        storage_test_support::dropDb(this->mSettingsPath);
    }

    std::unique_ptr<MarketStore> openStore() {
        return std::make_unique<MarketStore>(
            this->mRepo,
            this->mSettings,
            this->mOptions,
            this->mLogger,
            *this->mTimerManager,
            [](const std::string&) { return std::vector<std::string>{}; }
        );
    }

    MockExecutor mExecutor;
    std::shared_ptr<TimerManager> mTimerManager;
    std::filesystem::path mPath;
    std::filesystem::path mSettingsPath;
    std::shared_ptr<BlockRepository> mRepo;
    std::shared_ptr<BlockRepository> mSettings;
    std::shared_ptr<ll::io::Logger> mLogger;
    Config::C_Market mOptions;
};

TEST_F(MarketStoreWindowTest, RankingIgnoresSalesOutsideTransactionWindow) {
    auto store = this->openStore();

    auto created = store->createTables();
    ASSERT_TRUE(created.has_value()) << created.error().message();

    ASSERT_TRUE(market_store_window_support::seedStore(
        store->stores(), "fresh", market_store_window_support::at(1)));
    ASSERT_TRUE(market_store_window_support::seedStore(
        store->stores(), "stale", market_store_window_support::at(90)));
    ASSERT_TRUE(market_store_window_support::seedItem(store->items(), "fresh-item", "fresh"));
    ASSERT_TRUE(market_store_window_support::seedItem(store->items(), "stale-item", "stale"));

    ASSERT_TRUE(market_store_window_support::seedSale(
        store->sales(), "fresh:1", "fresh", 100, market_store_window_support::at(0)));
    ASSERT_TRUE(market_store_window_support::seedSale(
        store->sales(), "stale:1", "stale", 100, market_store_window_support::at(40 * market_store_window_support::kDaySeconds)));

    auto ranking = store->getStoreRanking();
    ASSERT_TRUE(ranking.has_value()) << ranking.error().message();
    ASSERT_EQ(ranking.value().size(), 2u);
    EXPECT_EQ(ranking.value().front(), "fresh");
    EXPECT_EQ(ranking.value().back(), "stale");
}
