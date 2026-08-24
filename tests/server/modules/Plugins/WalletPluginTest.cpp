#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <string>
#include <algorithm>

#include <ll/api/service/Bedrock.h>
#include <ll/api/thread/ServerThreadExecutor.h>

#include <mc/world/level/Level.h>
#include <mc/world/actor/player/Player.h>

#include <mc/server/SimulatedPlayer.h>

#include "LOICollectionA/data/SQLiteStorage.h"

#include "LOICollectionA/utils/mc-server/ScoreboardUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/base/Wrapper.h"
#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/WalletPlugin.h"

#include "common/coro/MockExecutor.h"
#include "server/TestSimulatedPlayer.h"

using namespace LOICollection::server::Plugins;

class WalletPluginTest : public testing::Test {
protected:
    void SetUp() override {
        if (!WalletPlugin::getShared()->isValid())
            GTEST_SKIP() << "WalletPlugin is not valid";
    }

    void TearDown() override {
        auto storage = ServiceProvider::getInstance().getService<SQLiteStorage>("SettingsDB");

        auto result = storage->exec("DELETE FROM Wallet; DELETE FROM RedEnvelope; DELETE FROM RedEnvelopeGrab; DELETE FROM WalletFee; DELETE FROM WalletLedger;");
        if (!result.has_value())
            GTEST_FAIL() << "Unable to clear data";

        EXPECT_TRUE(WalletPlugin::getShared()->setExecutor(ll::thread::ServerThreadExecutor::getDefault()).has_value());
    }
};

TEST_F(WalletPluginTest, GetPlayerInfo) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    auto info = WalletPlugin::getShared()->getPlayerInfo(sp->getUuid().asString());
    EXPECT_TRUE(info.has_value());
    EXPECT_EQ(info.value(), "Unknown");

    auto data = WalletPlugin::getShared()->getPlayerInfo();
    EXPECT_TRUE(data.has_value());
    EXPECT_TRUE(std::find(data.value().begin(), data.value().end(), std::make_pair(sp->getUuid().asString(), sp->getRealName())) == data.value().end());
}

TEST_F(WalletPluginTest, ForTransfer) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    TestSimulatedPlayer sp2("test_player4");
    EXPECT_TRUE(sp2.create());

    Config::C_Wallet config = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Wallet;

    bool hasScoreboard = true;
    if (!ScoreboardUtils::hasScoreboard(config.TargetScoreboard)) {
        hasScoreboard = false;

        ScoreboardUtils::create(config.TargetScoreboard);
    }

    auto transfer1 = WalletPlugin::getShared()->forTransfer(*sp, "nonexistent_target", "test_name", 100);
    EXPECT_TRUE(transfer1.has_value());
    EXPECT_FALSE(transfer1.value());

    ScoreboardUtils::setScore(*sp, config.TargetScoreboard, 100);

    auto transfer2 = WalletPlugin::getShared()->forTransfer(*sp, sp2.getPlayer()->getUuid().asString(), sp2.getPlayer()->getRealName(), 100);
    EXPECT_TRUE(transfer2.has_value());
    EXPECT_TRUE(transfer2.value());

    EXPECT_EQ(ScoreboardUtils::getScore(*sp, config.TargetScoreboard), 0);
    EXPECT_EQ(ScoreboardUtils::getScore(*sp2.getPlayer(), config.TargetScoreboard), 100 * (1 - config.ExchangeRate));

    if (!hasScoreboard)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

TEST_F(WalletPluginTest, Wealth) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    EXPECT_TRUE(WalletPlugin::getShared()->wealth(*sp).has_value());
}

