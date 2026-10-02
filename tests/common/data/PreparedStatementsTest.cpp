#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include <sqlite3.h>

#include <SQLiteCpp/SQLiteCpp.h>

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"
#include "LOICollectionA/data/sqlite/connection/ConnectionPool.h"
#include "LOICollectionA/data/sqlite/connection/PreparedStatements.h"

namespace prepared_statements_test_support {

    std::filesystem::path tempPreparedStatementsTestDb(std::string_view tag) {
        auto base = std::filesystem::temp_directory_path()
                    / ("loicollectiona-prepared-" + std::string(tag) + ".db");
        std::filesystem::remove(base);
        std::filesystem::remove(std::filesystem::path(base.string() + "-wal"));
        std::filesystem::remove(std::filesystem::path(base.string() + "-shm"));
        return base;
    }

    void dropPreparedStatementsTestDb(std::filesystem::path const& base) {
        std::filesystem::remove(base);
        std::filesystem::remove(std::filesystem::path(base.string() + "-wal"));
        std::filesystem::remove(std::filesystem::path(base.string() + "-shm"));
    }

    std::shared_ptr<SQLiteConnection> openPreparedStatementsTestConnection(std::filesystem::path const& base) {
        auto created = SQLiteConnection::create(base.string(), false);
        if (!created.has_value())
            return nullptr;

        return std::move(*created);
    }
}

TEST(PreparedStatementsTest, EnsureReusesStatementUntilSqlChanges) {
    auto path = prepared_statements_test_support::tempPreparedStatementsTestDb("stable");

    {
        auto repository = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repository.has_value()) << repository.error().message();
    }

    auto handle = prepared_statements_test_support::openPreparedStatementsTestConnection(path);
    ASSERT_NE(handle, nullptr);

    auto& statements = handle->statements();

    auto* first = statements.ensure("preparedStatementsTestStable", "SELECT id FROM block WHERE parent=?");
    ASSERT_NE(first, nullptr);

    auto* again = statements.ensure("preparedStatementsTestStable", "SELECT id FROM block WHERE parent=?");
    EXPECT_EQ(static_cast<void const*>(again), static_cast<void const*>(first));

    auto* changed =
        statements.ensure("preparedStatementsTestStable", "SELECT id FROM block WHERE parent=? ORDER BY id DESC");
    ASSERT_NE(changed, nullptr);
    EXPECT_NE(static_cast<void const*>(changed), static_cast<void const*>(again));

    EXPECT_EQ(statements.get("preparedStatementsTestMissingStatement"), nullptr);

    handle.reset();
    prepared_statements_test_support::dropPreparedStatementsTestDb(path);
}

TEST(PreparedStatementsTest, CatalogStatementsAreCachedToo) {
    auto path = prepared_statements_test_support::tempPreparedStatementsTestDb("catalog");

    {
        auto repository = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repository.has_value()) << repository.error().message();
    }

    auto handle = prepared_statements_test_support::openPreparedStatementsTestConnection(path);
    ASSERT_NE(handle, nullptr);

    auto& statements = handle->statements();

    auto* state = statements.get("getState");
    auto* alsoState = statements.get("getState");
    ASSERT_NE(state, nullptr);
    EXPECT_EQ(static_cast<void const*>(alsoState), static_cast<void const*>(state));

    auto* meta = statements.get("getMeta");
    ASSERT_NE(meta, nullptr);
    EXPECT_NE(static_cast<void const*>(meta), static_cast<void const*>(state));

    EXPECT_EQ(statements.get("preparedStatementsTestUnknownCatalogName"), nullptr);

    handle.reset();
    prepared_statements_test_support::dropPreparedStatementsTestDb(path);
}

