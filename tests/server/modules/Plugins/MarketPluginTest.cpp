#include <gtest/gtest.h>

#include <cmath>
#include <chrono>
#include <format>
#include <string>
#include <algorithm>
#include <unordered_map>

#include <ll/api/service/Bedrock.h>
#include <ll/api/thread/ServerThreadExecutor.h>

#include <mc/world/level/Level.h>
#include <mc/world/item/ItemStack.h>
#include <mc/world/item/SaveContext.h>
#include <mc/world/item/SaveContextFactory.h>

#include <mc/world/actor/player/Player.h>
#include <mc/world/actor/player/PlayerInventory.h>
#include <mc/world/actor/player/Inventory.h>

#include <mc/server/SimulatedPlayer.h>

#include <mc/deps/nbt/CompoundTag.h>

#include "LOICollectionA/utils/mc-server/InventoryUtils.h"
#include "LOICollectionA/utils/mc-server/ScoreboardUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/data/SQLiteStorage.h"

#include "LOICollectionA/base/Wrapper.h"
#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/market/MarketPlugin.h"

#include "common/coro/MockExecutor.h"
#include "server/TestSimulatedPlayer.h"

using namespace LOICollection::server::Plugins;

class MarketPluginTest : public testing::Test {
protected:
    std::string mBlacklistId{};
    std::string mItemId{};

protected:
    void SetUp() override {
        if (!MarketPlugin::getShared()->isValid())
            GTEST_SKIP() << "MarketPlugin is not valid";
    }

    void TearDown() override {
        auto db = MarketPlugin::getShared()->getDatabase();

        auto r1 = db->exec("DELETE FROM Blacklist;");
        if (!r1.has_value())
            GTEST_FAIL() << "Unable to clear data";

        auto r2 = db->exec("DELETE FROM Item;");
        if (!r2.has_value())
            GTEST_FAIL() << "Unable to clear data";

        auto r3 = ServiceProvider::getInstance().getService<SQLiteStorage>("SettingsDB")->exec("DELETE FROM Market;");
        if (!r3.has_value())
            GTEST_FAIL() << "Unable to clear data";

        auto r4 = db->exec("DELETE FROM Store;");
        if (!r4.has_value())
            GTEST_FAIL() << "Unable to clear data";

        auto r5 = db->exec("DELETE FROM StoreItem;");
        if (!r5.has_value())
            GTEST_FAIL() << "Unable to clear data";

        auto r6 = db->exec("DELETE FROM StoreSale;");
        if (!r6.has_value())
            GTEST_FAIL() << "Unable to clear data";

        auto r7 = db->exec("DELETE FROM StoreReview;");
        if (!r7.has_value())
            GTEST_FAIL() << "Unable to clear data";

        auto r8 = db->exec("DELETE FROM StoreWanted;");
        if (!r8.has_value())
            GTEST_FAIL() << "Unable to clear data";

        auto r9 = db->exec("DELETE FROM StoreAuction;");
        if (!r9.has_value())
            GTEST_FAIL() << "Unable to clear data";

        MarketPlugin::getShared()->clearStoreRankCache();

        EXPECT_TRUE(MarketPlugin::getShared()->setExecutor(ll::thread::ServerThreadExecutor::getDefault()).has_value());

        Config::C_Market config = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Market;
        if (config.StoreEnabled)
            MarketPlugin::getShared()->startStoreRankRefresh();
    }

    Config::C_Market GetMarketConfig() {
        return ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Market;
    }

    bool PrepareScoreboard(const Config::C_Market& config, bool& created) {
        created = false;

        if (!ScoreboardUtils::hasScoreboard(config.TargetScoreboard)) {
            created = true;

            ScoreboardUtils::create(config.TargetScoreboard);
        }

        return true;
    }

    bool CreateStore(Player& player) {
        Config::C_Market config = GetMarketConfig();
        bool created = false;

        PrepareScoreboard(config, created);

        if (config.StoreCreationCost > 0 && ScoreboardUtils::getScore(player, config.TargetScoreboard) < config.StoreCreationCost)
            ScoreboardUtils::setScore(player, config.TargetScoreboard, config.StoreCreationCost + 1000);

        auto result = MarketPlugin::getShared()->createStore(player, "Test Store", "minecraft:chest", "A test store.");

        return result.has_value() && result.value();
    }

    bool GiveItem(Player& player, const std::string& type, int count) {
        auto itemStack = std::make_unique<ItemStack>();
        itemStack->reinit(type, count, 0);

        InventoryUtils::giveItem(player, *itemStack, count);

        return true;
    }

    int FindSlot(Player& player, const std::string& type) {
        for (int i = 0; i < player.mInventory->mInventory->getContainerSize(); i++) {
            ItemStack mItemStack = player.mInventory->mInventory->getItem(i);

            if (mItemStack && !mItemStack.isNull() && mItemStack.getTypeName() == type)
                return i;
        }

        return -1;
    }

    bool UploadStoreItem(Player& player, const std::string& name = "grass_block", int price = 100) {
        int slot = FindSlot(player, "minecraft:grass_block");
        if (slot < 0)
            return false;

        auto result = MarketPlugin::getShared()->uploadStoreItem(player, slot, name, "minecraft:grass_block", "A grass block.", price);

        return result.has_value() && result.value();
    }

    bool CreateBlacklistEntry() {
        auto sp2 = ll::service::getLevel()->getPlayer("test_player");

        TestSimulatedPlayer sp("test_player6");
        if (!sp.create() || !sp2)
            return false;

        auto addResult = MarketPlugin::getShared()->addBlacklist(*sp2, *sp.getPlayer());
        if (!addResult.has_value()) return false;

        auto id = MarketPlugin::getShared()->getBlacklist(*sp2, *sp.getPlayer());
        if (!id.has_value()) return false;

        this->mBlacklistId = id.value();

        auto has = MarketPlugin::getShared()->hasBlacklist(*sp2, this->mBlacklistId);
        if (!has.has_value()) return false;

        if (this->mBlacklistId.empty() || !has.value())
            return false;

        return sp.destroy();
    }

    bool CreateItemEntry() {
        auto sp = ll::service::getLevel()->getPlayer("test_player");
        if (!sp)
            return false;

        auto itemStack = std::make_unique<ItemStack>();
        itemStack->reinit("minecraft:grass_block", 1, 0);

        auto addResult = MarketPlugin::getShared()->addItem(*sp, *itemStack, "grass_block", "", "A block of grass.", 100);
        if (!addResult.has_value()) return false;

        auto items = MarketPlugin::getShared()->getItems(*sp);
        if (!items.has_value()) return false;
        if (items.value().empty()) return false;

        this->mItemId = items.value().front();

        auto has = MarketPlugin::getShared()->hasItem(this->mItemId);
        if (!has.has_value()) return false;

        if (this->mItemId.empty() || !has.value())
            return false;

        return true;
    }
};

TEST_F(MarketPluginTest, CreateBlacklistEntry) {
    EXPECT_TRUE(CreateBlacklistEntry());
}

TEST_F(MarketPluginTest, DeletePlayerFromBlacklist) {
    EXPECT_TRUE(CreateBlacklistEntry());

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    EXPECT_TRUE(MarketPlugin::getShared()->delBlacklist(*sp, this->mBlacklistId).has_value());

    auto has = MarketPlugin::getShared()->hasBlacklist(*sp, this->mBlacklistId);
    EXPECT_TRUE(has.has_value());
    EXPECT_FALSE(has.value());
}

TEST_F(MarketPluginTest, GetBlacklist) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    TestSimulatedPlayer sp2("test_player6");
    EXPECT_TRUE(sp2.create());

    EXPECT_TRUE(MarketPlugin::getShared()->addBlacklist(*sp, *sp2.getPlayer()).has_value());

    auto blacklists = MarketPlugin::getShared()->getBlacklist(*sp2.getPlayer());
    EXPECT_TRUE(blacklists.has_value());
    EXPECT_FALSE(blacklists.value().empty());
}

TEST_F(MarketPluginTest, GetBlacklistData) {
    EXPECT_TRUE(CreateBlacklistEntry());

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    auto data = MarketPlugin::getShared()->getBlacklistData(this->mBlacklistId);
    EXPECT_TRUE(data.has_value());
    EXPECT_FALSE(data.value().empty());
    EXPECT_TRUE(data.value()["author"] == sp->getUuid().asString());
}

TEST_F(MarketPluginTest, CreateItemEntry) {
    EXPECT_TRUE(CreateItemEntry());
}

TEST_F(MarketPluginTest, DeleteItemEntry) {
    EXPECT_TRUE(CreateItemEntry());

    EXPECT_TRUE(MarketPlugin::getShared()->delItem(this->mItemId).has_value());

    auto has = MarketPlugin::getShared()->hasItem(this->mItemId);
    EXPECT_TRUE(has.has_value());
    EXPECT_FALSE(has.value());
}

