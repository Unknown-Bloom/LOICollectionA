#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"
#include "LOICollectionA/data/sqlite/block/WriteBatch.h"

namespace {

    std::filesystem::path tempWriteBatchTestDb(std::string_view tag) {
        auto base = std::filesystem::temp_directory_path()
                    / ("loicollectiona-batch-" + std::string(tag) + ".db");
        std::filesystem::remove(base);
        std::filesystem::remove(std::filesystem::path(base.string() + "-wal"));
        std::filesystem::remove(std::filesystem::path(base.string() + "-shm"));
        return base;
    }

    void dropWriteBatchTestDb(std::filesystem::path const& base) {
        std::filesystem::remove(base);
        std::filesystem::remove(std::filesystem::path(base.string() + "-wal"));
        std::filesystem::remove(std::filesystem::path(base.string() + "-shm"));
    }

}

namespace write_batch_payload_test_support {

    std::string writeBatchPayloadText(std::vector<std::byte> const& payload) {
        if (payload.empty())
            return {};

        return std::string(reinterpret_cast<char const*>(payload.data()), payload.size());
    }

}

TEST(WriteBatchTest, NestedRollbackKeepsOuterWrite) {
    auto path = tempWriteBatchTestDb("nested");

    {
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
    }

    dropWriteBatchTestDb(path);
}

TEST(WriteBatchTest, UncommittedReadIsNotCachedAfterRollback) {
    auto path = tempWriteBatchTestDb("phantom");

    {
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
    }

    dropWriteBatchTestDb(path);
}

TEST(WriteBatchTest, TwoPoolOverlappingTransactions) {
    auto pathA = tempWriteBatchTestDb("two-a");
    auto pathB = tempWriteBatchTestDb("two-b");

    {
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

        EXPECT_TRUE(storeA.load(0, "ownA").has_value());

        ASSERT_TRUE((*b)->rollback().has_value());

        EXPECT_TRUE(storeA.load(0, "ownA").has_value());

        ASSERT_TRUE((*a)->commit().has_value());
    }

    dropWriteBatchTestDb(pathA);
    dropWriteBatchTestDb(pathB);
}

TEST(WriteBatchTest, ConcurrentWritesDoNotDeadlockOrBusy) {
    auto path = tempWriteBatchTestDb("concurrent");

    {
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
    }

    dropWriteBatchTestDb(path);
}

TEST(WriteBatchTest, PropsWriteDistinctColumns) {
    auto path = tempWriteBatchTestDb("props");
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
    dropWriteBatchTestDb(path);
}

TEST(WriteBatchTest, UncommittedPayloadDoesNotLeakIntoProcessCache) {
    auto path = tempWriteBatchTestDb("payload-cache");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << (repo.has_value() ? "" : repo.error().message());
        auto& store = (*repo)->store();

        auto created = store.createBlock(0, 0, "payload-cache-subject", "v1");
        ASSERT_TRUE(created.has_value());
        auto id = created.value();

        auto committed = store.load(id);
        ASSERT_TRUE(committed.has_value());
        EXPECT_EQ(write_batch_payload_test_support::writeBatchPayloadText(committed.value().payload), "v1");

        auto batch = WriteBatch::begin(store);
        ASSERT_TRUE(batch.has_value());
        ASSERT_TRUE((*batch)->setPayload(id, "v2").has_value());

        auto inside = store.load(id);
        ASSERT_TRUE(inside.has_value());
        EXPECT_EQ(write_batch_payload_test_support::writeBatchPayloadText(inside.value().payload), "v2");

        auto insideByName = store.load(0, "payload-cache-subject");
        ASSERT_TRUE(insideByName.has_value());
        EXPECT_EQ(write_batch_payload_test_support::writeBatchPayloadText(insideByName.value().payload), "v2");

        ASSERT_TRUE((*batch)->rollback().has_value());

        auto after = store.load(id);
        ASSERT_TRUE(after.has_value());
        EXPECT_EQ(write_batch_payload_test_support::writeBatchPayloadText(after.value().payload), "v1");

        auto afterByName = store.load(0, "payload-cache-subject");
        ASSERT_TRUE(afterByName.has_value());
        EXPECT_EQ(write_batch_payload_test_support::writeBatchPayloadText(afterByName.value().payload), "v1");
    }

    dropWriteBatchTestDb(path);
}