TEST_F(WalletPluginTest, RedenvelopeTimeout) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    Config::C_Wallet config = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Wallet;

    bool hasScoreboard = true;
    if (!ScoreboardUtils::hasScoreboard(config.TargetScoreboard)) {
        hasScoreboard = false;

        ScoreboardUtils::create(config.TargetScoreboard);
    }

    ScoreboardUtils::setScore(*sp, config.TargetScoreboard, 500);

    MockExecutor executor;

    EXPECT_TRUE(WalletPlugin::getShared()->setExecutor(executor).has_value());
    EXPECT_TRUE(WalletPlugin::getShared()->redenvelope(*sp, "test_key", 100, 5).has_value());

    EXPECT_EQ(ScoreboardUtils::getScore(*sp, config.TargetScoreboard), 0);

    executor.advanceTime(std::chrono::seconds(config.RedEnvelopeTimeout + 1));

    EXPECT_EQ(ScoreboardUtils::getScore(*sp, config.TargetScoreboard), 500);

    if (!hasScoreboard)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

TEST_F(WalletPluginTest, RedenvelopeReceive) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    TestSimulatedPlayer sp2("test_player4");
    EXPECT_TRUE(sp2.create());

    Config::C_Wallet config = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Wallet;

    bool hasScoreboard = true;
    if (!ScoreboardUtils::hasScoreboard(config.TargetScoreboard)) {
        hasScoreboard = false;

        ScoreboardUtils::create(config.TargetScoreboard);
    }

    ScoreboardUtils::setScore(*sp, config.TargetScoreboard, 500);

    MockExecutor executor;

    EXPECT_TRUE(WalletPlugin::getShared()->setExecutor(executor).has_value());
    EXPECT_TRUE(WalletPlugin::getShared()->redenvelope(*sp, "test_key", 100, 5).has_value());

    EXPECT_EQ(ScoreboardUtils::getScore(*sp, config.TargetScoreboard), 0);

    EXPECT_TRUE(WalletPlugin::getShared()->tryGrabRedEnvelope(*sp2.getPlayer(), "test_key").has_value());

    executor.advanceTime(std::chrono::seconds(config.RedEnvelopeTimeout + 1));

    EXPECT_TRUE(ScoreboardUtils::getScore(*sp, config.TargetScoreboard) < 500);
    EXPECT_TRUE(ScoreboardUtils::getScore(*sp2.getPlayer(), config.TargetScoreboard) > 0);

    if (!hasScoreboard)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

TEST_F(WalletPluginTest, RedenvelopeReceiveOver) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    TestSimulatedPlayer sp2("test_player4");
    EXPECT_TRUE(sp2.create());

    TestSimulatedPlayer sp3("test_player5");
    EXPECT_TRUE(sp3.create());

    Config::C_Wallet config = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Wallet;

    bool hasScoreboard = true;
    if (!ScoreboardUtils::hasScoreboard(config.TargetScoreboard)) {
        hasScoreboard = false;

        ScoreboardUtils::create(config.TargetScoreboard);
    }

    ScoreboardUtils::setScore(*sp, config.TargetScoreboard, 200);

    MockExecutor executor;

    EXPECT_TRUE(WalletPlugin::getShared()->setExecutor(executor).has_value());
    EXPECT_TRUE(WalletPlugin::getShared()->redenvelope(*sp, "test_key", 100, 2).has_value());

    EXPECT_EQ(ScoreboardUtils::getScore(*sp, config.TargetScoreboard), 0);

    EXPECT_TRUE(WalletPlugin::getShared()->tryGrabRedEnvelope(*sp2.getPlayer(), "test_key").has_value());
    EXPECT_TRUE(WalletPlugin::getShared()->tryGrabRedEnvelope(*sp3.getPlayer(), "test_key").has_value());

    executor.advanceTime(std::chrono::seconds(config.RedEnvelopeTimeout + 1));

    EXPECT_EQ(ScoreboardUtils::getScore(*sp, config.TargetScoreboard), 0);
    EXPECT_TRUE(ScoreboardUtils::getScore(*sp2.getPlayer(), config.TargetScoreboard) > 0);
    EXPECT_TRUE(ScoreboardUtils::getScore(*sp3.getPlayer(), config.TargetScoreboard) > 0);

    if (!hasScoreboard)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

