#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include <ll/api/service/Bedrock.h>

#include <mc/world/level/Level.h>
#include <mc/world/actor/player/Player.h>

#include <mc/server/SimulatedPlayer.h>

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"

#include "LOICollectionA/include/server/Plugins/StatisticsPlugin.h"

using namespace LOICollection::server::Plugins;

class StatisticsPluginTest : public testing::Test {
protected:
    void SetUp() override {
        if (!StatisticsPlugin::getShared()->isValid())
            GTEST_SKIP() << "StatisticsPlugin is not valid";
    }

    void TearDown() override {
        auto db = StatisticsPlugin::getShared()->getDatabase();

        auto result = db->exec("DELETE FROM Statistics;");
        if (!result.has_value())
            GTEST_FAIL() << "Unable to clear data";
    }
};

TEST_F(StatisticsPluginTest, AddAndGetStatistic) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    EXPECT_TRUE(StatisticsPlugin::getShared()->addStatistic(*sp, StatisticType::onlinetime, 100).has_value());
    EXPECT_TRUE(StatisticsPlugin::getShared()->addStatistic(*sp, StatisticType::deaths, 100).has_value());

    auto stat1 = StatisticsPlugin::getShared()->getStatistic(*sp, StatisticType::onlinetime);
    EXPECT_TRUE(stat1.has_value());
    EXPECT_EQ(stat1.value(), 100);

    auto stat2 = StatisticsPlugin::getShared()->getStatistic(*sp, StatisticType::deaths);
    EXPECT_TRUE(stat2.has_value());
    EXPECT_EQ(stat2.value(), 100);
}