TEST_F(MarketPluginTest, GetItemData) {
    EXPECT_TRUE(CreateItemEntry());

    auto data = MarketPlugin::getShared()->getItemData(this->mItemId);
    EXPECT_TRUE(data.has_value());
    EXPECT_FALSE(data.value().empty());
    EXPECT_TRUE(data.value()["name"] == "grass_block");

    auto itemsData = MarketPlugin::getShared()->getItemsData({ this->mItemId });
    EXPECT_TRUE(itemsData.has_value());
    EXPECT_TRUE(itemsData.value().contains(this->mItemId));
}

TEST_F(MarketPluginTest, GetItems) {
    EXPECT_TRUE(CreateItemEntry());

    auto items1 = MarketPlugin::getShared()->getItems();
    EXPECT_TRUE(items1.has_value());
    EXPECT_FALSE(items1.value().empty());

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    auto items2 = MarketPlugin::getShared()->getItems(*sp);
    EXPECT_TRUE(items2.has_value());
    EXPECT_FALSE(items2.value().empty());
}

TEST_F(MarketPluginTest, BuyItem) {
    EXPECT_TRUE(CreateItemEntry());

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    TestSimulatedPlayer sp2("test_player6");
    EXPECT_TRUE(sp2.create());

    Config::C_Market config = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Market;

    bool hasScoreboard = true;
    if (!ScoreboardUtils::hasScoreboard(config.TargetScoreboard)) {
        hasScoreboard = false;

        ScoreboardUtils::create(config.TargetScoreboard);
    }

    ScoreboardUtils::setScore(*sp, config.TargetScoreboard, 0);
    ScoreboardUtils::setScore(*sp2.getPlayer(), config.TargetScoreboard, 100);

    auto buy = MarketPlugin::getShared()->buyItem(*sp2.getPlayer(), this->mItemId);
    EXPECT_TRUE(buy.has_value());
    EXPECT_TRUE(buy.value());

    EXPECT_EQ(ScoreboardUtils::getScore(*sp2.getPlayer(), config.TargetScoreboard), 0);
    EXPECT_EQ(ScoreboardUtils::getScore(*sp, config.TargetScoreboard), 100);

    EXPECT_TRUE(InventoryUtils::isItemInInventory(*sp2.getPlayer(), "minecraft:grass_block", 1));

    if (!hasScoreboard)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

TEST_F(MarketPluginTest, OffshelfItem) {
    EXPECT_TRUE(CreateItemEntry());

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    InventoryUtils::clearItem(*sp, "minecraft:grass_block", 2304);

    EXPECT_FALSE(InventoryUtils::isItemInInventory(*sp, "minecraft:grass_block", 1));

    EXPECT_TRUE(MarketPlugin::getShared()->offshelfItem(*sp, this->mItemId).has_value());

    EXPECT_FALSE(InventoryUtils::isItemInInventory(*sp, "minecraft:grass_block", 1));
}

TEST_F(MarketPluginTest, OffshelfItemReturn) {
    EXPECT_TRUE(CreateItemEntry());

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    InventoryUtils::clearItem(*sp, "minecraft:grass_block", 2304);

    EXPECT_FALSE(InventoryUtils::isItemInInventory(*sp, "minecraft:grass_block", 1));

    EXPECT_TRUE(MarketPlugin::getShared()->offshelfItem(*sp, this->mItemId, true).has_value());

    EXPECT_TRUE(InventoryUtils::isItemInInventory(*sp, "minecraft:grass_block", 1));
}

TEST_F(MarketPluginTest, SellItem) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    auto itemStack = std::make_unique<ItemStack>();
    itemStack->reinit("minecraft:grass_block", 1, 0);
    InventoryUtils::giveItem(*sp, *itemStack, 1);

    int slot = 0;
    for (int i = 0; i < sp->mInventory->mInventory->getContainerSize(); i++) {
        ItemStack mItemStack = sp->mInventory->mInventory->getItem(i);

        if (!mItemStack || mItemStack.isNull() || mItemStack.getTypeName() != "minecraft:grass_block")
            continue;

        slot = i;

        break;
    }

    auto sell = MarketPlugin::getShared()->sellItem(*sp, slot, "grass_block", "", "A block of grass.", 100);
    EXPECT_TRUE(sell.has_value());
    EXPECT_TRUE(sell.value());

    EXPECT_FALSE(InventoryUtils::isItemInInventory(*sp, "minecraft:grass_block", 1));

    auto items = MarketPlugin::getShared()->getItems(*sp);
    ASSERT_TRUE(items.has_value());
    ASSERT_FALSE(items.value().empty());

    std::string id = items.value().front();
    EXPECT_FALSE(id.empty());

    auto has = MarketPlugin::getShared()->hasItem(id);
    EXPECT_TRUE(has.has_value());
    EXPECT_TRUE(has.value());
}

TEST_F(MarketPluginTest, SendRequestTimeout) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    TestSimulatedPlayer sp2("test_player6");
    EXPECT_TRUE(sp2.create());

    Config::C_Market config = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Market;

    MockExecutor executor;

    EXPECT_TRUE(MarketPlugin::getShared()->setExecutor(executor).has_value());
    EXPECT_TRUE(MarketPlugin::getShared()->sendRequest(*sp, *sp2.getPlayer(), MarketTradeType::buy).has_value());

    auto has1 = MarketPlugin::getShared()->hasTrade(*sp);
    EXPECT_TRUE(has1.has_value());
    EXPECT_TRUE(has1.value());

    executor.advanceTime(std::chrono::seconds(config.TradeRequestTimeout + 1));

    auto has2 = MarketPlugin::getShared()->hasTrade(*sp);
    EXPECT_TRUE(has2.has_value());
    EXPECT_FALSE(has2.value());
}

TEST_F(MarketPluginTest, SendTradeTimeout) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    TestSimulatedPlayer sp2("test_player6");
    EXPECT_TRUE(sp2.create());

    Config::C_Market config = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Market;

    MockExecutor executor;

    EXPECT_TRUE(MarketPlugin::getShared()->setExecutor(executor).has_value());
    EXPECT_TRUE(MarketPlugin::getShared()->sendTrade(*sp, *sp2.getPlayer(), MarketTradeType::buy).has_value());

    auto has1 = MarketPlugin::getShared()->hasTrade(*sp);
    EXPECT_TRUE(has1.has_value());
    EXPECT_TRUE(has1.value());

    executor.advanceTime(std::chrono::seconds(config.TradeTimeout + 1));

    auto has2 = MarketPlugin::getShared()->hasTrade(*sp);
    EXPECT_TRUE(has2.has_value());
    EXPECT_FALSE(has2.value());
}

TEST_F(MarketPluginTest, AcceptRequest) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    TestSimulatedPlayer sp2("test_player6");
    EXPECT_TRUE(sp2.create());

    Config::C_Market config = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Market;

    MockExecutor executor;

    EXPECT_TRUE(MarketPlugin::getShared()->setExecutor(executor).has_value());
    EXPECT_TRUE(MarketPlugin::getShared()->sendRequest(*sp, *sp2.getPlayer(), MarketTradeType::buy).has_value());

    auto accept = MarketPlugin::getShared()->acceptRequest(*sp2.getPlayer());
    EXPECT_TRUE(accept.has_value());
    EXPECT_TRUE(accept.value());

    auto has1 = MarketPlugin::getShared()->hasTrade(*sp);
    EXPECT_TRUE(has1.has_value());
    EXPECT_TRUE(has1.value());

    executor.advanceTime(std::chrono::seconds(config.TradeTimeout + 1));

    auto has2 = MarketPlugin::getShared()->hasTrade(*sp);
    EXPECT_TRUE(has2.has_value());
    EXPECT_FALSE(has2.value());
}

TEST_F(MarketPluginTest, RejectRequest) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    TestSimulatedPlayer sp2("test_player6");
    EXPECT_TRUE(sp2.create());

    EXPECT_TRUE(MarketPlugin::getShared()->sendRequest(*sp, *sp2.getPlayer(), MarketTradeType::buy).has_value());

    auto reject = MarketPlugin::getShared()->rejectRequest(*sp2.getPlayer());
    EXPECT_TRUE(reject.has_value());
    EXPECT_TRUE(reject.value());

    auto has = MarketPlugin::getShared()->hasTrade(*sp);
    EXPECT_TRUE(has.has_value());
    EXPECT_FALSE(has.value());
}

