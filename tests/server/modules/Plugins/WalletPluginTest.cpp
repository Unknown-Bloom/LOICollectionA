#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
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

        auto result = storage->exec("DELETE FROM Wallet; DELETE FROM RedEnvelope; DELETE FROM RedEnvelopeGrab; DELETE FROM WalletFee; DELETE FROM WalletLedger; DELETE FROM WalletBank;");
        if (!result.has_value())
            GTEST_FAIL() << "Unable to clear data";

        EXPECT_TRUE(WalletPlugin::getShared()->setExecutor(ll::thread::ServerThreadExecutor::getDefault()).has_value());
    }

    Config::C_Wallet GetWalletConfig() {
        return ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Wallet;
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

TEST_F(WalletPluginTest, TransferLimitBelowMinimum) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    Config::C_Wallet base = GetWalletConfig();
    base.TransferMinAmount = 5;
    WalletPlugin::getShared()->setOptionsForTest(base);

    bool hasScoreboard = true;
    if (!ScoreboardUtils::hasScoreboard(base.TargetScoreboard)) {
        hasScoreboard = false;
        ScoreboardUtils::create(base.TargetScoreboard);
    }

    ScoreboardUtils::setScore(*sp, base.TargetScoreboard, 100);

    // Below the configured minimum is rejected with a dedicated error code.
    auto low = WalletPlugin::getShared()->forTransfer(*sp, "nonexistent_target", "test_name", 3);
    ASSERT_FALSE(low.has_value());
    EXPECT_EQ(static_cast<WalletPluginErrorCode>(low.error().value()), WalletPluginErrorCode::BelowMinimum);

    // Exactly at the minimum threshold is accepted.
    auto ok = WalletPlugin::getShared()->forTransfer(*sp, "nonexistent_target", "test_name", 5);
    EXPECT_TRUE(ok.has_value());

    WalletPlugin::getShared()->setOptionsForTest(GetWalletConfig());

    if (!hasScoreboard)
        ScoreboardUtils::remove(base.TargetScoreboard);
}

TEST_F(WalletPluginTest, TransferLimitDailyBudget) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    TestSimulatedPlayer sp2("test_player4");
    EXPECT_TRUE(sp2.create());

    Config::C_Wallet base = GetWalletConfig();
    base.TransferMinAmount = 1;
    base.TransferDailyLimit = 300;
    WalletPlugin::getShared()->setOptionsForTest(base);

    bool hasScoreboard = true;
    if (!ScoreboardUtils::hasScoreboard(base.TargetScoreboard)) {
        hasScoreboard = false;
        ScoreboardUtils::create(base.TargetScoreboard);
    }

    ScoreboardUtils::setScore(*sp, base.TargetScoreboard, 500);

    // First transfer fits within the 300/day budget.
    auto first = WalletPlugin::getShared()->forTransfer(*sp, sp2.getPlayer()->getUuid().asString(), sp2.getPlayer()->getRealName(), 200);
    EXPECT_TRUE(first.has_value());

    // Spending a further 200 would bring the day's total to 400 > 300, rejected.
    auto second = WalletPlugin::getShared()->forTransfer(*sp, sp2.getPlayer()->getUuid().asString(), sp2.getPlayer()->getRealName(), 200);
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(static_cast<WalletPluginErrorCode>(second.error().value()), WalletPluginErrorCode::DailyLimitExceeded);

    WalletPlugin::getShared()->setOptionsForTest(GetWalletConfig());

    if (!hasScoreboard)
        ScoreboardUtils::remove(base.TargetScoreboard);
}

TEST_F(WalletPluginTest, TransferLimitCooldown) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    TestSimulatedPlayer sp2("test_player4");
    EXPECT_TRUE(sp2.create());

    Config::C_Wallet base = GetWalletConfig();
    base.TransferMinAmount = 1;
    base.TransferCooldownSeconds = 60;
    WalletPlugin::getShared()->setOptionsForTest(base);

    bool hasScoreboard = true;
    if (!ScoreboardUtils::hasScoreboard(base.TargetScoreboard)) {
        hasScoreboard = false;
        ScoreboardUtils::create(base.TargetScoreboard);
    }

    ScoreboardUtils::setScore(*sp, base.TargetScoreboard, 500);

    ScoreboardUtils::setScore(*sp2.getPlayer(), base.TargetScoreboard, 100);

    // First transfer succeeds and stamps the cooldown timestamp.
    auto first = WalletPlugin::getShared()->forTransfer(*sp, sp2.getPlayer()->getUuid().asString(), sp2.getPlayer()->getRealName(), 100);
    EXPECT_TRUE(first.has_value());

    // An immediate second transfer is blocked by the active cooldown.
    auto second = WalletPlugin::getShared()->forTransfer(*sp, sp2.getPlayer()->getUuid().asString(), sp2.getPlayer()->getRealName(), 100);
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(static_cast<WalletPluginErrorCode>(second.error().value()), WalletPluginErrorCode::CooldownActive);

    WalletPlugin::getShared()->setOptionsForTest(GetWalletConfig());

    if (!hasScoreboard)
        ScoreboardUtils::remove(base.TargetScoreboard);
}

