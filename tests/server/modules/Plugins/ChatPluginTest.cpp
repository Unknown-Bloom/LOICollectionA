#include <gtest/gtest.h>

#include <string>
#include <unordered_map>

#include <ll/api/service/Bedrock.h>

#include <mc/world/level/Level.h>
#include <mc/world/actor/player/Player.h>

#include <mc/server/SimulatedPlayer.h>

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"

#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/include/server/Plugins/ChatPlugin.h"

#include "server/TestSimulatedPlayer.h"

using namespace LOICollection::server::Plugins;

class ChatPluginTest : public testing::Test {
protected:
    std::string mBlacklistId{};

protected:
    void SetUp() override {
        if (!ChatPlugin::getShared()->isValid())
            GTEST_SKIP() << "ChatPlugin is not valid";
    }

    void TearDown() override {
        auto db = ChatPlugin::getShared()->getDatabase();

        auto r1 = db->exec("DELETE FROM Blacklist;");
        if (!r1.has_value())
            GTEST_FAIL() << "Unable to clear data";

        auto r2 = db->exec("DELETE FROM Titles;");
        if (!r2.has_value())
            GTEST_FAIL() << "Unable to clear data";

        auto r3 = ServiceProvider::getInstance().getService<BlockRepository>("SettingsDB")->exec("DELETE FROM Chat;");
        if (!r3.has_value())
            GTEST_FAIL() << "Unable to clear data";
    }

    bool CreateBlacklistEntry() {
        auto sp2 = ll::service::getLevel()->getPlayer("test_player");

        TestSimulatedPlayer sp("test_player2");
        if (!sp.create() || !sp2)
            return false;

        auto addResult = ChatPlugin::getShared()->addBlacklist(*sp2, *sp.getPlayer());
        if (!addResult.has_value()) return false;

        auto id = ChatPlugin::getShared()->getBlacklist(*sp2, *sp.getPlayer());
        if (!id.has_value()) return false;

        this->mBlacklistId = id.value();

        auto has = ChatPlugin::getShared()->hasBlacklist(*sp2, this->mBlacklistId);
        if (!has.has_value()) return false;

        if (this->mBlacklistId.empty() || !has.value())
            return false;

        return sp.destroy();
    }

    bool CreateTitleEntry() {
        auto sp = ll::service::getLevel()->getPlayer("test_player");
        if (!sp)
            return false;

        if (!ChatPlugin::getShared()->addTitle(*sp, "Test Title", 24).has_value()) return false;

        auto has = ChatPlugin::getShared()->hasTitle(*sp, "Test Title");
        if (!has.has_value() || !has.value()) return false;

        return true;
    }
};

TEST_F(ChatPluginTest, AddPlayerToBlacklist) {
    EXPECT_TRUE(CreateBlacklistEntry());
}

TEST_F(ChatPluginTest, DeletePlayerFromBlacklist) {
    EXPECT_TRUE(CreateBlacklistEntry());

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    EXPECT_TRUE(ChatPlugin::getShared()->delBlacklist(*sp, this->mBlacklistId).has_value());

    auto has = ChatPlugin::getShared()->hasBlacklist(*sp, this->mBlacklistId);
    EXPECT_TRUE(has.has_value());
    EXPECT_FALSE(has.value());
}

TEST_F(ChatPluginTest, GetBlacklist) {
    EXPECT_TRUE(CreateBlacklistEntry());

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    auto blacklists = ChatPlugin::getShared()->getBlacklist(*sp);
    EXPECT_TRUE(blacklists.has_value());
    EXPECT_FALSE(blacklists.value().empty());
}

TEST_F(ChatPluginTest, GetBlacklistData) {
    EXPECT_TRUE(CreateBlacklistEntry());

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    auto data = ChatPlugin::getShared()->getBlacklistData(this->mBlacklistId);
    EXPECT_TRUE(data.has_value());
    EXPECT_FALSE(data.value().empty());
    EXPECT_TRUE(data.value()["author"] == sp->getUuid().asString());
}

TEST_F(ChatPluginTest, AddTitleToPlayer) {
    EXPECT_TRUE(CreateTitleEntry());
}

TEST_F(ChatPluginTest, DeleteTitleFromPlayer) {
    EXPECT_TRUE(CreateTitleEntry());

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    EXPECT_TRUE(ChatPlugin::getShared()->delTitle(*sp, "Test Title").has_value());

    auto has = ChatPlugin::getShared()->hasTitle(*sp, "Test Title");
    EXPECT_TRUE(has.has_value());
    EXPECT_FALSE(has.value());
}

TEST_F(ChatPluginTest, GetTitleFromPlayer) {
    EXPECT_TRUE(CreateTitleEntry());

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    EXPECT_TRUE(ChatPlugin::getShared()->setTitle(*sp, "Test Title").has_value());

    auto title = ChatPlugin::getShared()->getTitle(*sp);
    EXPECT_TRUE(title.has_value());
    EXPECT_FALSE(title.value().empty());
    EXPECT_TRUE(title.value() == "Test Title");
}

TEST_F(ChatPluginTest, GetTitleTimeFromPlayer) {
    EXPECT_TRUE(CreateTitleEntry());

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    auto time = ChatPlugin::getShared()->getTitleTime(*sp, "Test Title");
    EXPECT_TRUE(time.has_value());
    EXPECT_FALSE(time.value().empty());
}

TEST_F(ChatPluginTest, GetTitlesFromPlayer) {
    EXPECT_TRUE(CreateTitleEntry());

    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    auto titles = ChatPlugin::getShared()->getTitles(*sp);
    EXPECT_TRUE(titles.has_value());
    EXPECT_FALSE(titles.value().empty());
    EXPECT_TRUE(std::find(titles.value().begin(), titles.value().end(), "Test Title") != titles.value().end());
}

TEST_F(ChatPluginTest, ChatFormatting) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    EXPECT_TRUE(sp);

    static_cast<SimulatedPlayer*>(sp)->simulateChat("Test chat");
}
