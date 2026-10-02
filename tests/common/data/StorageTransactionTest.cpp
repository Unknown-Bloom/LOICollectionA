#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include <sqlite3.h>

#include <SQLiteCpp/SQLiteCpp.h>

#include "LOICollectionA/data/sqlite/connection/ConnectionPool.h"
#include "LOICollectionA/data/sqlite/connection/StorageTransaction.h"

namespace storage_transaction_test_support {

    std::filesystem::path tempStorageTransactionTestDb(std::string_view tag) {
        auto base = std::filesystem::temp_directory_path()
                    / ("loicollectiona-storage-txn-" + std::string(tag) + ".db");
        std::filesystem::remove(base);
        std::filesystem::remove(std::filesystem::path(base.string() + "-wal"));
        std::filesystem::remove(std::filesystem::path(base.string() + "-shm"));
        return base;
    }

    void dropStorageTransactionTestDb(std::filesystem::path const& base) {
        std::filesystem::remove(base);
        std::filesystem::remove(std::filesystem::path(base.string() + "-wal"));
        std::filesystem::remove(std::filesystem::path(base.string() + "-shm"));
    }

    std::shared_ptr<SQLiteConnection> openStorageTransactionTestConnection(std::filesystem::path const& base) {
        auto created = SQLiteConnection::create(base.string(), false);
        if (!created.has_value())
            return nullptr;

        return std::move(*created);
    }

    bool storageTransactionTableExists(SQLite::Database& db, std::string_view table) {
        SQLite::Statement query(db, "SELECT name FROM sqlite_master WHERE type='table' AND name=?");
        query.bind(1, std::string(table));
        return query.tryExecuteStep() == SQLITE_ROW;
    }

    std::int64_t storageTransactionRowCount(SQLite::Database& db, std::string_view table) {
        SQLite::Statement query(db, "SELECT COUNT(*) FROM " + std::string(table));
        if (query.tryExecuteStep() != SQLITE_ROW)
            return -1;

        return query.getColumn(0).getInt64();
    }

    std::string storageTransactionCreateTable(std::string_view table) {
        return "CREATE TABLE " + std::string(table) + "(id INTEGER PRIMARY KEY, v TEXT)";
    }

    std::string storageTransactionInsertRow(std::string_view table, std::int64_t id, std::string_view value) {
        return "INSERT INTO " + std::string(table) + "(id,v) VALUES(" + std::to_string(id) + ",'" + std::string(value) + "')";
    }
}

TEST(StorageTransactionTest, CommitIsVisibleAndFinishIsIdempotent) {
    auto path = storage_transaction_test_support::tempStorageTransactionTestDb("commit");
    constexpr std::string_view kTable = "storage_txn_commit_table";

    {
        auto handle = storage_transaction_test_support::openStorageTransactionTestConnection(path);
        ASSERT_NE(handle, nullptr);

        auto& db = handle->database();
        ASSERT_EQ(db.tryExec(storage_transaction_test_support::storageTransactionCreateTable(kTable)), SQLITE_OK);

        StorageTransaction txn(handle, nullptr);
        ASSERT_EQ(txn.connection().get(), handle.get());

        ASSERT_EQ(
            db.tryExec(storage_transaction_test_support::storageTransactionInsertRow(kTable, 1, "kept")), SQLITE_OK
        );

        auto committed = txn.commit();
        ASSERT_TRUE(committed.has_value()) << committed.error().message();
        EXPECT_TRUE(committed.value());

        auto recommitted = txn.commit();
        ASSERT_TRUE(recommitted.has_value());
        EXPECT_FALSE(recommitted.value());

        auto rolledBack = txn.rollback();
        ASSERT_TRUE(rolledBack.has_value());
        EXPECT_FALSE(rolledBack.value());

        EXPECT_EQ(txn.connection(), nullptr);
        EXPECT_EQ(handle.use_count(), 1);
        EXPECT_TRUE(storage_transaction_test_support::storageTransactionTableExists(db, kTable));
        EXPECT_EQ(storage_transaction_test_support::storageTransactionRowCount(db, kTable), 1);
    }

    {
        auto reopened = storage_transaction_test_support::openStorageTransactionTestConnection(path);
        ASSERT_NE(reopened, nullptr);
        EXPECT_EQ(storage_transaction_test_support::storageTransactionRowCount(reopened->database(), kTable), 1);
    }

    storage_transaction_test_support::dropStorageTransactionTestDb(path);
}

