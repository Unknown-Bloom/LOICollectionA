#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"
#include "LOICollectionA/data/sqlite/block/WriteBatch.h"

namespace {

    std::filesystem::path tempDb(std::string_view tag) {
        auto base = std::filesystem::temp_directory_path()
                    / ("loicollectiona-batch-" + std::string(tag) + ".db");
        std::filesystem::remove(base);
        std::filesystem::remove(std::filesystem::path(base.string() + "-wal"));
        std::filesystem::remove(std::filesystem::path(base.string() + "-shm"));
        return base;
    }

    void dropDb(std::filesystem::path const& base) {
        std::filesystem::remove(base);
        std::filesystem::remove(std::filesystem::path(base.string() + "-wal"));
        std::filesystem::remove(std::filesystem::path(base.string() + "-shm"));
    }

}

// A nested savepoint rollback must not destroy the enclosing transaction: the
// outer write survives and the inner write is gone from both the database and
// the process cache.
TEST(WriteBatchTest, NestedRollbackKeepsOuterWrite) {
    auto path = tempDb("nested");
    auto repo = BlockRepository::open(path.string(), 4);
    ASSERT_TRUE(repo.has_value()) << (repo.has_value() ? "" : repo.error().message());
    auto& store = (*repo)->store();

    auto outer = WriteBatch::begin(store);
    ASSERT_TRUE(outer.has_value());
    auto io = (*outer)->append(0, 0, "outer-row", "x");
    ASSERT_TRUE(io.has_value());

    auto inner = WriteBatch::begin(store);
    ASSERT_TRUE(inner.has_value());
    auto ii = (*inner)->append(0, 0, "inner-row", "y");
    ASSERT_TRUE(ii.has_value());

    ASSERT_TRUE((*inner)->rollback().has_value());
    ASSERT_TRUE((*outer)->commit().has_value());

    EXPECT_TRUE(store.load(0, "outer-row").has_value());
    EXPECT_FALSE(store.load(0, "inner-row").has_value());

    dropDb(path);
}

// A row read inside its own transaction must not survive a rollback as a cached
// phantom (I3: uncommitted data must not escape the transaction into the
// process cache).
TEST(WriteBatchTest, UncommittedReadIsNotCachedAfterRollback) {
    auto path = tempDb("phantom");
    auto repo = BlockRepository::open(path.string(), 4);
    ASSERT_TRUE(repo.has_value()) << (repo.has_value() ? "" : repo.error().message());
    auto& store = (*repo)->store();

    auto outer = WriteBatch::begin(store);
    ASSERT_TRUE(outer.has_value());
    auto io = (*outer)->append(0, 0, "keep", "x");
    ASSERT_TRUE(io.has_value());

    auto inner = WriteBatch::begin(store);
    ASSERT_TRUE(inner.has_value());
    auto ii = (*inner)->append(0, 0, "gone", "y");
    ASSERT_TRUE(ii.has_value());

    EXPECT_TRUE(store.load(0, "gone").has_value());

    ASSERT_TRUE((*inner)->rollback().has_value());
    EXPECT_FALSE(store.load(0, "gone").has_value());

    ASSERT_TRUE((*outer)->rollback().has_value());
    EXPECT_FALSE(store.load(0, "keep").has_value());

    dropDb(path);
}

// §3: two pools on the same thread with overlapping transactions. Opening a
// transaction on the second pool must not break the first pool's connection
// affinity, and ending the second must restore the first instead of clearing it.
TEST(WriteBatchTest, TwoPoolOverlappingTransactions) {
    auto pathA = tempDb("two-a");
    auto pathB = tempDb("two-b");
    auto repoA = BlockRepository::open(pathA.string(), 4);
    auto repoB = BlockRepository::open(pathB.string(), 4);
    ASSERT_TRUE(repoA.has_value()) << (repoA.has_value() ? "" : repoA.error().message());
    ASSERT_TRUE(repoB.has_value()) << (repoB.has_value() ? "" : repoB.error().message());
    auto& storeA = (*repoA)->store();
    auto& storeB = (*repoB)->store();

    auto a = WriteBatch::begin(storeA);
    ASSERT_TRUE(a.has_value());
    auto ia = (*a)->append(0, 0, "ownA", "x");
    ASSERT_TRUE(ia.has_value());

    auto b = WriteBatch::begin(storeB);
    ASSERT_TRUE(b.has_value());
    // A's own uncommitted write must still be visible through A's connection.
    EXPECT_TRUE(storeA.load(0, "ownA").has_value());

    ASSERT_TRUE((*b)->rollback().has_value());
    // After B ends, A's binding is restored, not cleared.
    EXPECT_TRUE(storeA.load(0, "ownA").has_value());

    ASSERT_TRUE((*a)->commit().has_value());

    dropDb(pathA);
    dropDb(pathB);
}

// Many threads opening write transactions on the same file must serialize
// through the pool write lock and never fail with SQLITE_BUSY, and must not
// deadlock. (§2, I2.)
TEST(WriteBatchTest, ConcurrentWritesDoNotDeadlockOrBusy) {
    auto path = tempDb("concurrent");
    auto repo = BlockRepository::open(path.string(), 4);
    ASSERT_TRUE(repo.has_value()) << (repo.has_value() ? "" : repo.error().message());
    auto& store = (*repo)->store();

    constexpr int kThreads = 8;
    constexpr int kPerThread = 10;
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&store, i, &failures] {
            for (int k = 0; k < kPerThread; ++k) {
                auto w = WriteBatch::begin(store);
                if (!w.has_value()) {
                    ++failures;
                    return;
                }
                auto id = (*w)->append(0, 0, "t" + std::to_string(i) + "_" + std::to_string(k), "p");
                if (!id.has_value()) {
                    ++failures;
                    return;
                }
                if (!(*w)->commit().has_value()) {
                    ++failures;
                    return;
                }
            }
        });
    }
    for (auto& t : threads)
        t.join();
    EXPECT_EQ(failures.load(), 0);

    dropDb(path);
}

// cbfc8f1c2: each set must write to its own column. Writing distinct properties
// to the same block must not collide through the prepared-statement cache and
// leave the wrong value in either column.
TEST(WriteBatchTest, PropsWriteDistinctColumns) {
    auto path = tempDb("props");
    BlockId id = 0;
    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << (repo.has_value() ? "" : repo.error().message());
        auto& store = (*repo)->store();

        auto root = store.createBlock(0, 0, "root");
        ASSERT_TRUE(root.has_value());
        auto block = store.createBlock(root.value(), 0, "subject");
        ASSERT_TRUE(block.has_value());
        id = block.value();

        ASSERT_TRUE(store.setProp(id, 1, std::string("alpha")).has_value());
        ASSERT_TRUE(store.setProp(id, 2, std::string("beta")).has_value());
    }
    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << (repo.has_value() ? "" : repo.error().message());
        auto byId = (*repo)->store().load(id);
        ASSERT_TRUE(byId.has_value()) << byId.error().message();
        std::optional<std::string> first;
        std::optional<std::string> second;
        for (auto const& p : byId.value().props) {
            if (p.key == 1)
                first = p.textValue;
            if (p.key == 2)
                second = p.textValue;
        }
        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(*first, "alpha");
        EXPECT_EQ(*second, "beta");
    }
    dropDb(path);
}
