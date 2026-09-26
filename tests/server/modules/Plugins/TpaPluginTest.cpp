#include <gtest/gtest.h>

#include <string>
#include <unordered_map>

#include <ll/api/service/Bedrock.h>
#include <ll/api/thread/ServerThreadExecutor.h>

#include <mc/world/level/Level.h>
#include <mc/world/actor/player/Player.h>

#include <mc/server/SimulatedPlayer.h>

#include "LOICollectionA/utils/mc-server/ScoreboardUtils.h"

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"

#include "LOICollectionA/base/Wrapper.h"
#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/TpaPlugin.h"

#include "common/coro/MockExecutor.h"
#include "server/TestSimulatedPlayer.h"

using namespace LOICollection::server::Plugins;

class TpaPluginTest : public testing::Test {
protected:
    std::string mBlacklistId{};

protected:
    void SetUp() override {
        if (!TpaPlugin::getShared()->isValid())
            GTEST_SKIP() << "TpaPlugin is not valid";
    }

    void TearDown() override {
        if (!TpaPlugin::getShared()->isValid())
            return;
        auto r1 = TpaPlugin::getShared()->getDatabase()->exec("DELETE FROM Blacklist;");
        if (!r1.has_value())
            GTEST_FAIL() << "Unable to clear data";

        auto r2 = ServiceProvider::getInstance().getService<BlockRepository>("SettingsDB")->exec("DELETE FROM Tpa;");
        if (!r2.has_value())
            GTEST_FAIL() << "Unable to clear data";

        EXPECT_TRUE(TpaPlugin::getShared()->setExecutor(ll::thread::ServerThreadExecutor::getDefault()).has_value());
    }

    bool CreateBlacklistEntry() {
        auto sp2 = ll::service::getLevel()->getPlayer("test_player");

        TestSimulatedPlayer sp("test_player2");
        if (!sp.create() || !sp2)
            return false;

        auto addResult = TpaPlugin::getShared()->addBlacklist(*sp2, *sp.getPlayer());
        if (!addResult.has_value()) return false;

        auto id = TpaPlugin::getShared()->getBlacklist(*sp2, *sp.getPlayer());
        if (!id.has_value()) return false;

        this->mBlacklistId = id.value();

        auto has = TpaPlugin::getShared()->hasBlacklist(*sp2, this->mBlacklistId);
        if (!has.has_value()) return false;

        if (this->mBlacklistId.empty() || !has.value())
            return false;

        return sp.destroy();
    }
};

TEST_F(TpaPluginTest, InviteSetting) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    auto invite1 = TpaPlugin::getShared()->isInvite(*sp);
    EXPECT_TRUE(invite1.has_value());
    EXPECT_FALSE(invite1.value());

    EXPECT_TRUE(TpaPlugin::getShared()->setInvite(*sp, true).has_value());

    auto invite2 = TpaPlugin::getShared()->isInvite(*sp);
    EXPECT_TRUE(invite2.has_value());
    EXPECT_TRUE(invite2.value());
}

TEST_F(TpaPluginTest, AddPlayerToBlacklist) {
    EXPECT_TRUE(CreateBlacklistEntry());
}

TEST_F(TpaPluginTest, DeletePlayerFromBlacklist) {
    EXPECT_TRUE(CreateBlacklistEntry());

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    EXPECT_TRUE(TpaPlugin::getShared()->delBlacklist(*sp, this->mBlacklistId).has_value());

    auto has = TpaPlugin::getShared()->hasBlacklist(*sp, this->mBlacklistId);
    EXPECT_TRUE(has.has_value());
    EXPECT_FALSE(has.value());
}

TEST_F(TpaPluginTest, GetBlacklist) {
    EXPECT_TRUE(CreateBlacklistEntry());

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    auto blacklists = TpaPlugin::getShared()->getBlacklist(*sp);
    EXPECT_TRUE(blacklists.has_value());
    EXPECT_FALSE(blacklists.value().empty());
}

TEST_F(TpaPluginTest, GetBlacklistFromTarget) {
    EXPECT_TRUE(CreateBlacklistEntry());

    auto blacklists = TpaPlugin::getShared()->getBlacklistFromTarget({ this->mBlacklistId });
    EXPECT_TRUE(blacklists.has_value());
    EXPECT_FALSE(blacklists.value().empty());
}

TEST_F(TpaPluginTest, GetBlacklistData) {
    EXPECT_TRUE(CreateBlacklistEntry());

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    auto data = TpaPlugin::getShared()->getBlacklistData(this->mBlacklistId);
    EXPECT_TRUE(data.has_value());
    EXPECT_FALSE(data.value().empty());
    EXPECT_TRUE(data.value()["author"] == sp->getUuid().asString());
}

TEST_F(TpaPluginTest, ForTpaContent) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    Config::C_Tpa config = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Tpa;

    bool hasScoreboard = true;
    if (!ScoreboardUtils::hasScoreboard(config.TargetScoreboard)) {
        hasScoreboard = false;

        ScoreboardUtils::create(config.TargetScoreboard);
    }

    ScoreboardUtils::setScore(*sp, config.TargetScoreboard, config.RequestRequired);

    auto content1 = TpaPlugin::getShared()->forTpaContent(*sp);
    EXPECT_TRUE(content1.has_value());
    EXPECT_TRUE(content1.value());

    auto content2 = TpaPlugin::getShared()->forTpaContent(*sp);
    EXPECT_TRUE(content2.has_value());
    EXPECT_FALSE(content2.value());

    EXPECT_EQ(ScoreboardUtils::getScore(*sp, config.TargetScoreboard), 0);

    if (!hasScoreboard)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