TEST_F(WalletPluginTest, GiftAmountConservation) {
    for (int round = 0; round < 1000; ++round) {
        int count = 1 + static_cast<int>(std::rand() % 20);
        int capacity = count + static_cast<int>(std::rand() % 200);

        int remainingCapacity = capacity;
        int remainingPeople = count;
        long long sum = 0;

        for (int i = 0; i < count; ++i) {
            bool last = (i == count - 1);
            int amount = last ? remainingCapacity : WalletPlugin::computeGiftAmount(remainingCapacity, remainingPeople);

            EXPECT_GE(amount, 1);

            sum += amount;
            remainingCapacity -= amount;
            remainingPeople -= 1;
        }

        EXPECT_EQ(sum, capacity);
    }
}

TEST_F(WalletPluginTest, GiftAmountPoisonRange) {
    EXPECT_EQ(WalletPlugin::computeGiftAmount(0, 3), 0);
    EXPECT_GE(WalletPlugin::computeGiftAmount(1, 5), 1);
    EXPECT_GE(WalletPlugin::computeGiftAmount(3, 10), 1);
}

TEST_F(WalletPluginTest, RedenvelopePersistRow) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    auto storage = ServiceProvider::getInstance().getService<SQLiteStorage>("SettingsDB");

    Config::C_Wallet config = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Wallet;

    bool hasScoreboard = true;
    if (!ScoreboardUtils::hasScoreboard(config.TargetScoreboard)) {
        hasScoreboard = false;

        ScoreboardUtils::create(config.TargetScoreboard);
    }

    ScoreboardUtils::setScore(*sp, config.TargetScoreboard, 500);

    MockExecutor executor;

    EXPECT_TRUE(WalletPlugin::getShared()->setExecutor(executor).has_value());
    EXPECT_TRUE(WalletPlugin::getShared()->redenvelope(*sp, "test_key", 100, 5).has_value());

    auto ids = storage->find("RedEnvelope", std::vector<std::pair<std::string, std::string>>{ { "chat_key", "test_key" } });
    EXPECT_TRUE(ids.has_value());
    ASSERT_EQ(ids.value().size(), 1u);

    EXPECT_EQ(storage->get("RedEnvelope", ids.value().at(0), "capacity", "0").value(), "500");
    EXPECT_EQ(storage->get("RedEnvelope", ids.value().at(0), "count", "0").value(), "5");
    EXPECT_EQ(storage->get("RedEnvelope", ids.value().at(0), "people", "0").value(), "0");

    if (!hasScoreboard)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

TEST_F(WalletPluginTest, RedenvelopeCrashRecovery) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    auto storage = ServiceProvider::getInstance().getService<SQLiteStorage>("SettingsDB");

    Config::C_Wallet config = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Wallet;

    bool hasScoreboard = true;
    if (!ScoreboardUtils::hasScoreboard(config.TargetScoreboard)) {
        hasScoreboard = false;

        ScoreboardUtils::create(config.TargetScoreboard);
    }

    long long past = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count() - 60000000000LL;

    std::unordered_map<std::string, std::string> env = {
        { "chat_key", "test_key" },
        { "sender_uuid", sp->getUuid().asString() },
        { "sender_name", sp->getRealName() },
        { "capacity", "300" },
        { "count", "3" },
        { "people", "0" },
        { "created_at", SystemUtils::getNowTime() },
        { "expire_at", std::to_string(past) }
    };

    ScoreboardUtils::setScore(*sp, config.TargetScoreboard, 0);
    EXPECT_TRUE(storage->set("RedEnvelope", "crash_id", env).has_value());

    EXPECT_TRUE(WalletPlugin::getShared()->sweepExpiredEnvelopes().has_value());

    EXPECT_EQ(ScoreboardUtils::getScore(*sp, config.TargetScoreboard), 300);
    EXPECT_FALSE(storage->has("RedEnvelope", "crash_id").value());

    if (!hasScoreboard)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

