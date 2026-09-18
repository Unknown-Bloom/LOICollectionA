#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <SQLiteCpp/SQLiteCpp.h>

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"

namespace {

class LegacyMigratorTest : public testing::Test {
protected:
    static void SetUpTestSuite() {
        dir = std::filesystem::temp_directory_path() / "legacy_migrator_test";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        dbPath = (dir / "wallet.db").string();

        // 用旧版 SQLiteStorage 的真实 schema 构造旧库。
        SQLite::Database db(dbPath.c_str(),
            SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec("CREATE TABLE wallet ("
                "key TEXT PRIMARY KEY, "
                "created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP, "
                "updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP, "
                "balance TEXT, owner TEXT)");
        db.exec("CREATE TABLE events ("
                "key TEXT PRIMARY KEY, "
                "created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP, "
                "updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP, "
                "event_name TEXT, event_type TEXT)");
        db.exec("INSERT INTO wallet (key, balance, owner) VALUES "
                "('u1', '100', 'Steve'), ('u2', '250', 'Alex'), ('u3', '33', NULL)");
        db.exec("INSERT INTO events (key, event_name, event_type) VALUES "
                "('1001', 'place', 'Operable')");
    }

    static void TearDownTestSuite() {
        std::filesystem::remove_all(dir);
    }

    static int archiveCount() {
        int count = 0;
        for (auto const& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.path().filename().string().ends_with(".legacy"))
                ++count;
        }
        return count;
    }

    static std::filesystem::path dir;
    static std::string dbPath;
};

std::filesystem::path LegacyMigratorTest::dir;
std::string LegacyMigratorTest::dbPath;

TEST_F(LegacyMigratorTest, OpensMigratingLegacyDb) {
    // 打开即触发旧库归档 + 块模型回放。
    EXPECT_EQ(archiveCount(), 0); // 打开前无归档
    auto repo = BlockRepository::open(dbPath);
    ASSERT_TRUE(repo.has_value());

    auto u1 = repo.value()->get("wallet", "u1");
    ASSERT_TRUE(u1.has_value());
    EXPECT_EQ(u1.value()["balance"], "100");
    EXPECT_EQ(u1.value()["owner"], "Steve");

    auto u3 = repo.value()->get("wallet", "u3");
    ASSERT_TRUE(u3.has_value());
    EXPECT_EQ(u3.value()["balance"], "33"); // NULL 列被跳过

    auto e1 = repo.value()->get("events", "1001");
    ASSERT_TRUE(e1.has_value());
    EXPECT_EQ(e1.value()["event_name"], "place");

    auto keys = repo.value()->list("wallet");
    ASSERT_TRUE(keys.has_value());
    EXPECT_EQ(keys.value().size(), 3);

    // 原始库已被替换为块格式：归档留存 1 份。
    EXPECT_EQ(archiveCount(), 1);
}

TEST_F(LegacyMigratorTest, ReopenDoesNotReArchive) {
    // 首次打开完成迁移后，再次打开不应重复触发归档。
    {
        auto first = BlockRepository::open(dbPath);
        ASSERT_TRUE(first.has_value());

        auto has = first.value()->has("wallet", "u1");
        EXPECT_TRUE(has.has_value());
        EXPECT_TRUE(has.value());
    } // 作用域结束，对象析构、连接关闭

    auto second = BlockRepository::open(dbPath);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(archiveCount(), 1); // 仍只有 1 份归档
    auto u2 = second.value()->get("wallet", "u2");
    ASSERT_TRUE(u2.has_value());
    EXPECT_EQ(u2.value()["owner"], "Alex");
}

} // namespace