TEST(PreparedStatementsTest, ResetAllowsRebindingAndRestart) {
    auto path = prepared_statements_test_support::tempPreparedStatementsTestDb("rebind");
    std::int64_t subjectId = 0;

    {
        auto repository = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repository.has_value()) << repository.error().message();
        auto& store = (*repository)->store();

        auto subject = store.createBlock(0, 0, "prepared-subject", "first");
        ASSERT_TRUE(subject.has_value());
        subjectId = subject.value();

        auto other = store.createBlock(0, 0, "prepared-other", "second");
        ASSERT_TRUE(other.has_value());
    }

    auto handle = prepared_statements_test_support::openPreparedStatementsTestConnection(path);
    ASSERT_NE(handle, nullptr);

    auto* stmt = handle->statements().ensure(
        "preparedStatementsTestByName", "SELECT id,name FROM block WHERE parent=? AND name=?"
    );
    ASSERT_NE(stmt, nullptr);

    stmt->reset();
    stmt->bind(1, static_cast<std::int64_t>(0));
    stmt->bind(2, std::string("prepared-subject"));
    ASSERT_EQ(stmt->tryExecuteStep(), SQLITE_ROW);
    EXPECT_EQ(stmt->getColumn(0).getInt64(), subjectId);
    EXPECT_EQ(stmt->getColumn(1).getString(), "prepared-subject");

    stmt->reset();
    stmt->bind(2, std::string("prepared-missing"));
    ASSERT_EQ(stmt->tryExecuteStep(), SQLITE_DONE);

    stmt->reset();
    stmt->bind(2, std::string("prepared-other"));
    ASSERT_EQ(stmt->tryExecuteStep(), SQLITE_ROW);
    EXPECT_EQ(stmt->getColumn(1).getString(), "prepared-other");

    stmt->reset();
    stmt->bind(2, std::string("prepared-subject"));
    ASSERT_EQ(stmt->tryExecuteStep(), SQLITE_ROW);
    EXPECT_EQ(stmt->getColumn(1).getString(), "prepared-subject");

    stmt->reset();
    stmt->clearBindings();
    stmt->bind(1, static_cast<std::int64_t>(0));
    stmt->bind(2, std::string("prepared-other"));
    ASSERT_EQ(stmt->tryExecuteStep(), SQLITE_ROW);
    EXPECT_EQ(stmt->getColumn(1).getString(), "prepared-other");

    stmt->reset();

    auto* reused = handle->statements().ensure(
        "preparedStatementsTestByName", "SELECT id,name FROM block WHERE parent=? AND name=?"
    );
    EXPECT_EQ(static_cast<void const*>(reused), static_cast<void const*>(stmt));
    reused->reset();
    reused->bind(1, static_cast<std::int64_t>(0));
    reused->bind(2, std::string("prepared-subject"));
    ASSERT_EQ(reused->tryExecuteStep(), SQLITE_ROW);
    EXPECT_EQ(reused->getColumn(0).getInt64(), subjectId);
    reused->reset();

    handle.reset();
    prepared_statements_test_support::dropPreparedStatementsTestDb(path);
}

TEST(PreparedStatementsTest, StatementSurvivesTableDropAndRecreate) {
    auto path = prepared_statements_test_support::tempPreparedStatementsTestDb("recreate");

    {
        auto repository = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repository.has_value()) << repository.error().message();
    }

    auto handle = prepared_statements_test_support::openPreparedStatementsTestConnection(path);
    ASSERT_NE(handle, nullptr);

    auto& db = handle->database();
    auto& statements = handle->statements();

    ASSERT_EQ(db.tryExec("CREATE TABLE preparedStatementsTestScratch(v TEXT)"), SQLITE_OK);
    ASSERT_EQ(db.tryExec("INSERT INTO preparedStatementsTestScratch(v) VALUES('first')"), SQLITE_OK);

    auto* stmt = statements.ensure("preparedStatementsTestScratch", "SELECT v FROM preparedStatementsTestScratch");
    ASSERT_NE(stmt, nullptr);

    stmt->reset();
    ASSERT_EQ(stmt->tryExecuteStep(), SQLITE_ROW);
    EXPECT_EQ(stmt->getColumn(0).getString(), "first");
    stmt->reset();

    ASSERT_EQ(db.tryExec("DROP TABLE preparedStatementsTestScratch"), SQLITE_OK);

    stmt->tryReset();
    EXPECT_NE(stmt->tryExecuteStep(), SQLITE_ROW);
    stmt->tryReset();
    EXPECT_NE(stmt->tryExecuteStep(), SQLITE_ROW);
    stmt->tryReset();

    ASSERT_EQ(db.tryExec("CREATE TABLE preparedStatementsTestScratch(v TEXT)"), SQLITE_OK);
    ASSERT_EQ(db.tryExec("INSERT INTO preparedStatementsTestScratch(v) VALUES('second')"), SQLITE_OK);

    stmt->tryReset();
    ASSERT_EQ(stmt->tryExecuteStep(), SQLITE_ROW);
    EXPECT_EQ(stmt->getColumn(0).getString(), "second");
    stmt->reset();

    EXPECT_EQ(
        static_cast<void const*>(
            statements.ensure("preparedStatementsTestScratch", "SELECT v FROM preparedStatementsTestScratch")
        ),
        static_cast<void const*>(stmt)
    );

    handle.reset();
    prepared_statements_test_support::dropPreparedStatementsTestDb(path);
}