TEST(StorageTransactionTest, RollbackUndoesDdlAndDmlOnSameConnection) {
    auto path = storage_transaction_test_support::tempStorageTransactionTestDb("rollback");
    constexpr std::string_view kTable = "storage_txn_rollback_table";
    constexpr std::string_view kCreated = "storage_txn_rollback_created";

    {
        auto handle = storage_transaction_test_support::openStorageTransactionTestConnection(path);
        ASSERT_NE(handle, nullptr);

        auto& db = handle->database();
        ASSERT_EQ(db.tryExec(storage_transaction_test_support::storageTransactionCreateTable(kTable)), SQLITE_OK);

        {
            StorageTransaction txn(handle, nullptr);

            ASSERT_EQ(
                db.tryExec(storage_transaction_test_support::storageTransactionInsertRow(kTable, 7, "gone")), SQLITE_OK
            );
            ASSERT_EQ(
                db.tryExec(storage_transaction_test_support::storageTransactionCreateTable(kCreated)), SQLITE_OK
            );

            EXPECT_EQ(storage_transaction_test_support::storageTransactionRowCount(db, kTable), 1);
            EXPECT_TRUE(storage_transaction_test_support::storageTransactionTableExists(db, kCreated));

            auto rolled = txn.rollback();
            ASSERT_TRUE(rolled.has_value()) << rolled.error().message();
            EXPECT_TRUE(rolled.value());
        }

        EXPECT_FALSE(storage_transaction_test_support::storageTransactionTableExists(db, kCreated));
        EXPECT_EQ(storage_transaction_test_support::storageTransactionRowCount(db, kTable), 0);
        EXPECT_EQ(db.tryExec(storage_transaction_test_support::storageTransactionInsertRow(kTable, 8, "after")), SQLITE_OK);
        EXPECT_EQ(storage_transaction_test_support::storageTransactionRowCount(db, kTable), 1);
    }

    storage_transaction_test_support::dropStorageTransactionTestDb(path);
}

TEST(StorageTransactionTest, SavepointCommitReleasesIntoOuterTransaction) {
    auto path = storage_transaction_test_support::tempStorageTransactionTestDb("savepoint-commit");
    constexpr std::string_view kTable = "storage_txn_savepoint_commit";

    {
        auto handle = storage_transaction_test_support::openStorageTransactionTestConnection(path);
        ASSERT_NE(handle, nullptr);

        auto& db = handle->database();
        ASSERT_EQ(db.tryExec(storage_transaction_test_support::storageTransactionCreateTable(kTable)), SQLITE_OK);

        {
            StorageTransaction outer(handle, nullptr);

            {
                StorageTransaction savepoint(handle, nullptr, "storage_txn_sp_x");
                ASSERT_EQ(
                    db.tryExec(storage_transaction_test_support::storageTransactionInsertRow(kTable, 1, "released")),
                    SQLITE_OK
                );

                auto released = savepoint.commit();
                ASSERT_TRUE(released.has_value()) << released.error().message();
                EXPECT_TRUE(released.value());
                EXPECT_EQ(savepoint.connection(), nullptr);
            }

            EXPECT_EQ(handle.use_count(), 2);
            EXPECT_EQ(storage_transaction_test_support::storageTransactionRowCount(db, kTable), 1);

            auto committed = outer.commit();
            ASSERT_TRUE(committed.has_value()) << committed.error().message();
            EXPECT_TRUE(committed.value());
        }

        EXPECT_EQ(handle.use_count(), 1);
        EXPECT_EQ(storage_transaction_test_support::storageTransactionRowCount(db, kTable), 1);
    }

    storage_transaction_test_support::dropStorageTransactionTestDb(path);
}

