#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"
#include "LOICollectionA/data/sqlite/block/BlockStore.h"
#include "LOICollectionA/data/sqlite/block/BlockError.h"
#include "LOICollectionA/data/sqlite/block/ColumnText.h"
#include "LOICollectionA/data/sqlite/block/WriteBatch.h"
#include "LOICollectionA/data/sqlite/connection/ConnectionPool.h"

#include "common/data/StorageTestSupport.h"

using storage_test_support::dropDb;
using storage_test_support::expectBlockError;
using storage_test_support::payloadText;
using storage_test_support::tempDb;

TEST(WriteBatchOperationsTest, AppendReadsYourOwnWritesAndCommits) {
    auto path = tempDb("batchops", "append");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto batch = WriteBatch::begin(store);
        ASSERT_TRUE(batch.has_value()) << batch.error().message();

        auto id = (*batch)->append(0, 3, "alpha", "body");
        ASSERT_TRUE(id.has_value()) << id.error().message();

        auto pending = store.load(*id);
        ASSERT_TRUE(pending.has_value()) << pending.error().message();
        EXPECT_EQ(pending.value().kind, 3);
        EXPECT_EQ(pending.value().name, "alpha");
        EXPECT_EQ(payloadText(pending.value().payload), "body");

        ASSERT_TRUE((*batch)->commit().has_value());

        auto visible = store.load(*id);
        ASSERT_TRUE(visible.has_value()) << visible.error().message();
        EXPECT_EQ(visible.value().kind, 3);
        EXPECT_EQ(visible.value().name, "alpha");
        EXPECT_EQ(payloadText(visible.value().payload), "body");
    }
    dropDb(path);
}

TEST(WriteBatchOperationsTest, AppendManyReturnsOrderedIdsAndCommits) {
    auto path = tempDb("batchops", "append-many");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        std::vector<std::string> names{"n1", "n2", "n3"};
        std::vector<std::pair<std::string_view, std::string_view>> rows;
        for (auto const& name : names)
            rows.emplace_back(name, name);

        auto batch = WriteBatch::begin(store);
        ASSERT_TRUE(batch.has_value()) << batch.error().message();

        auto ids = (*batch)->appendMany(0, 4, rows);
        ASSERT_TRUE(ids.has_value()) << ids.error().message();
        ASSERT_EQ(ids.value().size(), 3u);

        std::sort(ids.value().begin(), ids.value().end());
        EXPECT_EQ(ids.value().front() + 1, ids.value().at(1));
        EXPECT_EQ(ids.value().front() + 2, ids.value().back());

        ASSERT_TRUE((*batch)->commit().has_value());

        for (auto const& name : names) {
            auto id = store.idOf(0, name);
            ASSERT_TRUE(id.has_value()) << id.error().message();
            ASSERT_TRUE(id.value().has_value());

            auto record = store.load(*id.value());
            ASSERT_TRUE(record.has_value()) << record.error().message();
            EXPECT_EQ(payloadText(record.value().payload), name);
        }
    }
    dropDb(path);
}

TEST(WriteBatchOperationsTest, UpsertRowInsideBatchUpdatesExistingRow) {
    auto path = tempDb("batchops", "upsert");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto created = store.upsertRow(0, "row", "v1");
        ASSERT_TRUE(created.has_value()) << created.error().message();

        auto batch = WriteBatch::begin(store);
        ASSERT_TRUE(batch.has_value()) << batch.error().message();

        auto updated = (*batch)->upsertRow(0, "row", "v2");
        ASSERT_TRUE(updated.has_value()) << updated.error().message();
        EXPECT_EQ(*updated, *created);

        ASSERT_TRUE((*batch)->commit().has_value());

        auto record = store.load(*created);
        ASSERT_TRUE(record.has_value()) << record.error().message();
        EXPECT_EQ(payloadText(record.value().payload), "v2");
    }
    dropDb(path);
}

TEST(WriteBatchOperationsTest, SetPayloadAndControlApplyAfterCommit) {
    auto path = tempDb("batchops", "payload-control");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto batch = WriteBatch::begin(store);
        ASSERT_TRUE(batch.has_value()) << batch.error().message();

        auto id = (*batch)->append(0, 5, "staged", "v1");
        ASSERT_TRUE(id.has_value()) << id.error().message();

        ASSERT_TRUE((*batch)->setPayload(*id, "v2").has_value());
        ASSERT_TRUE((*batch)->control(*id, BlockLifecycle::Frozen).has_value());

        ASSERT_TRUE((*batch)->commit().has_value());

        auto record = store.load(*id);
        ASSERT_TRUE(record.has_value()) << record.error().message();
        EXPECT_EQ(payloadText(record.value().payload), "v2");
        EXPECT_EQ(record.value().state, BlockLifecycle::Frozen);
    }
    dropDb(path);
}

