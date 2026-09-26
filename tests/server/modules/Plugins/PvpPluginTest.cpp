#include <gtest/gtest.h>

#include <string>

#include <ll/api/service/Bedrock.h>

#include <mc/world/level/Level.h>
#include <mc/world/actor/player/Player.h>

#include <mc/server/SimulatedPlayer.h>

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"

#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/include/server/Plugins/PvpPlugin.h"

#include "server/TestSimulatedPlayer.h"

using namespace LOICollection::server::Plugins;

class PvpPluginTest : public testing::Test {
protected:
    void SetUp() override {
        if (!PvpPlugin::getShared()->isValid())
            GTEST_SKIP() << "PvpPlugin is not valid";
    }

    void TearDown() override {
        auto result = ServiceProvider::getInstance().getService<BlockRepository>("SettingsDB")->exec("DELETE FROM Pvp;");
        if (!result.has_value())
            GTEST_FAIL() << "Unable to clear data";
    }
};

TEST_F(PvpPluginTest, EnableAndIsPvp) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    EXPECT_TRUE(PvpPlugin::getShared()->enable(*sp, true).has_value());

    auto enabled = PvpPlugin::getShared()->isEnable(*sp);
    EXPECT_TRUE(enabled.has_value());
    EXPECT_TRUE(enabled.value());

    EXPECT_TRUE(PvpPlugin::getShared()->enable(*sp, false).has_value());

    auto disabled = PvpPlugin::getShared()->isEnable(*sp);
    EXPECT_TRUE(disabled.has_value());
    EXPECT_FALSE(disabled.value());
}

TEST_F(PvpPluginTest, CheckPvp) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    TestSimulatedPlayer sp2("test_player3");
    EXPECT_TRUE(sp2.create());

    EXPECT_TRUE(PvpPlugin::getShared()->enable(*sp, true).has_value());
    EXPECT_TRUE(PvpPlugin::getShared()->enable(*sp2.getPlayer(), true).has_value());

    sp2.getPlayer()->teleport(sp->getPosition(), sp->getDimensionId());

    static_cast<SimulatedPlayer*>(sp)->simulateAttack(sp2.getPlayer());
    EXPECT_TRUE(sp2.getPlayer()->getHealth() < 20);

    sp2.getPlayer()->heal(20);

    EXPECT_TRUE(PvpPlugin::getShared()->enable(*sp2.getPlayer(), false).has_value());

    static_cast<SimulatedPlayer*>(sp)->simulateAttack(sp2.getPlayer());
    EXPECT_TRUE(sp2.getPlayer()->getHealth() == 20);
}