TEST_F(MarketPluginTest, CancelRequest) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    TestSimulatedPlayer sp2("test_player6");
    EXPECT_TRUE(sp2.create());

    EXPECT_TRUE(MarketPlugin::getShared()->sendRequest(*sp, *sp2.getPlayer(), MarketTradeType::buy).has_value());

    auto cancel = MarketPlugin::getShared()->cancelRequest(*sp);
    EXPECT_TRUE(cancel.has_value());
    EXPECT_TRUE(cancel.value());

    auto has = MarketPlugin::getShared()->hasTrade(*sp);
    EXPECT_TRUE(has.has_value());
    EXPECT_FALSE(has.value());
}

TEST_F(MarketPluginTest, AcceptBuyTrade) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    TestSimulatedPlayer sp2("test_player6");
    EXPECT_TRUE(sp2.create());

    Config::C_Market config = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Market;

    bool hasScoreboard = true;
    if (!ScoreboardUtils::hasScoreboard(config.TargetScoreboard)) {
        hasScoreboard = false;

        ScoreboardUtils::create(config.TargetScoreboard);
    }

    ScoreboardUtils::setScore(*sp, config.TargetScoreboard, 100);
    ScoreboardUtils::setScore(*sp2.getPlayer(), config.TargetScoreboard, 0);

    auto itemStack = std::make_unique<ItemStack>();
    itemStack->reinit("minecraft:grass_block", 1, 0);

    sp2.getPlayer()->mInventory->mInventory->setItem(0, *itemStack);

    EXPECT_TRUE(MarketPlugin::getShared()->sendTrade(*sp, *sp2.getPlayer(), MarketTradeType::buy).has_value());

    auto accept = MarketPlugin::getShared()->acceptTrade(*sp2.getPlayer(), 0, 100);
    EXPECT_TRUE(accept.has_value());
    EXPECT_TRUE(accept.value());

    EXPECT_TRUE(InventoryUtils::isItemInInventory(*sp, "minecraft:grass_block", 1));
    EXPECT_FALSE(InventoryUtils::isItemInInventory(*sp2.getPlayer(), "minecraft:grass_block", 1));
    EXPECT_EQ(ScoreboardUtils::getScore(*sp, config.TargetScoreboard), 0);
    EXPECT_EQ(ScoreboardUtils::getScore(*sp2.getPlayer(), config.TargetScoreboard), 100);

    if (!hasScoreboard)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

TEST_F(MarketPluginTest, AcceptSellTrade) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    TestSimulatedPlayer sp2("test_player6");
    EXPECT_TRUE(sp2.create());

    Config::C_Market config = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Market;

    bool hasScoreboard = true;
    if (!ScoreboardUtils::hasScoreboard(config.TargetScoreboard)) {
        hasScoreboard = false;

        ScoreboardUtils::create(config.TargetScoreboard);
    }

    InventoryUtils::clearItem(*sp, "minecraft:grass_block", 2304);
    InventoryUtils::clearItem(*sp2.getPlayer(), "minecraft:grass_block", 2304);

    ScoreboardUtils::setScore(*sp, config.TargetScoreboard, 0);
    ScoreboardUtils::setScore(*sp2.getPlayer(), config.TargetScoreboard, 100);

    auto itemStack = std::make_unique<ItemStack>();
    itemStack->reinit("minecraft:grass_block", 1, 0);
    sp->mInventory->mInventory->setItem(0, *itemStack);

    EXPECT_TRUE(MarketPlugin::getShared()->sendTrade(*sp, *sp2.getPlayer(), MarketTradeType::sell).has_value());

    auto accept = MarketPlugin::getShared()->acceptTrade(*sp, 0, 100);
    EXPECT_TRUE(accept.has_value());
    EXPECT_TRUE(accept.value());

    EXPECT_FALSE(InventoryUtils::isItemInInventory(*sp, "minecraft:grass_block", 1));
    EXPECT_TRUE(InventoryUtils::isItemInInventory(*sp2.getPlayer(), "minecraft:grass_block", 1));
    EXPECT_EQ(ScoreboardUtils::getScore(*sp, config.TargetScoreboard), 100);
    EXPECT_EQ(ScoreboardUtils::getScore(*sp2.getPlayer(), config.TargetScoreboard), 0);

    if (!hasScoreboard)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

TEST_F(MarketPluginTest, CancelTrade) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    TestSimulatedPlayer sp2("test_player6");
    EXPECT_TRUE(sp2.create());

    EXPECT_TRUE(MarketPlugin::getShared()->sendTrade(*sp, *sp2.getPlayer(), MarketTradeType::buy).has_value());

    auto cancel = MarketPlugin::getShared()->cancelTrade(*sp);
    EXPECT_TRUE(cancel.has_value());
    EXPECT_TRUE(cancel.value());

    auto has = MarketPlugin::getShared()->hasTrade(*sp);
    EXPECT_TRUE(has.has_value());
    EXPECT_FALSE(has.value());
}

Config::C_Market MakeStoreScoreConfig() {
    Config::C_Market config;

    config.StoreSalesWeight = 0.5;
    config.StoreVolumeWeight = 0.3;
    config.StoreRatingWeight = 0.35;
    config.StoreBadReviewPenalty = 3.0;
    config.StoreColdStartWeight = 0.5;
    config.StoreRatingSmoothing = 15.0;
    config.StoreColdStartDays = 7;
    config.StoreTransactionWindowDays = 30;
    config.StoreRatingWindowDays = 180;
    config.StoreRiskWindowDays = 30;

    return config;
}

std::string FormatTimeDaysAgo(int days) {
    auto now = std::chrono::system_clock::now();
    auto past = std::chrono::floor<std::chrono::seconds>(now - std::chrono::hours(24LL * days));
    auto local = std::chrono::zoned_time(std::chrono::current_zone(), past);

    return std::format("{:%Y-%m-%d %H:%M:%S}", local);
}

TEST(MarketStoreScoreTest, Log1pCompression) {
    Config::C_Market config = MakeStoreScoreConfig();

    auto score = [&config](int transactions) -> double {
        StoreScoreInput input;
        input.ageDays = 30.0;
        input.transactions30 = transactions;

        return MarketPlugin::computeStoreScore(input, config);
    };

    double increment1 = score(1) - score(0);
    double increment2 = score(2) - score(1);

    EXPECT_GT(increment1, increment2);
    EXPECT_GT(increment2, 0.0);
}

TEST(MarketStoreScoreTest, BayesianShrink) {
    Config::C_Market config = MakeStoreScoreConfig();

    StoreScoreInput small;
    small.ageDays = 30.0;
    small.approvedReviews = 1;
    small.approvedAverage = 5.0;
    small.approved180 = 1;
    small.globalApprovedAverage = 3.0;

    StoreScoreInput large = small;
    large.approvedReviews = 100;

    double smallScore = MarketPlugin::computeStoreScore(small, config);
    double largeScore = MarketPlugin::computeStoreScore(large, config);

    EXPECT_LT(smallScore, largeScore);
    EXPECT_NEAR(smallScore, 0.35 * ((5.0 * 1 + 3.0 * 15.0) / 16.0 - 3.0) * std::log1p(1.0), 1e-9);
}

TEST(MarketStoreScoreTest, NoReviewsRatingZero) {
    Config::C_Market config = MakeStoreScoreConfig();

    StoreScoreInput input;
    input.ageDays = 30.0;
    input.transactions30 = 10;
    input.volume30 = 100;
    input.approvedReviews = 0;
    input.approved180 = 0;

    double expected = (0.5 * std::log1p(10.0) + 0.3 * std::log1p(100.0)) * 1.0;

    EXPECT_NEAR(MarketPlugin::computeStoreScore(input, config), expected, 1e-9);
}

TEST(MarketStoreScoreTest, ColdStartLinear) {
    Config::C_Market config = MakeStoreScoreConfig();

    auto score = [&config](double ageDays) -> double {
        StoreScoreInput input;
        input.ageDays = ageDays;

        return MarketPlugin::computeStoreScore(input, config);
    };

    EXPECT_NEAR(score(0.0), 0.5, 1e-9);
    EXPECT_NEAR(score(3.5), 0.25, 1e-9);
    EXPECT_NEAR(score(7.0), 0.0, 1e-9);
    EXPECT_NEAR(score(14.0), 0.0, 1e-9);
}

TEST(MarketStoreScoreTest, BadReviewPenaltyAndNegative) {
    Config::C_Market config = MakeStoreScoreConfig();

    StoreScoreInput input;
    input.ageDays = 30.0;
    input.reviews30 = 2;
    input.badReviews30 = 2;

    double score = MarketPlugin::computeStoreScore(input, config);

    EXPECT_NEAR(score, -3.0, 1e-9);
    EXPECT_LT(score, 0.0);
}

