#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

#include "LOICollectionA/data/sqlite/connection/ConnectionPool.h"

namespace {

    std::filesystem::path tempConnectionPoolTestDb(std::string_view tag) {
        auto base = std::filesystem::temp_directory_path()
                    / ("loicollectiona-pool-" + std::string(tag) + ".db");
        std::filesystem::remove(base);
        std::filesystem::remove(std::filesystem::path(base.string() + "-wal"));
        std::filesystem::remove(std::filesystem::path(base.string() + "-shm"));
        return base;
    }

    void dropConnectionPoolTestDb(std::filesystem::path const& base) {
        std::filesystem::remove(base);
        std::filesystem::remove(std::filesystem::path(base.string() + "-wal"));
        std::filesystem::remove(std::filesystem::path(base.string() + "-shm"));
    }

}

// §3: the active-transaction binding must be a per-pool stack, not a single slot
// shared by every pool on the thread. With two pools open on the same thread,
// binding a transaction on B must not clobber A's binding, and ending B's
// transaction must restore A's instead of clearing it forever.
TEST(ConnectionPoolTest, ActiveTransactionIsPerPoolStack) {
    auto pathA = tempConnectionPoolTestDb("A");
    auto pathB = tempConnectionPoolTestDb("B");
    auto poolA = ConnectionPool::create(pathA.string(), 1);
    auto poolB = ConnectionPool::create(pathB.string(), 1);
    ASSERT_TRUE(poolA.has_value());
    ASSERT_TRUE(poolB.has_value());

    auto connA = (*poolA)->acquire(5000);
    auto connB = (*poolB)->acquire(5000);
    ASSERT_TRUE(connA.has_value());
    ASSERT_TRUE(connB.has_value());

    EXPECT_EQ((*poolA)->activeTransaction(), nullptr);
    EXPECT_EQ((*poolB)->activeTransaction(), nullptr);

    (*poolA)->bindTransaction(connA->get());
    EXPECT_EQ((*poolA)->activeTransaction(), connA->get());
    EXPECT_EQ((*poolB)->activeTransaction(), nullptr);

    (*poolB)->bindTransaction(connB->get());
    // Both stay visible: B did not overwrite A's entry.
    EXPECT_EQ((*poolA)->activeTransaction(), connA->get());
    EXPECT_EQ((*poolB)->activeTransaction(), connB->get());

    (*poolB)->unbindTransaction(connB->get());
    // A's binding survives B ending.
    EXPECT_EQ((*poolA)->activeTransaction(), connA->get());
    EXPECT_EQ((*poolB)->activeTransaction(), nullptr);

    (*poolA)->unbindTransaction(connA->get());
    EXPECT_EQ((*poolA)->activeTransaction(), nullptr);

    dropConnectionPoolTestDb(pathA);
    dropConnectionPoolTestDb(pathB);
}

// Re-binding and unbinding the same pool must round-trip cleanly.
TEST(ConnectionPoolTest, SamePoolRebindRoundTrips) {
    auto path = tempConnectionPoolTestDb("rebind");
    auto pool = ConnectionPool::create(path.string(), 1);
    ASSERT_TRUE(pool.has_value());
    auto conn = (*pool)->acquire(5000);
    ASSERT_TRUE(conn.has_value());

    (*pool)->bindTransaction(conn->get());
    EXPECT_EQ((*pool)->activeTransaction(), conn->get());
    (*pool)->unbindTransaction(conn->get());
    EXPECT_EQ((*pool)->activeTransaction(), nullptr);

    dropConnectionPoolTestDb(path);
}

// §2 (I2): the pool-level write lock is mutually exclusive. A second acquire
// while held must be denied immediately rather than succeeding.
TEST(ConnectionPoolTest, WriteLockIsMutuallyExclusive) {
    auto path = tempConnectionPoolTestDb("writelock");
    auto pool = ConnectionPool::create(path.string(), 1);
    ASSERT_TRUE(pool.has_value());

    EXPECT_TRUE((*pool)->tryAcquireWrite(0));
    EXPECT_FALSE((*pool)->tryAcquireWrite(0));
    (*pool)->releaseWrite();
    EXPECT_TRUE((*pool)->tryAcquireWrite(0));
    (*pool)->releaseWrite();

    dropConnectionPoolTestDb(path);
}
