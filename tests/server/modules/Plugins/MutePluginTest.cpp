#include <gtest/gtest.h>

#include <string>
#include <unordered_map>

#include <ll/api/service/Bedrock.h>

#include <mc/world/level/Level.h>
#include <mc/world/actor/player/Player.h>

#include <mc/server/SimulatedPlayer.h>

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"

#include "LOICollectionA/include/server/Plugins/MutePlugin.h"

using namespace LOICollection::server::Plugins;

class MutePluginTest : public testing::Test {
protected:
    std::string mMuteId{};

protected:
    void SetUp() override {
        if (!MutePlugin::getShared()->isValid())
            GTEST_SKIP() << "MutePlugin is not valid";
    }

    void TearDown() override {
        auto result = MutePlugin::getShared()->getDatabase()->exec("DELETE FROM Mute;");
        if (!result.has_value())
            GTEST_FAIL() << "Unable to clear data";
    }

    bool CreateMuteEntry() {
        auto sp = ll::service::getLevel()->getPlayer("test_player");
        if (!sp)
            return false;

        auto addResult = MutePlugin::getShared()->addMute(*sp, "Test Cause", 3600);
        if (!addResult.has_value()) return false;

        auto id = MutePlugin::getShared()->getMute(*sp);
        if (!id.has_value()) return false;

        this->mMuteId = id.value();

        auto has = MutePlugin::getShared()->hasMute(this->mMuteId);
        if (!has.has_value()) return false;

        if (this->mMuteId.empty() || !has.value())
            return false;

        return true;
    }
};

TEST_F(MutePluginTest, AddPlayerToMute) {
    EXPECT_TRUE(CreateMuteEntry());
}

TEST_F(MutePluginTest, GetMuteData) {
    EXPECT_TRUE(CreateMuteEntry());

    auto data = MutePlugin::getShared()->getMuteData(this->mMuteId);
    EXPECT_TRUE(data.has_value());

    auto& maps = data.value();
    EXPECT_FALSE(maps.empty());
    EXPECT_TRUE(maps.size() == 5);
    EXPECT_TRUE(maps["name"] == "test_player");
}

TEST_F(MutePluginTest, GetMutes) {
    EXPECT_TRUE(CreateMuteEntry());

    auto mutes = MutePlugin::getShared()->getMutes();
    EXPECT_TRUE(mutes.has_value());

    auto& vecs = mutes.value();
    EXPECT_FALSE(vecs.empty());
}

TEST_F(MutePluginTest, DeleteMute) {
    EXPECT_TRUE(CreateMuteEntry());

    EXPECT_TRUE(MutePlugin::getShared()->delMute(this->mMuteId).has_value());

    auto has = MutePlugin::getShared()->hasMute(this->mMuteId);
    EXPECT_TRUE(has.has_value());
    EXPECT_FALSE(has.value());
}

TEST_F(MutePluginTest, InterceptPlayerMessage) {
    EXPECT_TRUE(CreateMuteEntry());

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    static_cast<SimulatedPlayer*>(sp)->simulateChat("Test Chat");
}