TEST_F(MarketPluginTest, StoreCreateDuplicateAndCost) {
    Config::C_Market config = GetMarketConfig();
    if (!config.StoreEnabled)
        GTEST_SKIP();

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    ASSERT_TRUE(sp);

    bool created = false;
    PrepareScoreboard(config, created);

    int base = 10000;
    ScoreboardUtils::setScore(*sp, config.TargetScoreboard, base);

    auto create = MarketPlugin::getShared()->createStore(*sp, "Test Store", "minecraft:chest", "A test store.");
    ASSERT_TRUE(create.has_value());
    EXPECT_TRUE(create.value());

    EXPECT_EQ(ScoreboardUtils::getScore(*sp, config.TargetScoreboard), base - std::max(0, config.StoreCreationCost));

    auto duplicate = MarketPlugin::getShared()->createStore(*sp, "Another Store", "minecraft:chest", "duplicate");
    EXPECT_FALSE(duplicate.has_value());

    auto db = MarketPlugin::getShared()->getDatabase();
    ASSERT_TRUE(db->exec("DELETE FROM Store;").has_value());
    MarketPlugin::getShared()->clearStoreRankCache();

    if (config.StoreCreationCost > 0) {
        ScoreboardUtils::setScore(*sp, config.TargetScoreboard, config.StoreCreationCost - 1);

        auto poor = MarketPlugin::getShared()->createStore(*sp, "Poor Store", "minecraft:chest", "poor");
        EXPECT_FALSE(poor.has_value());

        if (!poor.has_value()) {
            EXPECT_TRUE(poor.error().isA<ll::ErrorCodeError>());
            EXPECT_EQ(poor.error().as<ll::ErrorCodeError>().ec, MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreCostInsufficient));
        }
    }

    if (created)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

TEST_F(MarketPluginTest, StoreDissolveRequiresEmpty) {
    Config::C_Market config = GetMarketConfig();
    if (!config.StoreEnabled)
        GTEST_SKIP();

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    ASSERT_TRUE(sp);

    ASSERT_TRUE(CreateStore(*sp));
    ASSERT_TRUE(GiveItem(*sp, "minecraft:grass_block", 1));
    ASSERT_TRUE(UploadStoreItem(*sp));

    auto dissolve = MarketPlugin::getShared()->dissolveStore(*sp);
    ASSERT_TRUE(dissolve.has_value());
    EXPECT_FALSE(dissolve.value());

    auto items = MarketPlugin::getShared()->getStoreItems(sp->getUuid().asString());
    ASSERT_TRUE(items.has_value());
    ASSERT_FALSE(items.value().empty());

    auto off = MarketPlugin::getShared()->offshelfStoreItem(*sp, items.value().front(), true);
    ASSERT_TRUE(off.has_value());
    EXPECT_TRUE(off.value());

    dissolve = MarketPlugin::getShared()->dissolveStore(*sp);
    ASSERT_TRUE(dissolve.has_value());
    EXPECT_TRUE(dissolve.value());

    auto has = MarketPlugin::getShared()->getDatabase()->has("Store", sp->getUuid().asString());
    ASSERT_TRUE(has.has_value());
    EXPECT_FALSE(has.value());
}

TEST_F(MarketPluginTest, StoreUploadOffshelf) {
    Config::C_Market config = GetMarketConfig();
    if (!config.StoreEnabled)
        GTEST_SKIP();

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    ASSERT_TRUE(sp);

    ASSERT_TRUE(CreateStore(*sp));
    ASSERT_TRUE(GiveItem(*sp, "minecraft:grass_block", 1));
    ASSERT_TRUE(UploadStoreItem(*sp));

    auto items = MarketPlugin::getShared()->getStoreItems(sp->getUuid().asString());
    ASSERT_TRUE(items.has_value());
    ASSERT_FALSE(items.value().empty());

    auto data = MarketPlugin::getShared()->getStoreItemData(items.value().front());
    ASSERT_TRUE(data.has_value());
    EXPECT_EQ(data.value().at("name"), "grass_block");

    InventoryUtils::clearItem(*sp, "minecraft:grass_block", 2304);
    EXPECT_FALSE(InventoryUtils::isItemInInventory(*sp, "minecraft:grass_block", 1));

    auto off = MarketPlugin::getShared()->offshelfStoreItem(*sp, items.value().front(), true);
    ASSERT_TRUE(off.has_value());
    EXPECT_TRUE(off.value());

    EXPECT_TRUE(InventoryUtils::isItemInInventory(*sp, "minecraft:grass_block", 1));
}

