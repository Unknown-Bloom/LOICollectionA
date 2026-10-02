#include <gtest/gtest.h>

#include <string>
#include <vector>
#include <utility>
#include <system_error>

#include <ll/api/Expected.h>
#include <ll/api/service/Bedrock.h>

#include <mc/world/level/Level.h>
#include <mc/world/actor/player/Player.h>

#include "LOICollectionA/include/server/Plugins/MonitorPlugin.h"

using namespace LOICollection::server::Plugins;

TEST(MonitorPluginErrorTest, MakeErrorCodeInvalid) {
    std::error_code code = MonitorPlugin::makeErrorCode(MonitorPluginErrorCode::Invalid);

    EXPECT_EQ(code.value(), 1);
    EXPECT_EQ(std::string(code.category().name()), "MonitorPluginError");
    EXPECT_EQ(code.message(), "Plugin is invalid");
}

TEST(MonitorPluginErrorTest, OutOfRangeErrorCodeFallsBackToUnknownMessage) {
    std::error_code code = MonitorPlugin::makeErrorCode(static_cast<MonitorPluginErrorCode>(99));

    EXPECT_EQ(code.value(), 99);
    EXPECT_EQ(std::string(code.category().name()), "MonitorPluginError");
    EXPECT_EQ(code.message(), "Unknown");
}

class MonitorPluginSidebarTest : public testing::Test {
protected:
    void SetUp() override {
        if (!MonitorPlugin::getShared()->isValid())
            GTEST_SKIP() << "MonitorPlugin is not valid";
    }
};

TEST_F(MonitorPluginSidebarTest, SidebarLifecycleReturnsSuccess) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    ASSERT_TRUE(sp);

    EXPECT_TRUE(MonitorPlugin::getShared()->isValid());

    std::string id = "test_event_sidebar";

    EXPECT_TRUE(MonitorPlugin::getShared()->addSidebar(*sp, id, "Test Event Sidebar", SidebarType::Ascending).has_value());

    std::vector<std::pair<std::string, int>> data = {
        { "test_event_sidebar_line_one", 1 },
        { "test_event_sidebar_line_two", 2 }
    };

    EXPECT_TRUE(MonitorPlugin::getShared()->setSidebar(*sp, id, data).has_value());

    std::vector<std::pair<std::string, int>> single = { { "test_event_sidebar_line_one", 10 } };
    EXPECT_TRUE(MonitorPlugin::getShared()->setSidebar(*sp, id, single).has_value());

    EXPECT_TRUE(MonitorPlugin::getShared()->removeSidebar(*sp, id).has_value());

    EXPECT_TRUE(MonitorPlugin::getShared()->addSidebar(*sp, id, "Test Event Sidebar", SidebarType::Descending).has_value());
    EXPECT_TRUE(MonitorPlugin::getShared()->removeSidebar(*sp, id).has_value());
}