TEST(WriteBatchOperationsTest, SetPropOverloadsWriteTypedColumnsAfterCommit) {
    auto path = tempDb("batchops", "set-prop");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto batch = WriteBatch::begin(store);
        ASSERT_TRUE(batch.has_value()) << batch.error().message();

        auto id = (*batch)->append(0, 5, "props");
        ASSERT_TRUE(id.has_value()) << id.error().message();

        ASSERT_TRUE((*batch)->setProp(*id, 1, std::int64_t(42)).has_value());
        ASSERT_TRUE((*batch)->setProp(*id, 2, 2.5).has_value());
        ASSERT_TRUE((*batch)->setProp(*id, 3, std::string_view("hi")).has_value());
        ASSERT_TRUE((*batch)->setProp(*id, 4, PayloadType::Text, 0, 0.0, std::string_view("raw")).has_value());

        ASSERT_TRUE((*batch)->commit().has_value());

        auto record = store.load(*id);
        ASSERT_TRUE(record.has_value()) << record.error().message();
        ASSERT_EQ(record.value().props.size(), 4u);

        for (auto const& prop : record.value().props) {
            switch (prop.key) {
                case 1:
                    EXPECT_EQ(prop.type, PayloadType::Int);
                    EXPECT_EQ(prop.intValue, 42);
                    break;
                case 2:
                    EXPECT_EQ(prop.type, PayloadType::Double);
                    EXPECT_DOUBLE_EQ(prop.realValue, 2.5);
                    break;
                case 3:
                    EXPECT_EQ(prop.type, PayloadType::Text);
                    EXPECT_EQ(prop.textValue, "hi");
                    break;
                case 4:
                    EXPECT_EQ(prop.type, PayloadType::Text);
                    EXPECT_EQ(prop.textValue, "raw");
                    break;
                default:
                    ADD_FAILURE() << "unexpected prop key " << prop.key;
            }
        }
    }
    dropDb(path);
}

TEST(WriteBatchOperationsTest, ExecAndExecCellsRunInsideTheTransaction) {
    auto path = tempDb("batchops", "exec-cells");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto batch = WriteBatch::begin(store);
        ASSERT_TRUE(batch.has_value()) << batch.error().message();

        ASSERT_TRUE((*batch)->exec("CREATE TABLE scratch(a INTEGER, b TEXT)").has_value());

        std::vector<BlockProp> params;
        BlockProp integer{};
        integer.type = PayloadType::Int;
        integer.intValue = 42;
        params.push_back(integer);

        BlockProp text{};
        text.type = PayloadType::Text;
        text.textValue = "v";
        params.push_back(text);

        auto inserted = (*batch)->execCells("scratchInsert", "INSERT INTO scratch VALUES(?1, ?2)", params);
        ASSERT_TRUE(inserted.has_value()) << inserted.error().message();

        ASSERT_TRUE((*batch)->commit().has_value());

        int rows = 0;
        auto queried = store.withQuery(
            "scratchSelect", "SELECT a, b FROM scratch", {},
            [&](SQLite::Statement& stmt) {
                ++rows;
                EXPECT_EQ(stmt.getColumn(0).getInt64(), 42);
                EXPECT_EQ(columnToString(stmt.getColumn(1)), "v");
            });
        ASSERT_TRUE(queried.has_value()) << queried.error().message();
        EXPECT_EQ(rows, 1);
    }
    dropDb(path);
}

TEST(WriteBatchOperationsTest, InternInsideBatchMatchesStoreInternAfterCommit) {
    auto path = tempDb("batchops", "intern");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto batch = WriteBatch::begin(store);
        ASSERT_TRUE(batch.has_value()) << batch.error().message();

        auto interned = (*batch)->intern("label");
        ASSERT_TRUE(interned.has_value()) << interned.error().message();

        ASSERT_TRUE((*batch)->commit().has_value());

        auto outside = store.intern("label");
        ASSERT_TRUE(outside.has_value()) << outside.error().message();
        EXPECT_EQ(*outside, *interned);

        auto name = store.unintern(*interned);
        ASSERT_TRUE(name.has_value()) << name.error().message();
        EXPECT_EQ(*name, "label");
    }
    dropDb(path);
}

