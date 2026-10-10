#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include <sqlite3.h>

#include <SQLiteCpp/SQLiteCpp.h>

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"
#include "LOICollectionA/data/sqlite/connection/ConnectionPool.h"

#include "common/data/StorageTestSupport.h"

using storage_test_support::dropDb;
using storage_test_support::tempDb;

namespace fault_injection_test_support {
    void writeFile(std::filesystem::path const& path, std::string const& content) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << content;
    }

    void corruptHeader(std::filesystem::path const& path) {
        std::fstream io(path, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(io.is_open());

        std::string junk(16, '\x5A');
        io.seekp(0);
        io.write(junk.data(), junk.size());
    }
}

TEST(FaultInjectionTest, OpenRejectsPathInsideMissingDirectory) {
    auto path = std::filesystem::temp_directory_path()
        / "loicollectiona-fault-missing-dir-xyz"
        / "missing.db";

    auto repo = BlockRepository::open(path.string(), 2);
    ASSERT_FALSE(repo.has_value());
    EXPECT_FALSE(repo.error().message().empty());
}

TEST(FaultInjectionTest, OpenRejectsGarbageFile) {
    auto path = tempDb("fault", "garbage");
    fault_injection_test_support::writeFile(path, "this file is definitely not a sqlite database");

    auto repo = BlockRepository::open(path.string(), 2);
    ASSERT_FALSE(repo.has_value());
    EXPECT_FALSE(repo.error().message().empty());
    dropDb(path);
}

TEST(FaultInjectionTest, OpenRejectsCorruptedHeader) {
    auto path = tempDb("fault", "corrupt-header");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        ASSERT_TRUE((*repo)->store().createBlock(0, 1, "seed").has_value());
    }

    fault_injection_test_support::corruptHeader(path);

    auto repo = BlockRepository::open(path.string(), 2);
    ASSERT_FALSE(repo.has_value());
    EXPECT_FALSE(repo.error().message().empty());
    dropDb(path);
}

TEST(FaultInjectionTest, ReadOnlyConnectionCannotOpenMissingFile) {
    auto path = tempDb("fault", "readonly-missing");

    auto conn = SQLiteConnection::create(path.string(), true);
    ASSERT_FALSE(conn.has_value());
    EXPECT_FALSE(conn.error().message().empty());
    dropDb(path);
}

TEST(FaultInjectionTest, ReadOnlyConnectionServesReadsAndRejectsWrites) {
    auto path = tempDb("fault", "readonly-writes");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        ASSERT_TRUE((*repo)->store().metaSet("seed", "value").has_value());
    }

    {
        auto conn = SQLiteConnection::create(path.string(), true);
        ASSERT_TRUE(conn.has_value()) << conn.error().message();

        SQLite::Statement query((*conn)->database(), "SELECT value FROM meta WHERE key='seed'");
        ASSERT_EQ(query.tryExecuteStep(), SQLITE_ROW);
        EXPECT_EQ(query.getColumn(0).getString(), "value");

        SQLite::Statement insert((*conn)->database(),
            "INSERT INTO meta(key, value) VALUES('other', 'value')");
        EXPECT_EQ(insert.tryExecuteStep(), SQLITE_READONLY);
    }
    dropDb(path);
}
