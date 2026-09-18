#include <gtest/gtest.h>

#include <string>

#include <ll/api/service/Bedrock.h>

#include <mc/world/level/Level.h>
#include <mc/world/actor/player/Player.h>

#include <mc/server/SimulatedPlayer.h>

#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/data/json/JsonStorage.h"
#include "LOICollectionA/data/sqlite/block/BlockRepository.h"

#include "LOICollectionA/include/server/Plugins/NoticePlugin.h"

using namespace LOICollection::server::Plugins;

class NoticePluginTest : public testing::Test {
protected:
    void SetUp() override {
        if (!NoticePlugin::getShared()->isValid())
            GTEST_SKIP() << "NoticePlugin is not valid";
    }

    void TearDown() override {
        NoticePlugin::getShared()->getDatabase()->write({});

        auto saveResult = NoticePlugin::getShared()->getDatabase()->save();
        if (!saveResult.has_value())
            GTEST_FAIL() << "Unable to save data";

        auto result = ServiceProvider::getInstance().getService<BlockRepository>("SettingsDB")->exec("DELETE FROM Notice;");
        if (!result.has_value())
            GTEST_FAIL() << "Unable to clear data";
    }
};

TEST_F(NoticePluginTest, CreateNotice) {
    EXPECT_TRUE(NoticePlugin::getShared()->create("test_notice", "Test Notice", 1, false).has_value());

    auto has = NoticePlugin::getShared()->has("test_notice");
    EXPECT_TRUE(has.has_value());
    EXPECT_TRUE(has.value());
}

TEST_F(NoticePluginTest, RemoveNotice) {
    EXPECT_TRUE(NoticePlugin::getShared()->create("test_notice", "Test Notice", 1, false).has_value());

    auto has = NoticePlugin::getShared()->has("test_notice");
    EXPECT_TRUE(has.has_value());
    EXPECT_TRUE(has.value());

    EXPECT_TRUE(NoticePlugin::getShared()->remove("test_notice").has_value());

    auto has2 = NoticePlugin::getShared()->has("test_notice");
    EXPECT_TRUE(has2.has_value());
    EXPECT_FALSE(has2.value());
}

TEST_F(NoticePluginTest, SettingCloseStatus) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    EXPECT_TRUE(NoticePlugin::getShared()->setClose(*sp, true).has_value());

    auto isClose = NoticePlugin::getShared()->isClose(*sp);
    EXPECT_TRUE(isClose.has_value());
    EXPECT_TRUE(isClose.value());

    EXPECT_TRUE(NoticePlugin::getShared()->setClose(*sp, false).has_value());

    auto isClose2 = NoticePlugin::getShared()->isClose(*sp);
    EXPECT_TRUE(isClose2.has_value());
    EXPECT_FALSE(isClose2.value());
}