TEST(PreparedStatementsTest, ResetAllKeepsEveryEntryUsable) {
    auto path = prepared_statements_test_support::tempPreparedStatementsTestDb("resetall");
    std::int64_t subjectId = 0;

    {
        auto repository = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repository.has_value()) << repository.error().message();
        auto& store = (*repository)->store();

        auto subject = store.createBlock(0, 0, "prepared-reset-subject", "one");
        ASSERT_TRUE(subject.has_value());
        subjectId = subject.value();

        auto sibling = store.createBlock(0, 0, "prepared-reset-sibling", "two");
        ASSERT_TRUE(sibling.has_value());
    }

    auto handle = prepared_statements_test_support::openPreparedStatementsTestConnection(path);
    ASSERT_NE(handle, nullptr);

    auto& statements = handle->statements();

    auto* state = statements.get("getState");
    auto* count = statements.get("countChildren");
    auto* meta = statements.get("getMeta");
    auto* setMeta = statements.get("setMeta");
    ASSERT_NE(state, nullptr);
    ASSERT_NE(count, nullptr);
    ASSERT_NE(meta, nullptr);
    ASSERT_NE(setMeta, nullptr);

    state->reset();
    state->bind(1, subjectId);
    ASSERT_EQ(state->tryExecuteStep(), SQLITE_ROW);
    EXPECT_EQ(state->getColumn(0).getInt(), 1);
    state->reset();

    count->reset();
    count->bind(1, static_cast<std::int64_t>(0));
    ASSERT_EQ(count->tryExecuteStep(), SQLITE_ROW);
    EXPECT_EQ(count->getColumn(0).getInt(), 2);
    count->reset();

    setMeta->reset();
    setMeta->bind(1, std::string("preparedStatementsTestMetaKey"));
    setMeta->bind(2, std::string("preparedStatementsTestMetaValue"));
    ASSERT_EQ(setMeta->tryExecuteStep(), SQLITE_DONE);
    setMeta->reset();

    meta->reset();
    meta->bind(1, std::string("preparedStatementsTestMetaKey"));
    ASSERT_EQ(meta->tryExecuteStep(), SQLITE_ROW);
    EXPECT_EQ(meta->getColumn(0).getString(), "preparedStatementsTestMetaValue");
    meta->reset();

    statements.resetAll();

    state->reset();
    state->bind(1, subjectId);
    ASSERT_EQ(state->tryExecuteStep(), SQLITE_ROW);
    EXPECT_EQ(state->getColumn(0).getInt(), 1);
    state->reset();

    count->reset();
    count->bind(1, static_cast<std::int64_t>(0));
    ASSERT_EQ(count->tryExecuteStep(), SQLITE_ROW);
    EXPECT_EQ(count->getColumn(0).getInt(), 2);
    count->reset();

    setMeta->reset();
    setMeta->bind(1, std::string("preparedStatementsTestMetaKey"));
    setMeta->bind(2, std::string("preparedStatementsTestMetaValue2"));
    ASSERT_EQ(setMeta->tryExecuteStep(), SQLITE_DONE);
    setMeta->reset();

    meta->reset();
    meta->bind(1, std::string("preparedStatementsTestMetaKey"));
    ASSERT_EQ(meta->tryExecuteStep(), SQLITE_ROW);
    EXPECT_EQ(meta->getColumn(0).getString(), "preparedStatementsTestMetaValue2");
    meta->reset();

    EXPECT_EQ(static_cast<void const*>(statements.get("getState")), static_cast<void const*>(state));
    EXPECT_EQ(static_cast<void const*>(statements.get("countChildren")), static_cast<void const*>(count));
    EXPECT_EQ(static_cast<void const*>(statements.get("getMeta")), static_cast<void const*>(meta));
    EXPECT_EQ(static_cast<void const*>(statements.get("setMeta")), static_cast<void const*>(setMeta));

    handle.reset();
    prepared_statements_test_support::dropPreparedStatementsTestDb(path);
}
