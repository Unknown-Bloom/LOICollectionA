#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <string>

#include <ll/api/service/Bedrock.h>

#include <mc/world/level/Level.h>
#include <mc/world/actor/player/Player.h>

#include <mc/server/SimulatedPlayer.h>

#include "LOICollectionA/data/json/JsonStorage.h"

#include "LOICollectionA/utils/mc-server/InventoryUtils.h"
#include "LOICollectionA/utils/mc-server/ScoreboardUtils.h"

#include "LOICollectionA/include/server/Plugins/ChatPlugin.h"

#include "LOICollectionA/include/server/Plugins/CdkPlugin.h"

using namespace LOICollection::server::Plugins;

class CdkPluginTest : public testing::Test {
protected:
    void SetUp() override {
        if (!CdkPlugin::getShared()->isValid())
            GTEST_SKIP() << "CdkPlugin is not valid";
    }

    void TearDown() override {
        CdkPlugin::getShared()->getDatabase()->write({});

        auto saveResult = CdkPlugin::getShared()->getDatabase()->save();
        if (!saveResult.has_value())
            GTEST_FAIL() << "Unable to save data";
    }
};

TEST_F(CdkPluginTest, CreateCdk) {
    EXPECT_TRUE(CdkPlugin::getShared()->create("test", 3600, false).has_value());

    auto has = CdkPlugin::getShared()->has("test");
    EXPECT_TRUE(has.has_value());
    EXPECT_TRUE(has.value());
}

TEST_F(CdkPluginTest, DeleteCdk) {
    EXPECT_TRUE(CdkPlugin::getShared()->create("test", 3600, false).has_value());
    EXPECT_TRUE(CdkPlugin::getShared()->remove("test").has_value());

    auto has = CdkPlugin::getShared()->has("test");
    EXPECT_TRUE(has.has_value());
    EXPECT_FALSE(has.value());
}

TEST_F(CdkPluginTest, GetCdks) {
    EXPECT_TRUE(CdkPlugin::getShared()->create("test", 3600, false).has_value());

    auto cdks = CdkPlugin::getShared()->getCdks();
    EXPECT_TRUE(cdks.has_value());
    EXPECT_FALSE(cdks.value().empty());
}

TEST_F(CdkPluginTest, ConvertCdkNoPersonal) {
    EXPECT_TRUE(CdkPlugin::getShared()->create("test", 3600, false).has_value());

    ScoreboardUtils::create("test_tietu1");
    ScoreboardUtils::create("test_tietu2");

    auto dataResult = CdkPlugin::getShared()->getDatabase()->get<nlohmann::ordered_json>("test");
    ASSERT_TRUE(dataResult.has_value());
    nlohmann::ordered_json data = dataResult.value();
    data["scores"]["test_tietu1"] = 100;
    data["scores"]["test_tietu2"] = 200;
    data["title"]["test_tietu1"] = 0;
    data["title"]["test_tietu2"] = 0;
    data["item"] = nlohmann::ordered_json::array({
        {
            { "id", "minecraft:apple" },
            { "name", "apple" },
            { "quantity", 1 },
            { "specialvalue", 0 },
            { "type", "universal" }
        }, {
            { "id", "{Count:2b,Damage:0s,Name:'minecraft:diamond',WasPickedUp:0b}" },
            { "type", "nbt" }
        }
    });

    CdkPlugin::getShared()->getDatabase()->set("test", data);

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    EXPECT_TRUE(CdkPlugin::getShared()->convert(*sp, "test").has_value());

    EXPECT_EQ(ScoreboardUtils::getScore(*sp, "test_tietu1"), 100);
    EXPECT_EQ(ScoreboardUtils::getScore(*sp, "test_tietu2"), 200);
    EXPECT_TRUE(InventoryUtils::isItemInInventory(*sp, "minecraft:apple", 1));
    EXPECT_TRUE(InventoryUtils::isItemInInventory(*sp, "minecraft:diamond", 2));

    if (ChatPlugin::getShared()->isValid()) {
        auto has1 = ChatPlugin::getShared()->hasTitle(*sp, "test_tietu1");
        EXPECT_TRUE(has1.has_value());
        EXPECT_TRUE(has1.value());

        auto has2 = ChatPlugin::getShared()->hasTitle(*sp, "test_tietu2");
        EXPECT_TRUE(has2.has_value());
        EXPECT_TRUE(has2.value());
    }

    auto playerDataResult = CdkPlugin::getShared()->getDatabase()->get_ptr<nlohmann::ordered_json>("/test/player");
    ASSERT_TRUE(playerDataResult.has_value());
    auto& playerData = playerDataResult.value();
    EXPECT_TRUE(std::find(playerData.begin(), playerData.end(), sp->getUuid().asString()) != playerData.end());

    if (CdkPlugin::getShared()->isValid()) {
        EXPECT_TRUE(ChatPlugin::getShared()->delTitle(*sp, "test_tietu1").has_value());
        EXPECT_TRUE(ChatPlugin::getShared()->delTitle(*sp, "test_tietu2").has_value());
    }

    ScoreboardUtils::remove("test_tietu1");
    ScoreboardUtils::remove("test_tietu2");
}

