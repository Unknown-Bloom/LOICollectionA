#include <gtest/gtest.h>

#include <string>
#include <unordered_map>

#include <ll/api/service/Bedrock.h>

#include <mc/world/level/Level.h>
#include <mc/world/actor/player/Player.h>

#include <mc/server/SimulatedPlayer.h>

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"

#include "LOICollectionA/include/server/Plugins/BlacklistPlugin.h"

#include "server/TestSimulatedPlayer.h"

using namespace LOICollection::server::Plugins;

class BlacklistPluginTest : public testing::Test {
protected:
    std::string mBlacklistId{};

protected:
    void SetUp() override {
        if (!BlacklistPlugin::getShared()->isValid())
            GTEST_SKIP() << "BlacklistPlugin is not valid";
    }

    void TearDown() override {
        auto result = BlacklistPlugin::getShared()->getDatabase()->exec("DELETE FROM Blacklist;");
        if (!result.has_value())
            GTEST_FAIL() << "Unable to clear data";
    }

    bool CreateBlacklistEntry() {
        TestSimulatedPlayer sp("test_player1");
        if (!sp.create())
            return false;

        auto result = BlacklistPlugin::getShared()->addBlacklist(*sp.getPlayer(), "Test cause", 3600);
        if (!result.has_value()) return false;

        auto id = BlacklistPlugin::getShared()->getBlacklist(*sp.getPlayer());
        if (!id.has_value()) return false;

        this->mBlacklistId = id.value();

        auto has = BlacklistPlugin::getShared()->hasBlacklist(this->mBlacklistId);
        if (!has.has_value()) return false;

        if (this->mBlacklistId.empty() || !has.value())
            return false;
        
        return sp.destroy();
    }
};

TEST_F(BlacklistPluginTest, AddPlayerToBlacklist) {
    EXPECT_TRUE(CreateBlacklistEntry());
}

TEST_F(BlacklistPluginTest, GetBlacklistData) {
    EXPECT_TRUE(CreateBlacklistEntry());

    auto data = BlacklistPlugin::getShared()->getBlacklistData(this->mBlacklistId);
    EXPECT_TRUE(data.has_value());

    auto& maps = data.value();
    EXPECT_FALSE(maps.empty());
    EXPECT_TRUE(maps.size() == 7);
}

TEST_F(BlacklistPluginTest, GetBlacklists) {
    EXPECT_TRUE(CreateBlacklistEntry());

    auto blacklists = BlacklistPlugin::getShared()->getBlacklists();
    EXPECT_TRUE(blacklists.has_value());

    auto& vecs = blacklists.value();
    EXPECT_FALSE(vecs.empty());
}

TEST_F(BlacklistPluginTest, DeleteBlacklist) {
    EXPECT_TRUE(CreateBlacklistEntry());

    EXPECT_TRUE(BlacklistPlugin::getShared()->delBlacklist(this->mBlacklistId).has_value());

    auto has = BlacklistPlugin::getShared()->hasBlacklist(this->mBlacklistId);
    EXPECT_TRUE(has.has_value());
    EXPECT_FALSE(has.value());
}