TEST(WriteBatchOperationsTest, LinkInsideBatchShowsUpAfterCommit) {
    auto path = tempDb("batchops", "link");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto src = store.createBlock(0, 1, "src");
        auto dst = store.createBlock(0, 1, "dst");
        ASSERT_TRUE(src.has_value()) << src.error().message();
        ASSERT_TRUE(dst.has_value()) << dst.error().message();

        auto batch = WriteBatch::begin(store);
        ASSERT_TRUE(batch.has_value()) << batch.error().message();

        ASSERT_TRUE((*batch)->link(*src, *dst, 7).has_value());
        ASSERT_TRUE((*batch)->commit().has_value());

        auto edges = store.links(*src, 7);
        ASSERT_TRUE(edges.has_value()) << edges.error().message();
        ASSERT_EQ(edges.value().size(), 1u);
        EXPECT_EQ(edges.value().front(), *dst);
    }
    dropDb(path);
}

TEST(WriteBatchOperationsTest, RollbackDiscardsAppendedRows) {
    auto path = tempDb("batchops", "rollback-append");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto batch = WriteBatch::begin(store);
        ASSERT_TRUE(batch.has_value()) << batch.error().message();

        auto id = (*batch)->append(0, 3, "ghost", "body");
        ASSERT_TRUE(id.has_value()) << id.error().message();

        ASSERT_TRUE((*batch)->rollback().has_value());

        auto loaded = store.load(*id);
        ASSERT_FALSE(loaded.has_value());
        expectBlockError(loaded.error(), BlockError::BlockErrorCode::NotFound);
    }
    dropDb(path);
}

TEST(WriteBatchOperationsTest, FailedExecLeavesRollbackUsable) {
    auto path = tempDb("batchops", "exec-failure");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto batch = WriteBatch::begin(store);
        ASSERT_TRUE(batch.has_value()) << batch.error().message();

        auto id = (*batch)->append(0, 3, "kept", "body");
        ASSERT_TRUE(id.has_value()) << id.error().message();

        auto broken = (*batch)->exec("THIS IS NOT SQL");
        ASSERT_FALSE(broken.has_value());
        EXPECT_FALSE(broken.error().message().empty());

        ASSERT_TRUE((*batch)->rollback().has_value());

        auto after = store.load(*id);
        ASSERT_FALSE(after.has_value());
    }
    dropDb(path);
}

TEST(WriteBatchOperationsTest, OperationsAfterRollbackReportFinished) {
    auto path = tempDb("batchops", "finished");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto batch = WriteBatch::begin(store);
        ASSERT_TRUE(batch.has_value()) << batch.error().message();

        auto id = (*batch)->append(0, 3, "row");
        ASSERT_TRUE(id.has_value()) << id.error().message();

        ASSERT_TRUE((*batch)->rollback().has_value());

        auto stale = (*batch)->setProp(*id, 1, std::int64_t(1));
        ASSERT_FALSE(stale.has_value());
        EXPECT_EQ(stale.error().message(), "write batch already finished");
    }
    dropDb(path);
}

TEST(WriteBatchOperationsTest, BeginTimesOutWhenWriteLockIsHeld) {
    auto path = tempDb("batchops", "write-lock-timeout");

    {
        auto pool = ConnectionPool::create(path.string(), 1);
        ASSERT_TRUE(pool.has_value()) << pool.error().message();

        auto store = BlockStore::create(*pool);
        ASSERT_TRUE(store.has_value()) << store.error().message();

        ASSERT_TRUE((*pool)->tryAcquireWrite(0));

        auto started = std::chrono::steady_clock::now();
        auto batch = WriteBatch::begin(**store);
        auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();

        ASSERT_FALSE(batch.has_value());
        EXPECT_EQ(batch.error().message(), "timed out waiting for the database write lock");
        EXPECT_GE(waited, 4000);

        (*pool)->releaseWrite();

        auto recovered = WriteBatch::begin(**store);
        ASSERT_TRUE(recovered.has_value()) << recovered.error().message();
        ASSERT_TRUE((*recovered)->commit().has_value());
    }
    dropDb(path);
}