TEST_F(CdkPluginTest, ConvertCdkWithPersonal) {
    EXPECT_TRUE(CdkPlugin::getShared()->create("test", 3600, true).has_value());

    ScoreboardUtils::create("test_tietu1");
    ScoreboardUtils::create("test_tietu2");

    auto dataResult = CdkPlugin::getShared()->getDatabase()->get<nlohmann::ordered_json>("test");
    ASSERT_TRUE(dataResult.has_value());
    nlohmann::ordered_json data = dataResult.value();
    data["scores"]["test_tietu1"] = 100;
    data["scores"]["test_tietu2"] = 200;
    data["title"]["test_tietu1"] = 0;
    data["title"]["test_tietu2"] = 0;
    data["item"] = nlohmann::ordered_json::array({
        {
            { "id", "minecraft:apple" },
            { "name", "apple" },
            { "quantity", 1 },
            { "specialvalue", 0 },
            { "type", "universal" }
        }, {
            { "id", "{Count:2b,Damage:0s,Name:'minecraft:diamond',WasPickedUp:0b}" },
            { "type", "nbt" }
        }
    });

    CdkPlugin::getShared()->getDatabase()->set("test", data);

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    EXPECT_TRUE(CdkPlugin::getShared()->convert(*sp, "test").has_value());

    EXPECT_EQ(ScoreboardUtils::getScore(*sp, "test_tietu1"), 100);
    EXPECT_EQ(ScoreboardUtils::getScore(*sp, "test_tietu2"), 200);
    EXPECT_TRUE(InventoryUtils::isItemInInventory(*sp, "minecraft:apple", 1));
    EXPECT_TRUE(InventoryUtils::isItemInInventory(*sp, "minecraft:diamond", 2));

    if (ChatPlugin::getShared()->isValid()) {
        auto has1 = ChatPlugin::getShared()->hasTitle(*sp, "test_tietu1");
        EXPECT_TRUE(has1.has_value());
        EXPECT_TRUE(has1.value());

        auto has2 = ChatPlugin::getShared()->hasTitle(*sp, "test_tietu2");
        EXPECT_TRUE(has2.has_value());
        EXPECT_TRUE(has2.value());
    }

    auto has = CdkPlugin::getShared()->has("test");
    EXPECT_TRUE(has.has_value());
    EXPECT_FALSE(has.value());

    if (CdkPlugin::getShared()->isValid()) {
        EXPECT_TRUE(ChatPlugin::getShared()->delTitle(*sp, "test_tietu1").has_value());
        EXPECT_TRUE(ChatPlugin::getShared()->delTitle(*sp, "test_tietu2").has_value());
    }

    ScoreboardUtils::remove("test_tietu1");
    ScoreboardUtils::remove("test_tietu2");
}