TEST_F(TpaPluginTest, SendRequest) {
    MockExecutor executor;
    EXPECT_TRUE(TpaPlugin::getShared()->setExecutor(executor).has_value());

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    TestSimulatedPlayer sp2("test_player2");
    EXPECT_TRUE(sp2.create());

    EXPECT_TRUE(TpaPlugin::getShared()->sendRequest(*sp, *sp2.getPlayer(), "test_request", TpaType::tpa).has_value());

    auto has1 = TpaPlugin::getShared()->hasRequest(sp->getUuid().asString(), sp2.getPlayer()->getUuid().asString());
    EXPECT_TRUE(has1.has_value());
    EXPECT_TRUE(has1.value());

    Config::C_Tpa config = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Tpa;

    executor.advanceTime(std::chrono::seconds(config.RequestTimeout + 1));

    auto has2 = TpaPlugin::getShared()->hasRequest(sp->getUuid().asString(), sp2.getPlayer()->getUuid().asString());
    EXPECT_TRUE(has2.has_value());
    EXPECT_FALSE(has2.value());
}

TEST_F(TpaPluginTest, AcceptRequest) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    TestSimulatedPlayer sp2("test_player2");
    EXPECT_TRUE(sp2.create());

    Config::C_Tpa config = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Tpa;

    bool hasScoreboard = true;
    if (!ScoreboardUtils::hasScoreboard(config.TargetScoreboard)) {
        hasScoreboard = false;

        ScoreboardUtils::create(config.TargetScoreboard);
    }

    ScoreboardUtils::setScore(*sp, config.TargetScoreboard, config.RequestRequired);

    EXPECT_TRUE(TpaPlugin::getShared()->sendRequest(*sp, *sp2.getPlayer(), "test_request", TpaType::tpa).has_value());

    sp2.getPlayer()->teleport(sp2.getPlayer()->getPosition() + Vec3(1, 1, 1), sp2.getPlayer()->getDimensionId());

    auto accept = TpaPlugin::getShared()->acceptRequest(*sp2.getPlayer(), "test_request");
    EXPECT_TRUE(accept.has_value());
    EXPECT_TRUE(accept.value());

    auto has = TpaPlugin::getShared()->hasRequest(sp->getUuid().asString(), sp2.getPlayer()->getUuid().asString());
    EXPECT_TRUE(has.has_value());
    EXPECT_FALSE(has.value());

    Vec3 spPos = sp->getPosition();
    Vec3 sp2Pos = sp2.getPlayer()->getPosition();

    EXPECT_EQ(static_cast<int>(spPos.x), static_cast<int>(sp2Pos.x));
    EXPECT_EQ(static_cast<int>(spPos.y), static_cast<int>(sp2Pos.y) + 1);
    EXPECT_EQ(static_cast<int>(spPos.z), static_cast<int>(sp2Pos.z));

    if (!hasScoreboard)
        ScoreboardUtils::remove(config.TargetScoreboard);
}

TEST_F(TpaPluginTest, RejectRequest) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    TestSimulatedPlayer sp2("test_player2");
    EXPECT_TRUE(sp2.create());

    EXPECT_TRUE(TpaPlugin::getShared()->sendRequest(*sp, *sp2.getPlayer(), "test_request", TpaType::tpa).has_value());

    sp2.getPlayer()->teleport(sp2.getPlayer()->getPosition() + Vec3(1, 1, 1), sp2.getPlayer()->getDimensionId());

    auto reject = TpaPlugin::getShared()->rejectRequest(*sp2.getPlayer(), "test_request");
    EXPECT_TRUE(reject.has_value());
    EXPECT_TRUE(reject.value());

    auto has = TpaPlugin::getShared()->hasRequest(sp->getUuid().asString(), sp2.getPlayer()->getUuid().asString());
    EXPECT_TRUE(has.has_value());
    EXPECT_FALSE(has.value());

    EXPECT_NE(sp->getPosition(), sp2.getPlayer()->getPosition());
}

TEST_F(TpaPluginTest, CancelRequest) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    TestSimulatedPlayer sp2("test_player2");
    EXPECT_TRUE(sp2.create());

    EXPECT_TRUE(TpaPlugin::getShared()->sendRequest(*sp, *sp2.getPlayer(), "test_request", TpaType::tpa).has_value());

    sp2.getPlayer()->teleport(sp2.getPlayer()->getPosition() + Vec3(1, 1, 1), sp2.getPlayer()->getDimensionId());

    auto cancel = TpaPlugin::getShared()->cancelRequest("test_request");
    EXPECT_TRUE(cancel.has_value());
    EXPECT_TRUE(cancel.value());

    auto has = TpaPlugin::getShared()->hasRequest(sp->getUuid().asString(), sp2.getPlayer()->getUuid().asString());
    EXPECT_TRUE(has.has_value());
    EXPECT_FALSE(has.value());

    EXPECT_NE(sp->getPosition(), sp2.getPlayer()->getPosition());
}