TEST(StorageTransactionTest, SavepointCommitStaysInsideOuterTransaction) {
    auto path = storage_transaction_test_support::tempStorageTransactionTestDb("savepoint-outer");
    constexpr std::string_view kTable = "storage_txn_savepoint_outer";

    {
        auto handle = storage_transaction_test_support::openStorageTransactionTestConnection(path);
        ASSERT_NE(handle, nullptr);

        auto& db = handle->database();
        ASSERT_EQ(db.tryExec(storage_transaction_test_support::storageTransactionCreateTable(kTable)), SQLITE_OK);

        {
            StorageTransaction outer(handle, nullptr);

            {
                StorageTransaction savepoint(handle, nullptr, "storage_txn_sp_outer");
                ASSERT_EQ(
                    db.tryExec(storage_transaction_test_support::storageTransactionInsertRow(kTable, 1, "discarded")),
                    SQLITE_OK
                );
                ASSERT_TRUE(savepoint.commit().has_value());
            }

            EXPECT_EQ(storage_transaction_test_support::storageTransactionRowCount(db, kTable), 1);

            auto rolled = outer.rollback();
            ASSERT_TRUE(rolled.has_value()) << rolled.error().message();
            EXPECT_TRUE(rolled.value());
        }

        EXPECT_EQ(storage_transaction_test_support::storageTransactionRowCount(db, kTable), 0);
    }

    storage_transaction_test_support::dropStorageTransactionTestDb(path);
}

TEST(StorageTransactionTest, SavepointRollbackKeepsOuterWrites) {
    auto path = storage_transaction_test_support::tempStorageTransactionTestDb("savepoint-rollback");
    constexpr std::string_view kTable = "storage_txn_savepoint_rollback";

    {
        auto handle = storage_transaction_test_support::openStorageTransactionTestConnection(path);
        ASSERT_NE(handle, nullptr);

        auto& db = handle->database();
        ASSERT_EQ(db.tryExec(storage_transaction_test_support::storageTransactionCreateTable(kTable)), SQLITE_OK);

        {
            StorageTransaction outer(handle, nullptr);
            ASSERT_EQ(
                db.tryExec(storage_transaction_test_support::storageTransactionInsertRow(kTable, 1, "outer")), SQLITE_OK
            );

            {
                StorageTransaction savepoint(handle, nullptr, "storage_txn_sp_inner");
                ASSERT_EQ(
                    db.tryExec(storage_transaction_test_support::storageTransactionInsertRow(kTable, 2, "inner")),
                    SQLITE_OK
                );
                EXPECT_EQ(storage_transaction_test_support::storageTransactionRowCount(db, kTable), 2);

                auto rolled = savepoint.rollback();
                ASSERT_TRUE(rolled.has_value()) << rolled.error().message();
                EXPECT_TRUE(rolled.value());
            }

            EXPECT_EQ(storage_transaction_test_support::storageTransactionRowCount(db, kTable), 1);

            auto committed = outer.commit();
            ASSERT_TRUE(committed.has_value()) << committed.error().message();
            EXPECT_TRUE(committed.value());
        }

        EXPECT_EQ(storage_transaction_test_support::storageTransactionRowCount(db, kTable), 1);
    }

    storage_transaction_test_support::dropStorageTransactionTestDb(path);
}

