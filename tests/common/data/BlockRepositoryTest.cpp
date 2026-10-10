#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"
#include "LOICollectionA/data/sqlite/block/BlockStore.h"
#include "LOICollectionA/data/sqlite/block/ColumnText.h"
#include "LOICollectionA/data/sqlite/connection/ConnectionPool.h"

#include "common/data/StorageTestSupport.h"

using storage_test_support::dropDb;
using storage_test_support::tempDb;

namespace block_repository_test_support {
    std::vector<std::string> tableNames(BlockRepository& repo) {
        std::vector<std::string> names;
        auto queried = repo.store().withQuery(
            "tableNames", "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name", {},
            [&](SQLite::Statement& stmt) { names.push_back(columnToString(stmt.getColumn(0))); });
        if (!queried.has_value())
            return {};

        return names;
    }
}

TEST(BlockRepositoryTest, OpenCreatesCoreTables) {
    auto path = tempDb("repository", "core-tables");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto names = block_repository_test_support::tableNames(**repo);
        ASSERT_NE(names.size(), 0u);

        for (auto const& wanted : {"block", "prop", "link", "dict", "meta"})
            EXPECT_NE(std::find(names.begin(), names.end(), wanted), names.end())
                << "missing table " << wanted;
    }
    dropDb(path);
}

TEST(BlockRepositoryTest, ReopenOnSameFileKeepsSchema) {
    auto path = tempDb("repository", "reopen");

    {
        auto first = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(first.has_value()) << first.error().message();
        auto created = (*first)->store().createBlock(0, 3, "alpha");
        ASSERT_TRUE(created.has_value()) << created.error().message();
    }

    {
        auto second = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(second.has_value()) << second.error().message();

        auto id = (*second)->store().idOf(0, "alpha");
        ASSERT_TRUE(id.has_value());
        ASSERT_TRUE(id.value().has_value());
    }
    dropDb(path);
}

TEST(BlockRepositoryTest, OpenRejectsMissingParentDirectory) {
    auto repo = BlockRepository::open("/loicollectiona-missing-dir/a.db", 2);
    ASSERT_FALSE(repo.has_value());
    EXPECT_FALSE(repo.error().message().empty());
}

TEST(BlockRepositoryTest, OpenRejectsGarbageFile) {
    auto path = tempDb("repository", "garbage");

    {
        std::ofstream out(path, std::ios::binary);
        out << "this is definitely not a sqlite database";
    }

    auto repo = BlockRepository::open(path.string(), 2);
    ASSERT_FALSE(repo.has_value());
    EXPECT_FALSE(repo.error().message().empty());
    dropDb(path);
}

TEST(BlockRepositoryTest, FromStoreRejectsNullStore) {
    auto repo = BlockRepository::fromStore(nullptr);
    ASSERT_FALSE(repo.has_value());
    EXPECT_EQ(repo.error().message(), "null block store");
}

TEST(BlockRepositoryTest, FromStoreWrapsProvidedStore) {
    auto path = tempDb("repository", "from-store");

    {
        auto pool = ConnectionPool::create(path.string(), 1);
        ASSERT_TRUE(pool.has_value()) << pool.error().message();

        auto store = BlockStore::create(*pool);
        ASSERT_TRUE(store.has_value()) << store.error().message();

        auto repo = BlockRepository::fromStore(std::move(*store));
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto created = (*repo)->store().createBlock(0, 9, "wrapped");
        ASSERT_TRUE(created.has_value()) << created.error().message();

        auto loaded = (*repo)->store().load(0, "wrapped");
        ASSERT_TRUE(loaded.has_value()) << loaded.error().message();
        EXPECT_EQ(loaded.value().kind, 9);
    }
    dropDb(path);
}

TEST(BlockRepositoryTest, MetaRoundTripsThroughRepository) {
    auto path = tempDb("repository", "meta");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto missing = (*repo)->metaGet("absent");
        ASSERT_TRUE(missing.has_value()) << missing.error().message();
        EXPECT_FALSE(missing.value().has_value());

        ASSERT_TRUE((*repo)->metaSet("theme", "dark").has_value());
        auto stored = (*repo)->metaGet("theme");
        ASSERT_TRUE(stored.has_value()) << stored.error().message();
        ASSERT_TRUE(stored.value().has_value());
        EXPECT_EQ(*stored.value(), "dark");

        ASSERT_TRUE((*repo)->metaSet("theme", "light").has_value());
        auto overwritten = (*repo)->metaGet("theme");
        ASSERT_TRUE(overwritten.has_value()) << overwritten.error().message();
        ASSERT_TRUE(overwritten.value().has_value());
        EXPECT_EQ(*overwritten.value(), "light");

        ASSERT_TRUE((*repo)->metaDel("theme").has_value());
        auto removed = (*repo)->metaGet("theme");
        ASSERT_TRUE(removed.has_value()) << removed.error().message();
        EXPECT_FALSE(removed.value().has_value());

        ASSERT_TRUE((*repo)->metaDel("theme").has_value());
    }
    dropDb(path);
}

TEST(BlockRepositoryTest, MetaPersistsAcrossReopen) {
    auto path = tempDb("repository", "meta-persist");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        ASSERT_TRUE((*repo)->metaSet("score", "99").has_value());
    }

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto stored = (*repo)->metaGet("score");
        ASSERT_TRUE(stored.has_value()) << stored.error().message();
        ASSERT_TRUE(stored.value().has_value());
        EXPECT_EQ(*stored.value(), "99");
    }
    dropDb(path);
}

TEST(BlockRepositoryTest, ExecRunsSqlAndRejectsInvalidStatements) {
    auto path = tempDb("repository", "exec");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        ASSERT_TRUE((*repo)->exec("CREATE TABLE scratch(value TEXT)").has_value());
        ASSERT_TRUE((*repo)->exec("INSERT INTO scratch VALUES('a')").has_value());

        int rows = 0;
        auto queried = (*repo)->store().withQuery(
            "scratchRows", "SELECT value FROM scratch", {},
            [&](SQLite::Statement& stmt) {
                ++rows;
                EXPECT_EQ(columnToString(stmt.getColumn(0)), "a");
            });
        ASSERT_TRUE(queried.has_value()) << queried.error().message();
        EXPECT_EQ(rows, 1);

        auto broken = (*repo)->exec("THIS IS NOT SQL");
        ASSERT_FALSE(broken.has_value());
        EXPECT_FALSE(broken.error().message().empty());
    }
    dropDb(path);
}