TEST_F(WalletPluginTest, FeePoolAccumulates) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    TestSimulatedPlayer sp2("test_player4");
    EXPECT_TRUE(sp2.create());

    Config::C_Wallet config = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Wallet;

    bool hasScoreboard = true;
    if (!ScoreboardUtils::hasScoreboard(config.TargetScoreboard)) {
        hasScoreboard = false;

        ScoreboardUtils::create(config.TargetScoreboard);
    }

    ScoreboardUtils::setScore(*sp, config.TargetScoreboard, 500);

    auto before = WalletPlugin::getShared()->getFeePool();
    ASSERT_TRUE(before.has_value());

    EXPECT_TRUE(WalletPlugin::getShared()->forTransfer(*sp, sp2.getPlayer()->getUuid().asString(), sp2.getPlayer()->getRealName(), 200).has_value());

    auto after = WalletPlugin::getShared()->getFeePool();
    ASSERT_TRUE(after.has_value());

    int fee = 200 - static_cast<int>(200 * (1 - config.ExchangeRate));
    EXPECT_EQ(after.value() - before.value(), fee);

    if (!hasScoreboard)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

TEST_F(WalletPluginTest, LedgerRecordsTransfer) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    TestSimulatedPlayer sp2("test_player4");
    EXPECT_TRUE(sp2.create());

    auto storage = ServiceProvider::getInstance().getService<SQLiteStorage>("SettingsDB");

    Config::C_Wallet config = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Wallet;

    bool hasScoreboard = true;
    if (!ScoreboardUtils::hasScoreboard(config.TargetScoreboard)) {
        hasScoreboard = false;

        ScoreboardUtils::create(config.TargetScoreboard);
    }

    ScoreboardUtils::setScore(*sp, config.TargetScoreboard, 500);

    EXPECT_TRUE(WalletPlugin::getShared()->forTransfer(*sp, sp2.getPlayer()->getUuid().asString(), sp2.getPlayer()->getRealName(), 200).has_value());

    auto ids = storage->find("WalletLedger", std::vector<std::pair<std::string, std::string>>{ { "from_uuid", sp->getUuid().asString() } });
    EXPECT_TRUE(ids.has_value());
    ASSERT_FALSE(ids.value().empty());

    auto row = storage->get("WalletLedger", ids.value().at(0)).value();
    EXPECT_EQ(row["type"], "transfer");
    EXPECT_EQ(row["to_uuid"], sp2.getPlayer()->getUuid().asString());
    EXPECT_EQ(SystemUtils::toInt(row["amount"], 0) + SystemUtils::toInt(row["fee"], 0), 200);

    if (!hasScoreboard)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

TEST_F(WalletPluginTest, LedgerHistoryVisibility) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    auto storage = ServiceProvider::getInstance().getService<SQLiteStorage>("SettingsDB");

    Config::C_Wallet config = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Wallet;

    bool hasScoreboard = true;
    if (!ScoreboardUtils::hasScoreboard(config.TargetScoreboard)) {
        hasScoreboard = false;

        ScoreboardUtils::create(config.TargetScoreboard);
    }

    ScoreboardUtils::setScore(*sp, config.TargetScoreboard, 500);

    EXPECT_TRUE(WalletPlugin::getShared()->forTransfer(*sp, sp->getUuid().asString(), sp->getRealName(), 100).has_value());

    // The query path must only surface rows where the player is a participant.
    auto ids = storage->find("WalletLedger", {
        { "from_uuid", sp->getUuid().asString() },
        { "to_uuid", sp->getUuid().asString() }
    }, SQLiteStorage::FindCondition::OR);
    EXPECT_TRUE(ids.has_value());
    ASSERT_FALSE(ids.value().empty());

    if (!hasScoreboard)
        ScoreboardUtils::remove(config.TargetScoreboard);
}