TEST(WriteBatchTest, ThreeLevelSavepointsKeepCommittedLevels) {
    auto path = tempWriteBatchTestDb("three-level");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << (repo.has_value() ? "" : repo.error().message());
        auto& store = (*repo)->store();

        auto outer = WriteBatch::begin(store);
        ASSERT_TRUE(outer.has_value());
        ASSERT_TRUE((*outer)->append(0, 0, "three-level-1", "p1").has_value());

        auto mid = WriteBatch::begin(store);
        ASSERT_TRUE(mid.has_value());
        ASSERT_TRUE((*mid)->append(0, 0, "three-level-2", "p2").has_value());

        auto inner = WriteBatch::begin(store);
        ASSERT_TRUE(inner.has_value());
        ASSERT_TRUE((*inner)->append(0, 0, "three-level-3", "p3").has_value());

        EXPECT_TRUE(store.load(0, "three-level-3").has_value());

        ASSERT_TRUE((*inner)->rollback().has_value());
        EXPECT_FALSE(store.load(0, "three-level-3").has_value());

        ASSERT_TRUE((*mid)->commit().has_value());
        ASSERT_TRUE((*outer)->commit().has_value());

        EXPECT_TRUE(store.load(0, "three-level-1").has_value());
        EXPECT_TRUE(store.load(0, "three-level-2").has_value());
        EXPECT_FALSE(store.load(0, "three-level-3").has_value());
    }

    dropWriteBatchTestDb(path);
}

TEST(WriteBatchTest, MiddleLevelSavepointRollbackDiscardsNewerLevels) {
    auto path = tempWriteBatchTestDb("middle-level");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << (repo.has_value() ? "" : repo.error().message());
        auto& store = (*repo)->store();

        auto outer = WriteBatch::begin(store);
        ASSERT_TRUE(outer.has_value());
        ASSERT_TRUE((*outer)->append(0, 0, "middle-level-1", "p1").has_value());

        auto mid = WriteBatch::begin(store);
        ASSERT_TRUE(mid.has_value());
        ASSERT_TRUE((*mid)->append(0, 0, "middle-level-2", "p2").has_value());

        {
            auto inner = WriteBatch::begin(store);
            ASSERT_TRUE(inner.has_value());
            ASSERT_TRUE((*inner)->append(0, 0, "middle-level-3", "p3").has_value());

            EXPECT_TRUE(store.load(0, "middle-level-3").has_value());

            ASSERT_TRUE((*mid)->rollback().has_value());
        }

        EXPECT_FALSE(store.load(0, "middle-level-2").has_value());
        EXPECT_FALSE(store.load(0, "middle-level-3").has_value());

        ASSERT_TRUE((*outer)->commit().has_value());

        EXPECT_TRUE(store.load(0, "middle-level-1").has_value());
        EXPECT_FALSE(store.load(0, "middle-level-2").has_value());
        EXPECT_FALSE(store.load(0, "middle-level-3").has_value());
    }

    dropWriteBatchTestDb(path);
}

TEST(WriteBatchTest, OperationsAfterCommitReportFinished) {
    auto path = tempWriteBatchTestDb("finished");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << (repo.has_value() ? "" : repo.error().message());
        auto& store = (*repo)->store();

        auto batch = WriteBatch::begin(store);
        ASSERT_TRUE(batch.has_value());
        ASSERT_TRUE((*batch)->append(0, 0, "finished-row", "p").has_value());

        auto committed = (*batch)->commit();
        ASSERT_TRUE(committed.has_value()) << committed.error().message();
        EXPECT_TRUE(committed.value());

        auto appended = (*batch)->append(0, 0, "finished-late", "p");
        ASSERT_FALSE(appended.has_value());
        EXPECT_EQ(appended.error().message(), "write batch already finished");

        auto recommitted = (*batch)->commit();
        ASSERT_FALSE(recommitted.has_value());
        EXPECT_EQ(recommitted.error().message(), "write batch already finished");

        auto rolledBack = (*batch)->rollback();
        ASSERT_FALSE(rolledBack.has_value());
        EXPECT_EQ(rolledBack.error().message(), "write batch already finished");

        auto upserted = (*batch)->upsertRow(0, "finished-row", "p");
        ASSERT_FALSE(upserted.has_value());
        EXPECT_EQ(upserted.error().message(), "write batch already finished");

        EXPECT_TRUE(store.load(0, "finished-row").has_value());
        EXPECT_FALSE(store.load(0, "finished-late").has_value());
    }

    dropWriteBatchTestDb(path);
}

TEST(WriteBatchTest, InvalidSqlReportsSyntaxErrorAndRollbackStillWorks) {
    auto path = tempWriteBatchTestDb("syntax");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << (repo.has_value() ? "" : repo.error().message());
        auto& store = (*repo)->store();

        auto batch = WriteBatch::begin(store);
        ASSERT_TRUE(batch.has_value());
        ASSERT_TRUE((*batch)->append(0, 0, "syntax-survivor", "p").has_value());

        auto failed = (*batch)->exec("INSERTX INTO block(id) VALUES(1)");
        ASSERT_FALSE(failed.has_value());
        EXPECT_NE(failed.error().message().find("syntax error"), std::string::npos);

        auto stillWorks = (*batch)->append(0, 0, "syntax-extra", "p");
        ASSERT_TRUE(stillWorks.has_value());

        auto rolledBack = (*batch)->rollback();
        ASSERT_TRUE(rolledBack.has_value()) << rolledBack.error().message();
        EXPECT_TRUE(rolledBack.value());

        EXPECT_FALSE(store.load(0, "syntax-survivor").has_value());
        EXPECT_FALSE(store.load(0, "syntax-extra").has_value());
    }

    dropWriteBatchTestDb(path);
}
