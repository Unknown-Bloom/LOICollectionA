#include <gtest/gtest.h>

#include <string>

#include <ll/api/service/Bedrock.h>

#include <mc/world/level/Level.h>
#include <mc/world/actor/player/Player.h>

#include <mc/server/SimulatedPlayer.h>

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"
#include "LOICollectionA/data/sqlite/block/TypedTable.h"

#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/include/server/Plugins/LanguagePlugin.h"

using namespace LOICollection::server::Plugins;
using LOICollection::data::clearTypedTable;

class LanguagePluginTest : public testing::Test {
protected:
    void TearDown() override {
        auto result = clearTypedTable(*ServiceProvider::getInstance().getService<BlockRepository>("SettingsDB"), "Language");
        EXPECT_TRUE(result.has_value()) << "Unable to clear data";
    }
};

TEST_F(LanguagePluginTest, SetAndGetLanguage) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    EXPECT_TRUE(LanguagePlugin::getShared()->set(*sp, "en_US").has_value());

    auto langcode = LanguagePlugin::getShared()->getLanguage(*sp);
    EXPECT_TRUE(langcode.has_value());
    EXPECT_FALSE(langcode.value().empty());
    EXPECT_TRUE(langcode.value() == "en_US");
}