TEST(StorageTransactionTest, SavepointsOnSeparateConnectionsAreIndependent) {
    auto pathA = storage_transaction_test_support::tempStorageTransactionTestDb("independent-a");
    auto pathB = storage_transaction_test_support::tempStorageTransactionTestDb("independent-b");
    constexpr std::string_view kTable = "storage_txn_independent_table";

    {
        auto handleA = storage_transaction_test_support::openStorageTransactionTestConnection(pathA);
        auto handleB = storage_transaction_test_support::openStorageTransactionTestConnection(pathB);
        ASSERT_NE(handleA, nullptr);
        ASSERT_NE(handleB, nullptr);

        auto& dbA = handleA->database();
        auto& dbB = handleB->database();
        ASSERT_EQ(dbA.tryExec(storage_transaction_test_support::storageTransactionCreateTable(kTable)), SQLITE_OK);
        ASSERT_EQ(dbB.tryExec(storage_transaction_test_support::storageTransactionCreateTable(kTable)), SQLITE_OK);

        StorageTransaction outerA(handleA, nullptr);
        StorageTransaction outerB(handleB, nullptr);

        {
            StorageTransaction savepointA(handleA, nullptr, "storage_txn_sp_a");
            ASSERT_EQ(
                dbA.tryExec(storage_transaction_test_support::storageTransactionInsertRow(kTable, 1, "rollbackA")),
                SQLITE_OK
            );

            auto rolled = savepointA.rollback();
            ASSERT_TRUE(rolled.has_value()) << rolled.error().message();
            EXPECT_TRUE(rolled.value());
        }

        {
            StorageTransaction savepointB(handleB, nullptr, "storage_txn_sp_b");
            ASSERT_EQ(
                dbB.tryExec(storage_transaction_test_support::storageTransactionInsertRow(kTable, 2, "commitB")),
                SQLITE_OK
            );

            auto released = savepointB.commit();
            ASSERT_TRUE(released.has_value()) << released.error().message();
            EXPECT_TRUE(released.value());
        }

        EXPECT_EQ(storage_transaction_test_support::storageTransactionRowCount(dbA, kTable), 0);
        EXPECT_EQ(storage_transaction_test_support::storageTransactionRowCount(dbB, kTable), 1);

        ASSERT_TRUE(outerA.commit().has_value());
        ASSERT_TRUE(outerB.commit().has_value());
    }

    {
        auto reopenedA = storage_transaction_test_support::openStorageTransactionTestConnection(pathA);
        auto reopenedB = storage_transaction_test_support::openStorageTransactionTestConnection(pathB);
        ASSERT_NE(reopenedA, nullptr);
        ASSERT_NE(reopenedB, nullptr);
        EXPECT_EQ(storage_transaction_test_support::storageTransactionRowCount(reopenedA->database(), kTable), 0);
        EXPECT_EQ(storage_transaction_test_support::storageTransactionRowCount(reopenedB->database(), kTable), 1);
    }

    storage_transaction_test_support::dropStorageTransactionTestDb(pathA);
    storage_transaction_test_support::dropStorageTransactionTestDb(pathB);
}

TEST(StorageTransactionTest, NullPoolKeepsCallerConnectionAliveAfterFinish) {
    auto path = storage_transaction_test_support::tempStorageTransactionTestDb("nullpool");
    constexpr std::string_view kTable = "storage_txn_null_pool_table";

    {
        auto handle = storage_transaction_test_support::openStorageTransactionTestConnection(path);
        ASSERT_NE(handle, nullptr);

        auto& db = handle->database();
        ASSERT_EQ(db.tryExec(storage_transaction_test_support::storageTransactionCreateTable(kTable)), SQLITE_OK);

        {
            StorageTransaction txn(handle, nullptr);
            ASSERT_EQ(handle.use_count(), 2);
            ASSERT_EQ(
                db.tryExec(storage_transaction_test_support::storageTransactionInsertRow(kTable, 1, "committed")),
                SQLITE_OK
            );

            auto committed = txn.commit();
            ASSERT_TRUE(committed.has_value()) << committed.error().message();
            EXPECT_TRUE(committed.value());
            EXPECT_EQ(txn.connection(), nullptr);
        }

        EXPECT_EQ(handle.use_count(), 1);
        EXPECT_EQ(storage_transaction_test_support::storageTransactionRowCount(db, kTable), 1);

        {
            StorageTransaction txn(handle, nullptr);
            ASSERT_EQ(
                db.tryExec(storage_transaction_test_support::storageTransactionInsertRow(kTable, 2, "rolled")), SQLITE_OK
            );
            ASSERT_TRUE(txn.rollback().value());
        }

        EXPECT_EQ(handle.use_count(), 1);
        EXPECT_EQ(storage_transaction_test_support::storageTransactionRowCount(db, kTable), 1);

        {
            StorageTransaction txn(handle, nullptr);
            ASSERT_EQ(
                db.tryExec(storage_transaction_test_support::storageTransactionInsertRow(kTable, 3, "destructed")),
                SQLITE_OK
            );
        }

        EXPECT_EQ(handle.use_count(), 1);
        EXPECT_EQ(storage_transaction_test_support::storageTransactionRowCount(db, kTable), 1);
        EXPECT_NE(handle, nullptr);
        EXPECT_EQ(db.execAndGet("SELECT 1").getInt(), 1);
    }

    storage_transaction_test_support::dropStorageTransactionTestDb(path);
}
