#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include "LOICollectionA/data/sqlite/block/BlockError.h"
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

TEST(ConnectionPoolTest, ActiveTransactionIsPerPoolStack) {
    auto pathA = tempConnectionPoolTestDb("A");
    auto pathB = tempConnectionPoolTestDb("B");

    {
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

        EXPECT_EQ((*poolA)->activeTransaction(), connA->get());
        EXPECT_EQ((*poolB)->activeTransaction(), connB->get());

        (*poolB)->unbindTransaction(connB->get());

        EXPECT_EQ((*poolA)->activeTransaction(), connA->get());
        EXPECT_EQ((*poolB)->activeTransaction(), nullptr);

        (*poolA)->unbindTransaction(connA->get());
        EXPECT_EQ((*poolA)->activeTransaction(), nullptr);
    }

    dropConnectionPoolTestDb(pathA);
    dropConnectionPoolTestDb(pathB);
}

TEST(ConnectionPoolTest, SamePoolRebindRoundTrips) {
    auto path = tempConnectionPoolTestDb("rebind");

    {
        auto pool = ConnectionPool::create(path.string(), 1);
        ASSERT_TRUE(pool.has_value());
        auto conn = (*pool)->acquire(5000);
        ASSERT_TRUE(conn.has_value());

        (*pool)->bindTransaction(conn->get());
        EXPECT_EQ((*pool)->activeTransaction(), conn->get());
        (*pool)->unbindTransaction(conn->get());
        EXPECT_EQ((*pool)->activeTransaction(), nullptr);
    }

    dropConnectionPoolTestDb(path);
}

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

TEST(ConnectionPoolTest, CreateRejectsZeroSize) {
    auto path = tempConnectionPoolTestDb("zerosize");

    auto pool = ConnectionPool::create(path.string(), 0);
    ASSERT_FALSE(pool.has_value());
    EXPECT_EQ(pool.error().message(), "connection pool size must be at least 1");

    dropConnectionPoolTestDb(path);
}

TEST(ConnectionPoolTest, HeldSingletonConnectionTimesOutWithPoolErrorCode) {
    auto path = tempConnectionPoolTestDb("exhausted");

    {
        auto pool = ConnectionPool::create(path.string(), 1);
        ASSERT_TRUE(pool.has_value());

        auto held = (*pool)->acquire(1000);
        ASSERT_TRUE(held.has_value());

        auto refused = (*pool)->acquire(0);
        ASSERT_FALSE(refused.has_value());
        ASSERT_TRUE(refused.error().isA<ll::ErrorCodeError>());
        EXPECT_EQ(
            refused.error().as<ll::ErrorCodeError>().ec,
            BlockError::makeErrorCode(BlockError::BlockErrorCode::PoolTimeout)
        );

        (*pool)->release(std::move(*held));
    }

    dropConnectionPoolTestDb(path);
}

TEST(ConnectionPoolTest, TryAcquireWriteWaitsUntilRelease) {
    auto path = tempConnectionPoolTestDb("writewait");
    auto pool = ConnectionPool::create(path.string(), 1);
    ASSERT_TRUE(pool.has_value());

    ASSERT_TRUE((*pool)->tryAcquireWrite(0));

    std::atomic<bool> reportedTimeout{false};
    std::atomic<bool> waitedWhileHeld{false};
    std::atomic<bool> acquiredAfterRelease{false};
    std::thread waiter([&pool, &reportedTimeout, &waitedWhileHeld, &acquiredAfterRelease] {
        waitedWhileHeld.store(!(*pool)->tryAcquireWrite(10));
        reportedTimeout.store(true);

        for (int attempt = 0; attempt < 200; ++attempt) {
            if ((*pool)->tryAcquireWrite(10)) {
                acquiredAfterRelease.store(true);
                return;
            }
        }
    });

    while (!reportedTimeout.load())
        std::this_thread::yield();

    (*pool)->releaseWrite();
    waiter.join();

    EXPECT_TRUE(waitedWhileHeld.load());
    EXPECT_TRUE(acquiredAfterRelease.load());

    (*pool)->releaseWrite();

    dropConnectionPoolTestDb(path);
}

TEST(ConnectionPoolTest, ConnectionReleasedOnAnotherThreadIsRecycled) {
    auto path = tempConnectionPoolTestDb("handoff");

    {
        auto pool = ConnectionPool::create(path.string(), 1);
        ASSERT_TRUE(pool.has_value());

        auto held = (*pool)->acquire(5000);
        ASSERT_TRUE(held.has_value());
        auto* heldPointer = held->get();

        std::atomic<bool> started{false};
        std::promise<std::shared_ptr<SQLiteConnection>> handoff;
        auto handoffFuture = handoff.get_future();
        std::thread waiter([&pool, &started, &handoff] {
            started.store(true);
            auto recycled = (*pool)->acquire(5000);
            handoff.set_value(
                recycled.has_value() ? std::shared_ptr<SQLiteConnection>(*recycled)
                                     : std::shared_ptr<SQLiteConnection>{}
            );
        });

        while (!started.load())
            std::this_thread::yield();

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        (*pool)->release(std::move(*held));

        auto recycled = handoffFuture.get();
        waiter.join();

        ASSERT_NE(recycled, nullptr);
        EXPECT_EQ(recycled.get(), heldPointer);

        (*pool)->release(std::move(recycled));
    }

    dropConnectionPoolTestDb(path);
}

TEST(ConnectionPoolTest, BindTransactionTwiceUnbindsInLifoOrder) {
    auto path = tempConnectionPoolTestDb("lifo");

    {
        auto pool = ConnectionPool::create(path.string(), 2);
        ASSERT_TRUE(pool.has_value());

        auto first = (*pool)->acquire(5000);
        auto second = (*pool)->acquire(5000);
        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());
        EXPECT_NE(first->get(), second->get());

        (*pool)->bindTransaction(first->get());
        (*pool)->bindTransaction(second->get());
        EXPECT_EQ((*pool)->activeTransaction(), second->get());

        (*pool)->unbindTransaction(second->get());
        EXPECT_NE((*pool)->activeTransaction(), nullptr);
        EXPECT_EQ((*pool)->activeTransaction(), first->get());

        (*pool)->unbindTransaction(first->get());
        EXPECT_EQ((*pool)->activeTransaction(), nullptr);

        (*pool)->release(std::move(*first));
        (*pool)->release(std::move(*second));
    }

    dropConnectionPoolTestDb(path);
}