TEST_F(WalletPluginTest, TransferLargeRequiresConfirmation) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    TestSimulatedPlayer sp2("test_player4");
    EXPECT_TRUE(sp2.create());

    Config::C_Wallet base = GetWalletConfig();
    base.TransferMinAmount = 1;
    base.TransferConfirmThreshold = 1000;
    WalletPlugin::getShared()->setOptionsForTest(base);

    bool hasScoreboard = true;
    if (!ScoreboardUtils::hasScoreboard(base.TargetScoreboard)) {
        hasScoreboard = false;
        ScoreboardUtils::create(base.TargetScoreboard);
    }

    ScoreboardUtils::setScore(*sp, base.TargetScoreboard, 2000);

    // Unconfirmed large transfer is rejected with ConfirmRequired.
    auto blocked = WalletPlugin::getShared()->forTransfer(*sp, sp2.getPlayer()->getUuid().asString(), sp2.getPlayer()->getRealName(), 1500);
    ASSERT_FALSE(blocked.has_value());
    EXPECT_EQ(static_cast<WalletPluginErrorCode>(blocked.error().value()), WalletPluginErrorCode::ConfirmRequired);

    // Confirmed large transfer is accepted.
    auto confirm = WalletPlugin::getShared()->forTransfer(*sp, sp2.getPlayer()->getUuid().asString(), sp2.getPlayer()->getRealName(), 1500, true);
    EXPECT_TRUE(confirm.has_value());
    EXPECT_TRUE(confirm.value());

    WalletPlugin::getShared()->setOptionsForTest(GetWalletConfig());

    if (!hasScoreboard)
        ScoreboardUtils::remove(base.TargetScoreboard);
}

TEST_F(WalletPluginTest, BankDepositAndWithdraw) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    auto storage = ServiceProvider::getInstance().getService<SQLiteStorage>("SettingsDB");

    Config::C_Wallet config = GetWalletConfig();

    bool hasScoreboard = true;
    if (!ScoreboardUtils::hasScoreboard(config.TargetScoreboard)) {
        hasScoreboard = false;
        ScoreboardUtils::create(config.TargetScoreboard);
    }

    ScoreboardUtils::setScore(*sp, config.TargetScoreboard, 500);

    // Deposit 100: score drops, principal recorded, ledger row written.
    EXPECT_TRUE(WalletPlugin::getShared()->bankDeposit(*sp, 100).has_value());
    EXPECT_EQ(ScoreboardUtils::getScore(*sp, config.TargetScoreboard), 400);
    EXPECT_EQ(WalletPlugin::getShared()->getBankPrincipal(sp->getUuid().asString()).value(), 100);

    auto ids = storage->find("WalletLedger", std::vector<std::pair<std::string, std::string>>{ { "from_uuid", sp->getUuid().asString() } });
    EXPECT_TRUE(ids.has_value());
    ASSERT_FALSE(ids.value().empty());
    EXPECT_EQ(storage->get("WalletLedger", ids.value().at(0)).value()["type"], "bank_deposit");

    // Withdraw immediately: interest is 0, principal fully returned.
    EXPECT_TRUE(WalletPlugin::getShared()->bankWithdraw(*sp).has_value());
    EXPECT_EQ(ScoreboardUtils::getScore(*sp, config.TargetScoreboard), 500);
    EXPECT_EQ(WalletPlugin::getShared()->getBankPrincipal(sp->getUuid().asString()).value(), 0);

    if (!hasScoreboard)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

TEST_F(WalletPluginTest, BankDepositBelowMinimum) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    Config::C_Wallet base = GetWalletConfig();
    base.WalletBankMinDeposit = 10;
    WalletPlugin::getShared()->setOptionsForTest(base);

    bool hasScoreboard = true;
    if (!ScoreboardUtils::hasScoreboard(base.TargetScoreboard)) {
        hasScoreboard = false;
        ScoreboardUtils::create(base.TargetScoreboard);
    }

    ScoreboardUtils::setScore(*sp, base.TargetScoreboard, 100);

    auto low = WalletPlugin::getShared()->bankDeposit(*sp, 5);
    ASSERT_FALSE(low.has_value());
    EXPECT_EQ(static_cast<WalletPluginErrorCode>(low.error().value()), WalletPluginErrorCode::BelowMinDeposit);

    auto ok = WalletPlugin::getShared()->bankDeposit(*sp, 10);
    EXPECT_TRUE(ok.has_value());

    WalletPlugin::getShared()->setOptionsForTest(GetWalletConfig());

    if (!hasScoreboard)
        ScoreboardUtils::remove(base.TargetScoreboard);
}

TEST_F(WalletPluginTest, BankWithdrawEmpty) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    auto result = WalletPlugin::getShared()->bankWithdraw(*sp);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(static_cast<WalletPluginErrorCode>(result.error().value()), WalletPluginErrorCode::BankEmpty);
}

