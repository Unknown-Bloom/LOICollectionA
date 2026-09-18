#include <gtest/gtest.h>

#include <string>

#include <ll/api/service/Bedrock.h>
#include <ll/api/thread/ThreadPoolExecutor.h>

#include <mc/world/level/Level.h>
#include <mc/world/actor/player/Player.h>

#include <mc/server/SimulatedPlayer.h>

#include "LOICollectionA/include/server/Plugins/BehaviorEvent/BehaviorEventPlugin.h"

using namespace LOICollection::server::Plugins;

class BehaviorEventPluginTest : public testing::Test {
protected:
    void SetUp() override {
        if (!BehaviorEventPlugin::getShared()->isValid())
            GTEST_SKIP() << "BehaviorEventPlugin is not valid";
    }

    void TearDown() override {
        auto result = BehaviorEventPlugin::getShared()->clean(0);
        if (!result.has_value())
            GTEST_FAIL() << "Unable to clear data";
    }

    std::string CreateDatabaseEntry() {
        auto mEvent = BehaviorEventPlugin::getShared()->getBasicEvent("test", "test", Vec3(-114514, -114514, -114514), 0);
        if (!mEvent.has_value()) return {};

        auto result = BehaviorEventPlugin::getShared()->write(mEvent.value());
        if (!result.has_value()) return {};

        return result.value();
    }
};

TEST_F(BehaviorEventPluginTest, WriteDatabase) {
    EXPECT_FALSE(CreateDatabaseEntry().empty());
}

TEST_F(BehaviorEventPluginTest, GetEvents) {
    auto id = CreateDatabaseEntry();

    auto data = BehaviorEventPlugin::getShared()->getEvents();
    EXPECT_TRUE(data.has_value());

    auto& vecs = data.value();
    EXPECT_EQ(vecs.size(), 1);
    EXPECT_EQ(vecs[0], id);
}

TEST_F(BehaviorEventPluginTest, GetEventByConditions) {
    auto id = CreateDatabaseEntry();

    auto data = BehaviorEventPlugin::getShared()->getEvents({
        { "event_name", "test" },
        { "event_type", "test" }
    });
    EXPECT_TRUE(data.has_value());

    auto& vecs = data.value();
    EXPECT_FALSE(vecs.empty());
    EXPECT_EQ(vecs.size(), 1);
    EXPECT_EQ(vecs[0], id);
}

TEST_F(BehaviorEventPluginTest, GetEventByFilter) {
    auto id = CreateDatabaseEntry();

    auto data = BehaviorEventPlugin::getShared()->getEvents({
        { "event_type", "" }
    }, [](std::string value) -> bool {
        return value == "test";
    });
    EXPECT_TRUE(data.has_value());

    auto& vecs = data.value();
    EXPECT_FALSE(vecs.empty());
    EXPECT_EQ(vecs.size(), 1);
    EXPECT_EQ(vecs[0], id);
}

TEST_F(BehaviorEventPluginTest, GetEventByPosition) {
    auto id = CreateDatabaseEntry();

    auto data = BehaviorEventPlugin::getShared()->getEventsByPosition(0, [](int x, int y, int z) -> bool {
        return x == -114514 && y == -114514 && z == -114514;
    });
    EXPECT_TRUE(data.has_value());

    auto& vecs = data.value();
    EXPECT_FALSE(vecs.empty());
    EXPECT_EQ(vecs.size(), 1);
    EXPECT_EQ(vecs[0], id);
}

TEST_F(BehaviorEventPluginTest, GetEventsWithinHours) {
    auto id = CreateDatabaseEntry();

    auto data = BehaviorEventPlugin::getShared()->getEventsWithin(1);
    EXPECT_TRUE(data.has_value());

    auto& vecs = data.value();
    EXPECT_EQ(vecs.size(), 1);
    EXPECT_EQ(vecs[0], id);
}

TEST_F(BehaviorEventPluginTest, Filter) {
    auto first = CreateDatabaseEntry();
    auto second = CreateDatabaseEntry();

    auto data = BehaviorEventPlugin::getShared()->filter({ first, second });
    EXPECT_TRUE(data.has_value());

    auto& vecs = data.value();
    EXPECT_FALSE(vecs.empty());
    EXPECT_EQ(vecs.size(), 1);
    EXPECT_EQ(vecs[0], second);
}