TEST_F(MarketPluginTest, StoreBuyOnlineOwner) {
    Config::C_Market config = GetMarketConfig();
    if (!config.StoreEnabled)
        GTEST_SKIP();

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    ASSERT_TRUE(sp);

    TestSimulatedPlayer buyer("test_player6");
    ASSERT_TRUE(buyer.create());

    ASSERT_TRUE(CreateStore(*sp));
    ASSERT_TRUE(GiveItem(*sp, "minecraft:grass_block", 1));
    ASSERT_TRUE(UploadStoreItem(*sp, "grass_block", 100));

    auto items = MarketPlugin::getShared()->getStoreItems(sp->getUuid().asString());
    ASSERT_TRUE(items.has_value());
    ASSERT_FALSE(items.value().empty());

    bool created = false;
    PrepareScoreboard(config, created);

    ScoreboardUtils::setScore(*sp, config.TargetScoreboard, 0);
    ScoreboardUtils::setScore(*buyer.getPlayer(), config.TargetScoreboard, 50);

    auto poor = MarketPlugin::getShared()->buyStoreItem(*buyer.getPlayer(), items.value().front());
    ASSERT_TRUE(poor.has_value());
    EXPECT_FALSE(poor.value());
    EXPECT_EQ(ScoreboardUtils::getScore(*buyer.getPlayer(), config.TargetScoreboard), 50);

    ScoreboardUtils::setScore(*buyer.getPlayer(), config.TargetScoreboard, 1000);

    auto buy = MarketPlugin::getShared()->buyStoreItem(*buyer.getPlayer(), items.value().front());
    ASSERT_TRUE(buy.has_value());
    EXPECT_TRUE(buy.value());

    EXPECT_EQ(ScoreboardUtils::getScore(*buyer.getPlayer(), config.TargetScoreboard), 900);
    EXPECT_EQ(ScoreboardUtils::getScore(*sp, config.TargetScoreboard), 100);
    EXPECT_TRUE(InventoryUtils::isItemInInventory(*buyer.getPlayer(), "minecraft:grass_block", 1));

    auto sales = MarketPlugin::getShared()->getDatabase()->find("StoreSale", {
        { "store_id", sp->getUuid().asString() },
        { "buyer_uuid", buyer.getPlayer()->getUuid().asString() }
    }, SQLiteStorage::FindCondition::AND);
    ASSERT_TRUE(sales.has_value());
    EXPECT_FALSE(sales.value().empty());

    auto has = MarketPlugin::getShared()->getDatabase()->has("StoreItem", items.value().front());
    ASSERT_TRUE(has.has_value());
    EXPECT_FALSE(has.value());

    if (created)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

TEST_F(MarketPluginTest, StoreBuyOfflineOwner) {
    Config::C_Market config = GetMarketConfig();
    if (!config.StoreEnabled)
        GTEST_SKIP();

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    ASSERT_TRUE(sp);

    std::string ownerUuid = "00000000-0000-0000-0000-00000000dead";
    std::string nowTime = SystemUtils::getNowTime();
    auto db = MarketPlugin::getShared()->getDatabase();

    auto itemStack = std::make_unique<ItemStack>();
    itemStack->reinit("minecraft:grass_block", 1, 0);
    std::string snbt = itemStack->save(*SaveContextFactory::createCloneSaveContext())->toSnbt(SnbtFormat::Minimize, 0);

    ASSERT_TRUE(db->exec("INSERT INTO Store (key, name, introduce, icon, owner_uuid, owner_name, store_created_at) VALUES ('" +
        ownerUuid + "', 'Offline Store', 'A store.', 'minecraft:chest', '" + ownerUuid + "', 'Offline Owner', '" + nowTime + "');").has_value());

    std::string itemId = SystemUtils::getCurrentTimestamp();
    ASSERT_TRUE(db->exec("INSERT INTO StoreItem (key, store_id, name, icon, introduce, score, data) VALUES ('" +
        itemId + "', '" + ownerUuid + "', 'grass_block', 'minecraft:grass_block', 'A grass block.', '100', '" + snbt + "');").has_value());

    MarketPlugin::getShared()->clearStoreRankCache();

    bool created = false;
    PrepareScoreboard(config, created);
    ScoreboardUtils::setScore(*sp, config.TargetScoreboard, 1000);

    auto buy = MarketPlugin::getShared()->buyStoreItem(*sp, itemId);
    ASSERT_TRUE(buy.has_value());
    EXPECT_TRUE(buy.value());

    EXPECT_EQ(ScoreboardUtils::getScore(*sp, config.TargetScoreboard), 900);
    EXPECT_TRUE(InventoryUtils::isItemInInventory(*sp, "minecraft:grass_block", 1));

    auto stored = ServiceProvider::getInstance().getService<SQLiteStorage>("SettingsDB")->get("Market", ownerUuid, "Score", "0");
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(SystemUtils::toInt(stored.value(), -1), 100);

    if (created)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

TEST_F(MarketPluginTest, StoreReviewValidation) {
    Config::C_Market config = GetMarketConfig();
    if (!config.StoreEnabled || !config.StoreReviewEnabled)
        GTEST_SKIP();

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    ASSERT_TRUE(sp);

    TestSimulatedPlayer buyer("test_player6");
    ASSERT_TRUE(buyer.create());

    ASSERT_TRUE(CreateStore(*sp));
    ASSERT_TRUE(GiveItem(*sp, "minecraft:grass_block", 1));
    ASSERT_TRUE(UploadStoreItem(*sp, "grass_block", 100));

    auto items = MarketPlugin::getShared()->getStoreItems(sp->getUuid().asString());
    ASSERT_TRUE(items.has_value());
    ASSERT_FALSE(items.value().empty());

    bool created = false;
    PrepareScoreboard(config, created);
    ScoreboardUtils::setScore(*buyer.getPlayer(), config.TargetScoreboard, 1000);

    auto buy = MarketPlugin::getShared()->buyStoreItem(*buyer.getPlayer(), items.value().front());
    ASSERT_TRUE(buy.has_value());
    EXPECT_TRUE(buy.value());

    std::string storeId = sp->getUuid().asString();

    auto noPurchase = MarketPlugin::getShared()->addReview(*sp, storeId, 5, "good");
    ASSERT_TRUE(noPurchase.has_value());
    EXPECT_FALSE(noPurchase.value());

    auto badRating = MarketPlugin::getShared()->addReview(*buyer.getPlayer(), storeId, 0, "good");
    ASSERT_TRUE(badRating.has_value());
    EXPECT_FALSE(badRating.value());

    auto badRating2 = MarketPlugin::getShared()->addReview(*buyer.getPlayer(), storeId, 6, "good");
    ASSERT_TRUE(badRating2.has_value());
    EXPECT_FALSE(badRating2.value());

    auto emptyContent = MarketPlugin::getShared()->addReview(*buyer.getPlayer(), storeId, 5, "   ");
    ASSERT_TRUE(emptyContent.has_value());
    EXPECT_FALSE(emptyContent.value());

    auto ok = MarketPlugin::getShared()->addReview(*buyer.getPlayer(), storeId, 5, "good");
    ASSERT_TRUE(ok.has_value());
    EXPECT_TRUE(ok.value());

    auto duplicate = MarketPlugin::getShared()->addReview(*buyer.getPlayer(), storeId, 4, "again");
    ASSERT_TRUE(duplicate.has_value());
    EXPECT_FALSE(duplicate.value());

    auto pending = MarketPlugin::getShared()->getReviews(storeId, MarketStoreReviewStatus::pending);
    ASSERT_TRUE(pending.has_value());
    EXPECT_EQ(pending.value().size(), 1);

    auto approved = MarketPlugin::getShared()->getReviews(storeId, MarketStoreReviewStatus::approved);
    ASSERT_TRUE(approved.has_value());
    EXPECT_TRUE(approved.value().empty());

    if (created)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

TEST_F(MarketPluginTest, StoreReviewStatusScoring) {
    Config::C_Market config = GetMarketConfig();
    if (!config.StoreEnabled || !config.StoreReviewEnabled)
        GTEST_SKIP();

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    ASSERT_TRUE(sp);

    TestSimulatedPlayer buyer("test_player6");
    ASSERT_TRUE(buyer.create());

    ASSERT_TRUE(CreateStore(*sp));
    ASSERT_TRUE(GiveItem(*sp, "minecraft:grass_block", 1));
    ASSERT_TRUE(UploadStoreItem(*sp, "grass_block", 100));

    auto items = MarketPlugin::getShared()->getStoreItems(sp->getUuid().asString());
    ASSERT_TRUE(items.has_value());
    ASSERT_FALSE(items.value().empty());

    bool created = false;
    PrepareScoreboard(config, created);
    ScoreboardUtils::setScore(*buyer.getPlayer(), config.TargetScoreboard, 1000);

    auto buy = MarketPlugin::getShared()->buyStoreItem(*buyer.getPlayer(), items.value().front());
    ASSERT_TRUE(buy.has_value());
    EXPECT_TRUE(buy.value());

    std::string storeId = sp->getUuid().asString();

    auto ok = MarketPlugin::getShared()->addReview(*buyer.getPlayer(), storeId, 5, "good");
    ASSERT_TRUE(ok.has_value());
    EXPECT_TRUE(ok.value());

    auto pending = MarketPlugin::getShared()->getReviews(storeId, MarketStoreReviewStatus::pending);
    ASSERT_TRUE(pending.has_value());
    ASSERT_EQ(pending.value().size(), 1);
    std::string reviewKey = pending.value().front();

    auto audit = MarketPlugin::getShared()->auditReview(*sp, reviewKey, true);
    ASSERT_TRUE(audit.has_value());
    EXPECT_FALSE(audit.value());

    auto db = MarketPlugin::getShared()->getDatabase();
    ASSERT_TRUE(db->exec("UPDATE StoreReview SET status='approved' WHERE key='" + reviewKey + "';").has_value());
    MarketPlugin::getShared()->clearStoreRankCache();

    auto approved = MarketPlugin::getShared()->getReviews(storeId, MarketStoreReviewStatus::approved);
    ASSERT_TRUE(approved.has_value());
    EXPECT_EQ(approved.value().size(), 1);

    TestSimulatedPlayer buyer2("test_player7");
    ASSERT_TRUE(buyer2.create());
    ScoreboardUtils::setScore(*buyer2.getPlayer(), config.TargetScoreboard, 1000);

    ASSERT_TRUE(GiveItem(*sp, "minecraft:grass_block", 1));
    ASSERT_TRUE(UploadStoreItem(*sp, "grass_block2", 50));

    auto items2 = MarketPlugin::getShared()->getStoreItems(storeId);
    ASSERT_TRUE(items2.has_value());
    ASSERT_FALSE(items2.value().empty());

    auto buy2 = MarketPlugin::getShared()->buyStoreItem(*buyer2.getPlayer(), items2.value().front());
    ASSERT_TRUE(buy2.has_value());
    EXPECT_TRUE(buy2.value());

    auto ok2 = MarketPlugin::getShared()->addReview(*buyer2.getPlayer(), storeId, 1, "bad");
    ASSERT_TRUE(ok2.has_value());
    EXPECT_TRUE(ok2.value());

    auto pending2 = MarketPlugin::getShared()->getReviews(storeId, MarketStoreReviewStatus::pending);
    ASSERT_TRUE(pending2.has_value());
    ASSERT_EQ(pending2.value().size(), 1);

    ASSERT_TRUE(db->exec("UPDATE StoreReview SET status='rejected' WHERE key='" + pending2.value().front() + "';").has_value());
    MarketPlugin::getShared()->clearStoreRankCache();

    auto approvedFinal = MarketPlugin::getShared()->getReviews(storeId, MarketStoreReviewStatus::approved);
    ASSERT_TRUE(approvedFinal.has_value());
    EXPECT_EQ(approvedFinal.value().size(), 1);

    auto rejected = MarketPlugin::getShared()->getReviews(storeId, MarketStoreReviewStatus::rejected);
    ASSERT_TRUE(rejected.has_value());
    EXPECT_EQ(rejected.value().size(), 1);

    if (created)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

TEST_F(MarketPluginTest, StoreRankingCacheInvalidation) {
    Config::C_Market config = GetMarketConfig();
    if (!config.StoreEnabled)
        GTEST_SKIP();

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    ASSERT_TRUE(sp);
    std::string uuidA = sp->getUuid().asString();

    ASSERT_TRUE(CreateStore(*sp));
    ASSERT_TRUE(GiveItem(*sp, "minecraft:grass_block", 1));
    ASSERT_TRUE(UploadStoreItem(*sp, "grass_block", 1));

    auto rank1 = MarketPlugin::getShared()->getStoreRanking();
    ASSERT_TRUE(rank1.has_value());
    ASSERT_EQ(rank1.value().size(), 1);
    EXPECT_EQ(rank1.value().front(), uuidA);

    TestSimulatedPlayer sp2("test_player6");
    ASSERT_TRUE(sp2.create());
    std::string uuidB = sp2.getPlayer()->getUuid().asString();

    ASSERT_TRUE(CreateStore(*sp2.getPlayer()));
    ASSERT_TRUE(GiveItem(*sp2.getPlayer(), "minecraft:grass_block", 1));
    ASSERT_TRUE(UploadStoreItem(*sp2.getPlayer(), "grass_block", 1));

    auto rank2 = MarketPlugin::getShared()->getStoreRanking();
    ASSERT_TRUE(rank2.has_value());
    ASSERT_EQ(rank2.value().size(), 2);

    auto bItems = MarketPlugin::getShared()->getStoreItems(uuidB);
    ASSERT_TRUE(bItems.has_value());
    ASSERT_FALSE(bItems.value().empty());

    auto off = MarketPlugin::getShared()->offshelfStoreItem(*sp2.getPlayer(), bItems.value().front(), true);
    ASSERT_TRUE(off.has_value());
    EXPECT_TRUE(off.value());

    auto rank3 = MarketPlugin::getShared()->getStoreRanking();
    ASSERT_TRUE(rank3.has_value());
    ASSERT_EQ(rank3.value().size(), 1);
    EXPECT_EQ(rank3.value().front(), uuidA);
}

TEST_F(MarketPluginTest, StoreRankingCacheHitAndWindow) {
    Config::C_Market config = GetMarketConfig();
    if (!config.StoreEnabled)
        GTEST_SKIP();

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    ASSERT_TRUE(sp);
    std::string uuidA = sp->getUuid().asString();

    ASSERT_TRUE(CreateStore(*sp));
    ASSERT_TRUE(GiveItem(*sp, "minecraft:grass_block", 1));
    ASSERT_TRUE(UploadStoreItem(*sp, "grass_block", 1));

    TestSimulatedPlayer sp2("test_player6");
    ASSERT_TRUE(sp2.create());
    std::string uuidB = sp2.getPlayer()->getUuid().asString();

    ASSERT_TRUE(CreateStore(*sp2.getPlayer()));
    ASSERT_TRUE(GiveItem(*sp2.getPlayer(), "minecraft:grass_block", 1));
    ASSERT_TRUE(UploadStoreItem(*sp2.getPlayer(), "grass_block", 1));

    auto db = MarketPlugin::getShared()->getDatabase();
    long long nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    std::string nowTime = SystemUtils::getNowTime();
    std::string past31 = FormatTimeDaysAgo(31);
    std::string past30 = FormatTimeDaysAgo(30);
    std::string past40 = FormatTimeDaysAgo(40);

    ASSERT_TRUE(db->exec("UPDATE Store SET store_created_at='" + past31 + "' WHERE key='" + uuidA + "';").has_value());
    ASSERT_TRUE(db->exec("UPDATE Store SET store_created_at='" + past30 + "' WHERE key='" + uuidB + "';").has_value());

    MarketPlugin::getShared()->clearStoreRankCache();

    auto rank1 = MarketPlugin::getShared()->getStoreRanking();
    ASSERT_TRUE(rank1.has_value());
    ASSERT_EQ(rank1.value().size(), 2);
    EXPECT_EQ(rank1.value().front(), uuidA);

    std::string saleKey = std::to_string(nowNs + 1);
    ASSERT_TRUE(db->exec("INSERT INTO StoreSale (key, store_id, item_name, price, buyer_uuid, buyer_name, time) VALUES ('" +
        saleKey + "', '" + uuidB + "', 'grass_block', '1000', 'buyer', 'buyer', '" + nowTime + "');").has_value());

    auto rank2 = MarketPlugin::getShared()->getStoreRanking();
    ASSERT_TRUE(rank2.has_value());
    EXPECT_EQ(rank2.value().front(), uuidA);

    MarketPlugin::getShared()->clearStoreRankCache();

    auto rank3 = MarketPlugin::getShared()->getStoreRanking();
    ASSERT_TRUE(rank3.has_value());
    EXPECT_EQ(rank3.value().front(), uuidB);

    ASSERT_TRUE(db->exec("INSERT INTO StoreSale (key, store_id, item_name, price, buyer_uuid, buyer_name, time) VALUES ('" +
        std::to_string(nowNs + 2) + "', '" + uuidA + "', 'grass_block', '1000000000', 'buyer', 'buyer', '" + past40 + "');").has_value());
    MarketPlugin::getShared()->clearStoreRankCache();

    auto rank4 = MarketPlugin::getShared()->getStoreRanking();
    ASSERT_TRUE(rank4.has_value());
    EXPECT_EQ(rank4.value().front(), uuidB);

    ASSERT_TRUE(db->exec("INSERT INTO StoreSale (key, store_id, item_name, price, buyer_uuid, buyer_name, time) VALUES ('" +
        std::to_string(nowNs + 3) + "', '" + uuidA + "', 'grass_block', '100000', 'buyer', 'buyer', '" + nowTime + "');").has_value());
    MarketPlugin::getShared()->clearStoreRankCache();

    auto rank5 = MarketPlugin::getShared()->getStoreRanking();
    ASSERT_TRUE(rank5.has_value());
    EXPECT_EQ(rank5.value().front(), uuidA);
}

TEST_F(MarketPluginTest, StoreRankingTimedRefresh) {
    Config::C_Market config = GetMarketConfig();
    if (!config.StoreEnabled || config.StoreRankRefreshMinutes <= 0)
        GTEST_SKIP();

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    ASSERT_TRUE(sp);
    std::string uuidA = sp->getUuid().asString();

    ASSERT_TRUE(CreateStore(*sp));
    ASSERT_TRUE(GiveItem(*sp, "minecraft:grass_block", 1));
    ASSERT_TRUE(UploadStoreItem(*sp, "grass_block", 1));

    TestSimulatedPlayer sp2("test_player6");
    ASSERT_TRUE(sp2.create());
    std::string uuidB = sp2.getPlayer()->getUuid().asString();

    ASSERT_TRUE(CreateStore(*sp2.getPlayer()));
    ASSERT_TRUE(GiveItem(*sp2.getPlayer(), "minecraft:grass_block", 1));
    ASSERT_TRUE(UploadStoreItem(*sp2.getPlayer(), "grass_block", 1));

    auto db = MarketPlugin::getShared()->getDatabase();
    long long nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    std::string nowTime = SystemUtils::getNowTime();

    ASSERT_TRUE(db->exec("UPDATE Store SET store_created_at='" + FormatTimeDaysAgo(31) + "' WHERE key='" + uuidA + "';").has_value());
    ASSERT_TRUE(db->exec("UPDATE Store SET store_created_at='" + FormatTimeDaysAgo(30) + "' WHERE key='" + uuidB + "';").has_value());

    MarketPlugin::getShared()->clearStoreRankCache();

    MockExecutor executor;
    ASSERT_TRUE(MarketPlugin::getShared()->setExecutor(executor).has_value());
    MarketPlugin::getShared()->startStoreRankRefresh();

    auto rank1 = MarketPlugin::getShared()->getStoreRanking();
    ASSERT_TRUE(rank1.has_value());
    EXPECT_EQ(rank1.value().front(), uuidA);

    ASSERT_TRUE(db->exec("INSERT INTO StoreSale (key, store_id, item_name, price, buyer_uuid, buyer_name, time) VALUES ('" +
        std::to_string(nowNs + 1) + "', '" + uuidB + "', 'grass_block', '1000', 'buyer', 'buyer', '" + nowTime + "');").has_value());

    auto rank2 = MarketPlugin::getShared()->getStoreRanking();
    ASSERT_TRUE(rank2.has_value());
    EXPECT_EQ(rank2.value().front(), uuidA);

    executor.advanceTime(std::chrono::minutes(config.StoreRankRefreshMinutes + 1));

    auto rank3 = MarketPlugin::getShared()->getStoreRanking();
    ASSERT_TRUE(rank3.has_value());
    EXPECT_EQ(rank3.value().front(), uuidB);
}

// ---- 税（阶段 1）：纯函数，无需服务器环境 ----
TEST(MarketTaxTest, ComputeTaxFloor) {
    // 按价格 * 税率向下取整
    EXPECT_EQ(MarketPlugin::computeTax(1000, 0.05), 50);
    EXPECT_EQ(MarketPlugin::computeTax(999, 0.05), 49);
    EXPECT_EQ(MarketPlugin::computeTax(1234, 0.03), 37);
    EXPECT_EQ(MarketPlugin::computeTax(100000, 0.15), 15000);
}

TEST(MarketTaxTest, ComputeTaxEdgeCases) {
    // 零税率与零价格
    EXPECT_EQ(MarketPlugin::computeTax(1000, 0.0), 0);
    EXPECT_EQ(MarketPlugin::computeTax(0, 0.1), 0);
    // 小额成交不产生税
    EXPECT_EQ(MarketPlugin::computeTax(1, 0.99), 0);
    EXPECT_EQ(MarketPlugin::computeTax(100, 0.001), 0);
}

// ---- 反作弊（阶段 1）：离群价判定纯函数 ----
TEST(MarketQuoteOutlierTest, IsPriceOutlier) {
    // 区间内价格视为有效
    EXPECT_FALSE(MarketQuote::isPriceOutlier(100.0, 100, 2.0));
    EXPECT_FALSE(MarketQuote::isPriceOutlier(100.0, 199, 2.0));
    EXPECT_FALSE(MarketQuote::isPriceOutlier(100.0, 51, 2.0));
    // 超过均价 ratio 倍 / 低于均价的 1/ratio 视为离群
    EXPECT_TRUE(MarketQuote::isPriceOutlier(100.0, 201, 2.0));
    EXPECT_TRUE(MarketQuote::isPriceOutlier(100.0, 49, 2.0));
}

TEST(MarketQuoteOutlierTest, OutlierDisabled) {
    // ratio <= 0 或均价为 0 时关闭过滤
    EXPECT_FALSE(MarketQuote::isPriceOutlier(100.0, 100000, 0.0));
    EXPECT_FALSE(MarketQuote::isPriceOutlier(100.0, 100000, -1.0));
    EXPECT_FALSE(MarketQuote::isPriceOutlier(0.0, 100000, 2.0));
}

// ---- 行情（阶段 1）：成交后行情聚合、成交量排行、报表税收 ----
TEST_F(MarketPluginTest, StoreQuoteAggregation) {
    Config::C_Market config = GetMarketConfig();
    if (!config.StoreEnabled || !config.StoreQuoteEnabled)
        GTEST_SKIP();

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    ASSERT_TRUE(sp);

    TestSimulatedPlayer buyer("test_player6");
    ASSERT_TRUE(buyer.create());

    ASSERT_TRUE(CreateStore(*sp));
    ASSERT_TRUE(GiveItem(*sp, "minecraft:grass_block", 1));
    // 用唯一物品名隔离内存态行情统计（避免被其他用例的 grass_block 成交污染）
    ASSERT_TRUE(UploadStoreItem(*sp, "quote_probe", 100));

    auto items = MarketPlugin::getShared()->getStoreItems(sp->getUuid().asString());
    ASSERT_TRUE(items.has_value());
    ASSERT_FALSE(items.value().empty());

    bool created = false;
    PrepareScoreboard(config, created);
    ScoreboardUtils::setScore(*buyer.getPlayer(), config.TargetScoreboard, 1000);

    auto buy = MarketPlugin::getShared()->buyStoreItem(*buyer.getPlayer(), items.value().front());
    ASSERT_TRUE(buy.has_value());
    EXPECT_TRUE(buy.value());

    auto quote = MarketPlugin::getShared()->getQuote("quote_probe");
    ASSERT_TRUE(quote.has_value());
    ASSERT_TRUE(quote.value().has_value());
    EXPECT_EQ(quote.value()->count30d, 1);
    EXPECT_EQ(quote.value()->lastPrice, 100);
    EXPECT_EQ(quote.value()->avg30d, 100);
    EXPECT_EQ(quote.value()->max30d, 100);

    auto top = MarketPlugin::getShared()->getTopVolume(50, 30);
    ASSERT_TRUE(top.has_value());
    auto found = std::find_if(top.value().begin(), top.value().end(), [](const auto& entry) -> bool {
        return entry.first == "quote_probe";
    });
    ASSERT_NE(found, top.value().end());
    EXPECT_EQ(found->second, 1);

    // 报表为全局累计，做下限断言
    auto report = MarketPlugin::getShared()->getReport(30);
    ASSERT_TRUE(report.has_value());
    EXPECT_GE(report.value().count, 1);
    EXPECT_GE(report.value().turnover, 100);
    EXPECT_GE(report.value().tax, MarketPlugin::computeTax(100, config.StoreTransactionTaxRate));
    EXPECT_GE(report.value().activeSellers, 1);

    if (created)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

// ---- 求购（阶段 2）：创建冻结预付款 ----
TEST_F(MarketPluginTest, StoreWantedCreateFreeze) {
    Config::C_Market config = GetMarketConfig();
    if (!config.StoreEnabled || !config.StoreWantedEnabled)
        GTEST_SKIP();

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    ASSERT_TRUE(sp);

    bool created = false;
    PrepareScoreboard(config, created);

    ASSERT_TRUE(GiveItem(*sp, "minecraft:grass_block", 1));
    int slot = FindSlot(*sp, "minecraft:grass_block");
    ASSERT_GE(slot, 0);

    int unitPrice = 30;
    int amount = 5;
    int base = 10000;
    ScoreboardUtils::setScore(*sp, config.TargetScoreboard, base);

    auto create = MarketPlugin::getShared()->createWanted(*sp, slot, "Wanted Grass", unitPrice, amount);
    ASSERT_TRUE(create.has_value());
    EXPECT_TRUE(create.value());

    // 预付款冻结：base - unitPrice * amount
    EXPECT_EQ(ScoreboardUtils::getScore(*sp, config.TargetScoreboard), base - unitPrice * amount);

    auto list = MarketPlugin::getShared()->getWantedItems(*sp);
    ASSERT_TRUE(list.has_value());
    ASSERT_EQ(list.value().size(), 1);

    auto data = MarketPlugin::getShared()->getWantedData(list.value().front());
    ASSERT_TRUE(data.has_value());
    EXPECT_EQ(data.value().at("unit_price"), "30");
    EXPECT_EQ(data.value().at("amount_total"), "5");
    EXPECT_EQ(data.value().at("amount_filled"), "0");

    if (created)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

// ---- 求购（阶段 2）：供货成交，卖家实收扣税 ----
TEST_F(MarketPluginTest, StoreWantedFill) {
    Config::C_Market config = GetMarketConfig();
    if (!config.StoreEnabled || !config.StoreWantedEnabled)
        GTEST_SKIP();

    auto buyer = ll::service::getLevel()->getPlayer("test_player");
    ASSERT_TRUE(buyer);

    TestSimulatedPlayer seller("test_player6");
    ASSERT_TRUE(seller.create());

    bool created = false;
    PrepareScoreboard(config, created);

    InventoryUtils::clearItem(*buyer, "minecraft:grass_block", 2304);
    InventoryUtils::clearItem(*seller.getPlayer(), "minecraft:grass_block", 2304);

    ASSERT_TRUE(GiveItem(*buyer, "minecraft:grass_block", 1));
    int slot = FindSlot(*buyer, "minecraft:grass_block");
    ASSERT_GE(slot, 0);

    int unitPrice = 30;
    int amount = 5;
    ScoreboardUtils::setScore(*buyer, config.TargetScoreboard, 10000);
    ScoreboardUtils::setScore(*seller.getPlayer(), config.TargetScoreboard, 0);

    auto create = MarketPlugin::getShared()->createWanted(*buyer, slot, "Wanted Grass", unitPrice, amount);
    ASSERT_TRUE(create.has_value());
    EXPECT_TRUE(create.value());

    auto list = MarketPlugin::getShared()->getWantedItems(*buyer);
    ASSERT_TRUE(list.has_value());
    ASSERT_EQ(list.value().size(), 1);
    std::string id = list.value().front();

    ASSERT_TRUE(GiveItem(*seller.getPlayer(), "minecraft:grass_block", amount));

    auto fill = MarketPlugin::getShared()->fillWanted(*seller.getPlayer(), id, amount);
    ASSERT_TRUE(fill.has_value());
    EXPECT_TRUE(fill.value());

    // 卖家实收 = 单价 * 数量 - 税；物品从卖家转到买家；求购单已满删除
    int pay = unitPrice * amount;
    int tax = MarketPlugin::computeTax(pay, config.StoreTransactionTaxRate);
    EXPECT_EQ(ScoreboardUtils::getScore(*seller.getPlayer(), config.TargetScoreboard), pay - tax);
    EXPECT_FALSE(InventoryUtils::isItemInInventory(*seller.getPlayer(), "minecraft:grass_block", 1));
    EXPECT_TRUE(InventoryUtils::isItemInInventory(*buyer, "minecraft:grass_block", amount));

    auto has = MarketPlugin::getShared()->getDatabase()->has("StoreWanted", id);
    ASSERT_TRUE(has.has_value());
    EXPECT_FALSE(has.value());

    if (created)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

// ---- 求购（阶段 2）：反作弊——买家不能给自己供货 ----
TEST_F(MarketPluginTest, StoreWantedSelfFillBlocked) {
    Config::C_Market config = GetMarketConfig();
    if (!config.StoreEnabled || !config.StoreWantedEnabled)
        GTEST_SKIP();

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    ASSERT_TRUE(sp);

    bool created = false;
    PrepareScoreboard(config, created);

    ASSERT_TRUE(GiveItem(*sp, "minecraft:grass_block", 1));
    int slot = FindSlot(*sp, "minecraft:grass_block");
    ASSERT_GE(slot, 0);

    ScoreboardUtils::setScore(*sp, config.TargetScoreboard, 10000);

    auto create = MarketPlugin::getShared()->createWanted(*sp, slot, "Wanted Grass", 30, 5);
    ASSERT_TRUE(create.has_value());
    EXPECT_TRUE(create.value());

    auto list = MarketPlugin::getShared()->getWantedItems(*sp);
    ASSERT_TRUE(list.has_value());
    ASSERT_EQ(list.value().size(), 1);

    auto fill = MarketPlugin::getShared()->fillWanted(*sp, list.value().front(), 1);
    ASSERT_TRUE(fill.has_value());
    EXPECT_FALSE(fill.value());

    if (created)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

// ---- 求购（阶段 2）：取消退还剩余冻结资金 ----
TEST_F(MarketPluginTest, StoreWantedCancelRefund) {
    Config::C_Market config = GetMarketConfig();
    if (!config.StoreEnabled || !config.StoreWantedEnabled)
        GTEST_SKIP();

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    ASSERT_TRUE(sp);

    bool created = false;
    PrepareScoreboard(config, created);

    ASSERT_TRUE(GiveItem(*sp, "minecraft:grass_block", 1));
    int slot = FindSlot(*sp, "minecraft:grass_block");
    ASSERT_GE(slot, 0);

    int unitPrice = 30;
    int amount = 5;
    int base = 10000;
    ScoreboardUtils::setScore(*sp, config.TargetScoreboard, base);

    auto create = MarketPlugin::getShared()->createWanted(*sp, slot, "Wanted Grass", unitPrice, amount);
    ASSERT_TRUE(create.has_value());
    EXPECT_TRUE(create.value());

    auto list = MarketPlugin::getShared()->getWantedItems(*sp);
    ASSERT_TRUE(list.has_value());
    ASSERT_EQ(list.value().size(), 1);

    auto cancel = MarketPlugin::getShared()->cancelWanted(*sp, list.value().front());
    ASSERT_TRUE(cancel.has_value());
    EXPECT_TRUE(cancel.value());

    // 未成交全额退还
    EXPECT_EQ(ScoreboardUtils::getScore(*sp, config.TargetScoreboard), base);

    if (created)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

// ---- 拍卖（阶段 3）：上架扣物品 + 出价冻结全款 ----
TEST_F(MarketPluginTest, StoreAuctionCreateBid) {
    Config::C_Market config = GetMarketConfig();
    if (!config.StoreEnabled || !config.StoreAuctionEnabled)
        GTEST_SKIP();

    auto seller = ll::service::getLevel()->getPlayer("test_player");
    ASSERT_TRUE(seller);

    TestSimulatedPlayer bidder("test_player6");
    ASSERT_TRUE(bidder.create());

    bool created = false;
    PrepareScoreboard(config, created);

    InventoryUtils::clearItem(*seller, "minecraft:grass_block", 2304);

    ASSERT_TRUE(GiveItem(*seller, "minecraft:grass_block", 1));
    int slot = FindSlot(*seller, "minecraft:grass_block");
    ASSERT_GE(slot, 0);

    int duration = std::max(30 * 60, config.StoreAuctionMinDurationMinutes * 60);
    auto create = MarketPlugin::getShared()->createAuction(*seller, slot, "Auction Grass", 100, duration);
    ASSERT_TRUE(create.has_value());
    EXPECT_TRUE(create.value());
    EXPECT_FALSE(InventoryUtils::isItemInInventory(*seller, "minecraft:grass_block", 1));

    auto list = MarketPlugin::getShared()->getAuctionList();
    ASSERT_TRUE(list.has_value());
    ASSERT_EQ(list.value().size(), 1);
    std::string id = list.value().front();

    // 出价即冻结全款
    int base = 10000;
    ScoreboardUtils::setScore(*bidder.getPlayer(), config.TargetScoreboard, base);

    auto bid = MarketPlugin::getShared()->bidAuction(*bidder.getPlayer(), id, 150);
    ASSERT_TRUE(bid.has_value());
    EXPECT_TRUE(bid.value());
    EXPECT_EQ(ScoreboardUtils::getScore(*bidder.getPlayer(), config.TargetScoreboard), base - 150);

    auto data = MarketPlugin::getShared()->getAuctionData(id);
    ASSERT_TRUE(data.has_value());
    EXPECT_EQ(data.value().at("current_price"), "150");
    EXPECT_EQ(data.value().at("bidder_uuid"), bidder.getPlayer()->getUuid().asString());

    if (created)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

// ---- 拍卖（阶段 3）：反作弊——卖家不能出价自己的拍卖 ----
TEST_F(MarketPluginTest, StoreAuctionSelfBidBlocked) {
    Config::C_Market config = GetMarketConfig();
    if (!config.StoreEnabled || !config.StoreAuctionEnabled)
        GTEST_SKIP();

    auto seller = ll::service::getLevel()->getPlayer("test_player");
    ASSERT_TRUE(seller);

    bool created = false;
    PrepareScoreboard(config, created);

    InventoryUtils::clearItem(*seller, "minecraft:grass_block", 2304);
    ASSERT_TRUE(GiveItem(*seller, "minecraft:grass_block", 1));
    int slot = FindSlot(*seller, "minecraft:grass_block");
    ASSERT_GE(slot, 0);

    int duration = std::max(30 * 60, config.StoreAuctionMinDurationMinutes * 60);
    auto create = MarketPlugin::getShared()->createAuction(*seller, slot, "Auction Grass", 100, duration);
    ASSERT_TRUE(create.has_value());
    EXPECT_TRUE(create.value());

    auto list = MarketPlugin::getShared()->getAuctionList();
    ASSERT_TRUE(list.has_value());
    ASSERT_EQ(list.value().size(), 1);

    auto bid = MarketPlugin::getShared()->bidAuction(*seller, list.value().front(), 200);
    ASSERT_TRUE(bid.has_value());
    EXPECT_FALSE(bid.value());

    if (created)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

// ---- 拍卖（阶段 3）：加价校验 + 被超越自动退款 ----
TEST_F(MarketPluginTest, StoreAuctionOutbidRefund) {
    Config::C_Market config = GetMarketConfig();
    if (!config.StoreEnabled || !config.StoreAuctionEnabled)
        GTEST_SKIP();

    auto seller = ll::service::getLevel()->getPlayer("test_player");
    ASSERT_TRUE(seller);

    TestSimulatedPlayer bidder1("test_player6");
    TestSimulatedPlayer bidder2("test_player7");
    ASSERT_TRUE(bidder1.create());
    ASSERT_TRUE(bidder2.create());

    bool created = false;
    PrepareScoreboard(config, created);

    InventoryUtils::clearItem(*seller, "minecraft:grass_block", 2304);
    ASSERT_TRUE(GiveItem(*seller, "minecraft:grass_block", 1));
    int slot = FindSlot(*seller, "minecraft:grass_block");
    ASSERT_GE(slot, 0);

    int duration = std::max(30 * 60, config.StoreAuctionMinDurationMinutes * 60);
    auto create = MarketPlugin::getShared()->createAuction(*seller, slot, "Auction Grass", 100, duration);
    ASSERT_TRUE(create.has_value());
    EXPECT_TRUE(create.value());

    auto list = MarketPlugin::getShared()->getAuctionList();
    ASSERT_TRUE(list.has_value());
    ASSERT_EQ(list.value().size(), 1);
    std::string id = list.value().front();

    int base = 10000;
    ScoreboardUtils::setScore(*bidder1.getPlayer(), config.TargetScoreboard, base);
    ScoreboardUtils::setScore(*bidder2.getPlayer(), config.TargetScoreboard, base);

    auto bid1 = MarketPlugin::getShared()->bidAuction(*bidder1.getPlayer(), id, 100);
    ASSERT_TRUE(bid1.has_value());
    EXPECT_TRUE(bid1.value());
    EXPECT_EQ(ScoreboardUtils::getScore(*bidder1.getPlayer(), config.TargetScoreboard), base - 100);

    // 出价低于最低加价（当前价 * 增量比例）：返回错误而非成交
    auto low = MarketPlugin::getShared()->bidAuction(*bidder2.getPlayer(), id, 100);
    EXPECT_FALSE(low.has_value());

    // 满足最低加价：成交并退回首位出价者
    int minBid = static_cast<int>(std::ceil(100.0 * config.StoreAuctionMinBidIncrement));
    auto bid2 = MarketPlugin::getShared()->bidAuction(*bidder2.getPlayer(), id, minBid);
    ASSERT_TRUE(bid2.has_value());
    EXPECT_TRUE(bid2.value());

    EXPECT_EQ(ScoreboardUtils::getScore(*bidder1.getPlayer(), config.TargetScoreboard), base);
    EXPECT_EQ(ScoreboardUtils::getScore(*bidder2.getPlayer(), config.TargetScoreboard), base - minBid);

    if (created)
        ScoreboardUtils::remove(config.TargetScoreboard);
}
