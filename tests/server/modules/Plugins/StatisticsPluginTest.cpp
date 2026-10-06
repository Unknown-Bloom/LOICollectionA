#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ll/api/service/Bedrock.h>

#include <mc/world/level/Level.h>
#include <mc/world/actor/player/Player.h>

#include <mc/server/SimulatedPlayer.h>

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"
#include "LOICollectionA/data/sqlite/block/TypedTable.h"

#include "LOICollectionA/include/server/Plugins/StatisticsPlugin.h"

#include "server/TestSimulatedPlayer.h"

using namespace LOICollection::server::Plugins;
using LOICollection::data::clearTypedTable;

class StatisticsPluginTest : public testing::Test {
protected:
    void SetUp() override {
        if (!StatisticsPlugin::getShared()->isValid())
            GTEST_SKIP() << "StatisticsPlugin is not valid";
    }

    void TearDown() override {
        if (!StatisticsPlugin::getShared()->isValid())
            return;
        auto db = StatisticsPlugin::getShared()->getDatabase();

        auto result = clearTypedTable(*db, "Statistics");
        EXPECT_TRUE(result.has_value()) << "Unable to clear data";
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

TEST_F(StatisticsPluginTest, RankSortsBeforeTruncation) {
    std::vector<std::pair<std::string, int>> data{
        { "a", 3 },
        { "b", 90 },
        { "c", 50 },
        { "d", 7 }
    };

    auto top2 = StatisticsPlugin::rank(data, 2);

    ASSERT_EQ(top2.size(), 2u);
    EXPECT_EQ(top2[0].first, "b");
    EXPECT_EQ(top2[1].first, "c");
}

TEST_F(StatisticsPluginTest, RankKeepsAllWhenLimitIsNonPositive) {
    std::vector<std::pair<std::string, int>> data{
        { "a", 3 },
        { "b", 90 },
        { "c", 50 }
    };

    auto all = StatisticsPlugin::rank(data, -1);

    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0].first, "b");
    EXPECT_EQ(all[1].first, "c");
    EXPECT_EQ(all[2].first, "a");
}

TEST_F(StatisticsPluginTest, RankingListRanksGloballyNotByInsertionOrder) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    TestSimulatedPlayer sp2("test_player2");
    EXPECT_TRUE(sp2.create());

    TestSimulatedPlayer sp3("test_player3");
    EXPECT_TRUE(sp3.create());

    EXPECT_TRUE(StatisticsPlugin::getShared()->addStatistic(*sp, StatisticType::kills, 3).has_value());
    EXPECT_TRUE(StatisticsPlugin::getShared()->addStatistic(*sp2.getPlayer(), StatisticType::kills, 90).has_value());
    EXPECT_TRUE(StatisticsPlugin::getShared()->addStatistic(*sp3.getPlayer(), StatisticType::kills, 50).has_value());

    auto ranking = StatisticsPlugin::getShared()->getRankingList(StatisticType::kills, 1);
    ASSERT_TRUE(ranking.has_value());
    ASSERT_EQ(ranking.value().size(), 1u);
    EXPECT_EQ(ranking.value()[0].first, sp2.getPlayer()->getUuid().asString());
    EXPECT_EQ(ranking.value()[0].second, 90);
}