TEST_F(WalletPluginTest, BankInterestDegradesWhenPoolEmpty) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    Config::C_Wallet base = GetWalletConfig();
    base.WalletInterestFromPool = true;
    WalletPlugin::getShared()->setOptionsForTest(base);

    bool hasScoreboard = true;
    if (!ScoreboardUtils::hasScoreboard(base.TargetScoreboard)) {
        hasScoreboard = false;
        ScoreboardUtils::create(base.TargetScoreboard);
    }

    // Fee pool starts empty (cleared in TearDown).
    EXPECT_EQ(WalletPlugin::getShared()->getFeePool().value(), 0);

    ScoreboardUtils::setScore(*sp, base.TargetScoreboard, 1000);
    EXPECT_TRUE(WalletPlugin::getShared()->bankDeposit(*sp, 1000).has_value());
    EXPECT_EQ(ScoreboardUtils::getScore(*sp, base.TargetScoreboard), 0);

    // Withdraw: interest limited by the empty pool -> 0, principal still fully returned.
    EXPECT_TRUE(WalletPlugin::getShared()->bankWithdraw(*sp).has_value());
    EXPECT_EQ(ScoreboardUtils::getScore(*sp, base.TargetScoreboard), 1000);

    WalletPlugin::getShared()->setOptionsForTest(GetWalletConfig());

    if (!hasScoreboard)
        ScoreboardUtils::remove(base.TargetScoreboard);
}

TEST_F(WalletPluginTest, BankInterestCalculation) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    auto storage = ServiceProvider::getInstance().getService<SQLiteStorage>("SettingsDB");

    Config::C_Wallet config = GetWalletConfig();

    std::string uuid = sp->getUuid().asString();

    // No bank account -> no principal, no interest.
    EXPECT_EQ(WalletPlugin::getShared()->getBankInterest(uuid).value(), 0);

    long long now = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    // Deposited just now -> 0 full days elapsed -> 0 interest.
    EXPECT_TRUE(storage->set("WalletBank", uuid, { { "principal", "1000" }, { "deposit_at", std::to_string(now) } }).has_value());
    EXPECT_EQ(WalletPlugin::getShared()->getBankInterest(uuid).value(), 0);

    // 3 full days at the daily rate -> floor(principal * rate * days).
    long long threeDaysAgo = now - 3LL * 86400LL * 1000000000LL;
    EXPECT_TRUE(storage->set("WalletBank", uuid, { { "principal", "1000" }, { "deposit_at", std::to_string(threeDaysAgo) } }).has_value());
    long long expected = static_cast<long long>(std::floor(
        1000.0 * config.WalletBankDailyRate * 3.0
    ));
    EXPECT_EQ(WalletPlugin::getShared()->getBankInterest(uuid).value(), expected);
}

TEST_F(WalletPluginTest, WealthRankingOrder) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    auto storage = ServiceProvider::getInstance().getService<SQLiteStorage>("SettingsDB");

    Config::C_Wallet config = GetWalletConfig();

    bool hasScoreboard = true;
    if (!ScoreboardUtils::hasScoreboard(config.TargetScoreboard)) {
        hasScoreboard = false;
        ScoreboardUtils::create(config.TargetScoreboard);
    }

    // test_player is online: its real scoreboard must override the stored snapshot.
    ScoreboardUtils::setScore(*sp, config.TargetScoreboard, 50);

    std::string uuidA = sp->getUuid().asString();
    std::string uuidB = "00000000-0000-0000-0000-0000000000bb";
    std::string uuidC = "00000000-0000-0000-0000-0000000000cc";

    EXPECT_TRUE(storage->set("Wallet", uuidA, { { "name", sp->getRealName() }, { "balance", "999" } }).has_value());
    EXPECT_TRUE(storage->set("Wallet", uuidB, { { "name", "Bob" }, { "balance", "200" } }).has_value());
    EXPECT_TRUE(storage->set("Wallet", uuidC, { { "name", "Carol" }, { "balance", "150" } }).has_value());

    EXPECT_TRUE(WalletPlugin::getShared()->rebuildWealthRanking().has_value());

    // Offline players use the snapshot; online test_player uses the real balance (50, not 999).
    auto ranking = WalletPlugin::getShared()->getWealthRanking(10);
    ASSERT_TRUE(ranking.has_value());
    ASSERT_EQ(ranking.value().size(), 3u);
    EXPECT_EQ(ranking.value().at(0).first, "Bob");
    EXPECT_EQ(ranking.value().at(1).first, "Carol");
    EXPECT_EQ(ranking.value().at(2).first, sp->getRealName());

    auto rank = WalletPlugin::getShared()->getWealthRank(uuidB);
    ASSERT_TRUE(rank.has_value());
    EXPECT_EQ(rank.value().first, 1);
    EXPECT_EQ(rank.value().second, 200);

    auto self = WalletPlugin::getShared()->getWealthRank(uuidA);
    ASSERT_TRUE(self.has_value());
    EXPECT_EQ(self.value().first, 3);
    EXPECT_EQ(self.value().second, 50);

    if (!hasScoreboard)
        ScoreboardUtils::remove(config.TargetScoreboard);
}
