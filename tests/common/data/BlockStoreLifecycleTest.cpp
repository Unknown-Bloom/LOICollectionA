#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"
#include "LOICollectionA/data/sqlite/block/BlockStore.h"
#include "LOICollectionA/data/sqlite/block/WriteBatch.h"

#include "common/data/StorageTestSupport.h"

using storage_test_support::dropDb;
using storage_test_support::expectBlockError;
using storage_test_support::payloadText;
using storage_test_support::tempDb;

TEST(BlockStoreLifecycleTest, CreateBlockStoresEveryField) {
    auto path = tempDb("lifecycle", "create-fields");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto id = store.createBlock(0, 7, "alpha", "body");
        ASSERT_TRUE(id.has_value()) << id.error().message();

        auto record = store.load(*id);
        ASSERT_TRUE(record.has_value()) << record.error().message();
        EXPECT_EQ(record.value().id, *id);
        EXPECT_EQ(record.value().parent, 0);
        EXPECT_EQ(record.value().kind, 7);
        EXPECT_EQ(record.value().name, "alpha");
        EXPECT_EQ(payloadText(record.value().payload), "body");
        EXPECT_EQ(record.value().state, BlockLifecycle::Active);
        EXPECT_GT(record.value().created, 0);
        EXPECT_GE(record.value().updated, record.value().created);
    }
    dropDb(path);
}

TEST(BlockStoreLifecycleTest, CreateBlockReturnsDistinctIds) {
    auto path = tempDb("lifecycle", "distinct-ids");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto first = store.createBlock(0, 1, "one");
        auto second = store.createBlock(0, 1, "two");
        ASSERT_TRUE(first.has_value()) << first.error().message();
        ASSERT_TRUE(second.has_value()) << second.error().message();
        EXPECT_NE(*first, *second);
    }
    dropDb(path);
}

TEST(BlockStoreLifecycleTest, CreateBlockUnderParentRecordsParent) {
    auto path = tempDb("lifecycle", "nested-parent");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto parent = store.createBlock(0, 1, "root");
        ASSERT_TRUE(parent.has_value()) << parent.error().message();
        auto child = store.createBlock(*parent, 2, "leaf");
        ASSERT_TRUE(child.has_value()) << child.error().message();

        auto record = store.load(*child);
        ASSERT_TRUE(record.has_value()) << record.error().message();
        EXPECT_EQ(record.value().parent, *parent);
    }
    dropDb(path);
}

TEST(BlockStoreLifecycleTest, CreateBlockWithoutPayloadStoresEmptyBlob) {
    auto path = tempDb("lifecycle", "empty-payload");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto id = store.createBlock(0, 1, "hollow");
        ASSERT_TRUE(id.has_value()) << id.error().message();

        auto record = store.load(*id);
        ASSERT_TRUE(record.has_value()) << record.error().message();
        EXPECT_TRUE(record.value().payload.empty());
    }
    dropDb(path);
}

TEST(BlockStoreLifecycleTest, CreateBlockRejectsDuplicateTypedRowNames) {
    auto path = tempDb("lifecycle", "duplicate-row");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto first = store.createBlock(0, kTypedRowKind, "dup");
        ASSERT_TRUE(first.has_value()) << first.error().message();

        auto second = store.createBlock(0, kTypedRowKind, "dup");
        ASSERT_FALSE(second.has_value());
        EXPECT_NE(second.error().message().find("UNIQUE"), std::string::npos)
            << second.error().message();
    }
    dropDb(path);
}

TEST(BlockStoreLifecycleTest, UpsertRowCreatesThenUpdatesInPlace) {
    auto path = tempDb("lifecycle", "upsert-row");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto first = store.upsertRow(0, "row", "v1");
        ASSERT_TRUE(first.has_value()) << first.error().message();
        auto second = store.upsertRow(0, "row", "v2");
        ASSERT_TRUE(second.has_value()) << second.error().message();
        EXPECT_EQ(*first, *second);

        auto record = store.load(*first);
        ASSERT_TRUE(record.has_value()) << record.error().message();
        EXPECT_EQ(payloadText(record.value().payload), "v2");
        EXPECT_EQ(record.value().state, BlockLifecycle::Active);
    }
    dropDb(path);
}

TEST(BlockStoreLifecycleTest, UpsertRowRevivesDeletedRow) {
    auto path = tempDb("lifecycle", "revive-row");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto id = store.upsertRow(0, "row", "v1");
        ASSERT_TRUE(id.has_value()) << id.error().message();

        ASSERT_TRUE(store.control(*id, BlockLifecycle::Deleted).has_value());
        auto state = store.stateOf(*id);
        ASSERT_TRUE(state.has_value()) << state.error().message();
        EXPECT_EQ(state.value(), BlockLifecycle::Deleted);

        auto revived = store.upsertRow(0, "row", "v2");
        ASSERT_TRUE(revived.has_value()) << revived.error().message();
        EXPECT_EQ(*revived, *id);

        auto record = store.load(*revived);
        ASSERT_TRUE(record.has_value()) << record.error().message();
        EXPECT_EQ(record.value().state, BlockLifecycle::Active);
        EXPECT_EQ(payloadText(record.value().payload), "v2");
    }
    dropDb(path);
}

TEST(BlockStoreLifecycleTest, LoadByNameSkipsDeletedRows) {
    auto path = tempDb("lifecycle", "deleted-by-name");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto id = store.createBlock(0, 4, "victim");
        ASSERT_TRUE(id.has_value()) << id.error().message();
        ASSERT_TRUE(store.control(*id, BlockLifecycle::Deleted).has_value());

        auto byName = store.load(0, "victim");
        ASSERT_FALSE(byName.has_value());
        expectBlockError(byName.error(), BlockError::BlockErrorCode::NotFound);

        auto byId = store.load(*id);
        ASSERT_TRUE(byId.has_value()) << byId.error().message();
        EXPECT_EQ(byId.value().state, BlockLifecycle::Deleted);

        auto idOf = store.idOf(0, "victim");
        ASSERT_TRUE(idOf.has_value()) << idOf.error().message();
        EXPECT_FALSE(idOf.value().has_value());
    }
    dropDb(path);
}

TEST(BlockStoreLifecycleTest, LoadMissingIdReturnsNotFound) {
    auto path = tempDb("lifecycle", "missing-id");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto loaded = store.load(9999);
        ASSERT_FALSE(loaded.has_value());
        expectBlockError(loaded.error(), BlockError::BlockErrorCode::NotFound);
    }
    dropDb(path);
}

TEST(BlockStoreLifecycleTest, LoadMissingNameReturnsNotFound) {
    auto path = tempDb("lifecycle", "missing-name");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto loaded = store.load(0, "ghost");
        ASSERT_FALSE(loaded.has_value());
        expectBlockError(loaded.error(), BlockError::BlockErrorCode::NotFound);
    }
    dropDb(path);
}

TEST(BlockStoreLifecycleTest, WithBlockExposesViewAndReportsMissing) {
    auto path = tempDb("lifecycle", "with-block");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto id = store.createBlock(0, 6, "viewed", "data");
        ASSERT_TRUE(id.has_value()) << id.error().message();

        auto visited = store.withBlock(*id, [&](BlockView const& view) {
            EXPECT_EQ(view.id, *id);
            EXPECT_EQ(view.parent, 0);
            EXPECT_EQ(view.kind, 6);
            EXPECT_EQ(view.name, "viewed");
            EXPECT_EQ(view.state, BlockLifecycle::Active);
            EXPECT_EQ(view.payload, "data");
        });
        ASSERT_TRUE(visited.has_value()) << visited.error().message();

        auto missing = store.withBlock(4242, [&](BlockView const&) { FAIL() << "must not be visited"; });
        ASSERT_FALSE(missing.has_value());
        expectBlockError(missing.error(), BlockError::BlockErrorCode::NotFound);
    }
    dropDb(path);
}

TEST(BlockStoreLifecycleTest, MaterializeRestoresOwnedRecord) {
    auto path = tempDb("lifecycle", "materialize");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto id = store.createBlock(0, 3, "origin", "blob");
        ASSERT_TRUE(id.has_value()) << id.error().message();

        std::string name = "origin";
        std::string payload = "blob";
        BlockView view;
        view.id = *id;
        view.parent = 0;
        view.kind = 3;
        view.name = name;
        view.state = BlockLifecycle::Frozen;
        view.payload = payload;

        auto record = materialize(view);
        EXPECT_EQ(record.id, *id);
        EXPECT_EQ(record.parent, 0);
        EXPECT_EQ(record.kind, 3);
        EXPECT_EQ(record.name, "origin");
        EXPECT_EQ(record.state, BlockLifecycle::Frozen);
        EXPECT_EQ(payloadText(record.payload), "blob");
        EXPECT_TRUE(record.props.empty());
    }
    dropDb(path);
}

TEST(BlockStoreLifecycleTest, SetPayloadUpdatesLoadedRecord) {
    auto path = tempDb("lifecycle", "set-payload");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto id = store.createBlock(0, 1, "row", "first");
        ASSERT_TRUE(id.has_value()) << id.error().message();

        ASSERT_TRUE(store.load(*id).has_value());
        ASSERT_TRUE(store.setPayload(*id, "second").has_value());

        auto record = store.load(*id);
        ASSERT_TRUE(record.has_value()) << record.error().message();
        EXPECT_EQ(payloadText(record.value().payload), "second");
    }
    dropDb(path);
}

TEST(BlockStoreLifecycleTest, SetPayloadMissingBlockReturnsNotFound) {
    auto path = tempDb("lifecycle", "set-payload-missing");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto updated = store.setPayload(777, "x");
        ASSERT_FALSE(updated.has_value());
        expectBlockError(updated.error(), BlockError::BlockErrorCode::NotFound);
    }
    dropDb(path);
}

TEST(BlockStoreLifecycleTest, SetPayloadRoundTripsLargeBinaryPayload) {
    auto path = tempDb("lifecycle", "binary-payload");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        std::string payload(1u << 20, '\0');
        for (std::size_t i = 0; i < payload.size(); i += 997)
            payload[i] = static_cast<char>((i * 31) & 0xFF);

        auto id = store.createBlock(0, 1, "binary", payload);
        ASSERT_TRUE(id.has_value()) << id.error().message();

        auto record = store.load(*id);
        ASSERT_TRUE(record.has_value()) << record.error().message();
        EXPECT_EQ(payloadText(record.value().payload), payload);
    }
    dropDb(path);
}

TEST(BlockStoreLifecycleTest, RemoveSoftDeletesBlockFromLiveQueries) {
    auto path = tempDb("lifecycle", "remove");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto id = store.createBlock(0, 1, "doomed");
        ASSERT_TRUE(id.has_value()) << id.error().message();
        ASSERT_TRUE(store.remove(*id).has_value());

        auto loaded = store.load(*id);
        ASSERT_TRUE(loaded.has_value()) << loaded.error().message();
        EXPECT_EQ(loaded.value().state, BlockLifecycle::Deleted);

        auto byName = store.load(0, "doomed");
        ASSERT_FALSE(byName.has_value());
        expectBlockError(byName.error(), BlockError::BlockErrorCode::NotFound);

        auto byId = store.idOf(0, "doomed");
        ASSERT_TRUE(byId.has_value()) << byId.error().message();
        EXPECT_FALSE(byId.value().has_value());

        auto children = store.children(0);
        ASSERT_TRUE(children.has_value());
        EXPECT_EQ(children.value().size(), 0u);

        ASSERT_TRUE(store.remove(*id).has_value());
    }
    dropDb(path);
}

TEST(BlockStoreLifecycleTest, ControlUpdatesStateForEveryLifecycleStage) {
    auto path = tempDb("lifecycle", "control-stages");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto id = store.createBlock(0, 1, "staged");
        ASSERT_TRUE(id.has_value()) << id.error().message();

        for (auto stage : {BlockLifecycle::Frozen, BlockLifecycle::Sealed,
                           BlockLifecycle::Archived, BlockLifecycle::Deleted}) {
            ASSERT_TRUE(store.control(*id, stage).has_value());
            auto state = store.stateOf(*id);
            ASSERT_TRUE(state.has_value()) << state.error().message();
            EXPECT_EQ(state.value(), stage);

            auto record = store.load(*id);
            ASSERT_TRUE(record.has_value()) << record.error().message();
            EXPECT_EQ(record.value().state, stage);
        }
    }
    dropDb(path);
}

TEST(BlockStoreLifecycleTest, ControlMissingBlockReturnsNotFound) {
    auto path = tempDb("lifecycle", "control-missing");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto updated = store.control(31337, BlockLifecycle::Frozen);
        ASSERT_FALSE(updated.has_value());
        expectBlockError(updated.error(), BlockError::BlockErrorCode::NotFound);
    }
    dropDb(path);
}

TEST(BlockStoreLifecycleTest, ChildrenCountAndNamesSkipArchivedAndDeleted) {
    auto path = tempDb("lifecycle", "live-children");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto live = store.createBlock(0, 5, "live");
        auto archived = store.createBlock(0, 5, "archived");
        auto deleted = store.createBlock(0, 5, "deleted");
        ASSERT_TRUE(live.has_value()) << live.error().message();
        ASSERT_TRUE(archived.has_value()) << archived.error().message();
        ASSERT_TRUE(deleted.has_value()) << deleted.error().message();

        ASSERT_TRUE(store.control(*archived, BlockLifecycle::Archived).has_value());
        ASSERT_TRUE(store.control(*deleted, BlockLifecycle::Deleted).has_value());

        auto children = store.children(0);
        ASSERT_TRUE(children.has_value()) << children.error().message();
        EXPECT_EQ(children.value(), std::vector<BlockId>{*live});

        auto count = store.countChildren(0);
        ASSERT_TRUE(count.has_value()) << count.error().message();
        EXPECT_EQ(count.value(), 1u);

        auto names = store.childNames(0);
        ASSERT_TRUE(names.has_value()) << names.error().message();
        ASSERT_EQ(names.value().size(), 1u);
        EXPECT_EQ(names.value().front().first, *live);
        EXPECT_EQ(names.value().front().second, "live");
    }
    dropDb(path);
}

TEST(BlockStoreLifecycleTest, CountChildrenFiltersByKind) {
    auto path = tempDb("lifecycle", "count-kinds");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        ASSERT_TRUE(store.createBlock(0, 5, "a").has_value());
        ASSERT_TRUE(store.createBlock(0, 5, "b").has_value());
        ASSERT_TRUE(store.createBlock(0, 6, "c").has_value());

        auto kind5 = store.countChildren(0, 5);
        ASSERT_TRUE(kind5.has_value()) << kind5.error().message();
        EXPECT_EQ(kind5.value(), 2u);
        auto kind6 = store.countChildren(0, 6);
        ASSERT_TRUE(kind6.has_value()) << kind6.error().message();
        EXPECT_EQ(kind6.value(), 1u);
        auto anyKind = store.countChildren(0, -1);
        ASSERT_TRUE(anyKind.has_value()) << anyKind.error().message();
        EXPECT_EQ(anyKind.value(), 3u);
        auto noneKind = store.countChildren(0, 7);
        ASSERT_TRUE(noneKind.has_value()) << noneKind.error().message();
        EXPECT_EQ(noneKind.value(), 0u);
    }
    dropDb(path);
}

TEST(BlockStoreLifecycleTest, ChildrenLimitCapsResultSet) {
    auto path = tempDb("lifecycle", "children-limit");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        for (int i = 0; i < 5; ++i)
            ASSERT_TRUE(store.createBlock(0, 5, "child" + std::to_string(i)).has_value());

        auto limited = store.children(0, -1, 2);
        ASSERT_TRUE(limited.has_value()) << limited.error().message();
        EXPECT_EQ(limited.value().size(), 2u);

        auto unlimited = store.children(0);
        ASSERT_TRUE(unlimited.has_value());
        EXPECT_EQ(unlimited.value().size(), 5u);
    }
    dropDb(path);
}

TEST(BlockStoreLifecycleTest, RecordsIncludePayloadAndProps) {
    auto path = tempDb("lifecycle", "records-full");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto id = store.createBlock(0, 8, "full", "body");
        ASSERT_TRUE(id.has_value()) << id.error().message();
        ASSERT_TRUE(store.setProp(*id, 7, std::int64_t(42)).has_value());

        auto rows = store.records(0, 8);
        ASSERT_TRUE(rows.has_value()) << rows.error().message();
        ASSERT_EQ(rows.value().size(), 1u);

        auto const& record = rows.value().front();
        EXPECT_EQ(record.id, *id);
        EXPECT_EQ(record.name, "full");
        EXPECT_EQ(payloadText(record.payload), "body");

        auto prop = std::find_if(record.props.begin(), record.props.end(),
            [](BlockProp const& p) { return p.key == 7; });
        ASSERT_NE(prop, record.props.end());
        EXPECT_EQ(prop->type, PayloadType::Int);
        EXPECT_EQ(prop->intValue, 42);
    }
    dropDb(path);
}

TEST(BlockStoreLifecycleTest, RowsByIdsRespectWithPropsFlag) {
    auto path = tempDb("lifecycle", "rows-by-ids");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto first = store.createBlock(0, 2, "first");
        auto second = store.createBlock(0, 2, "second");
        ASSERT_TRUE(first.has_value()) << first.error().message();
        ASSERT_TRUE(second.has_value()) << second.error().message();
        ASSERT_TRUE(store.setProp(*first, 3, std::int64_t(11)).has_value());
        ASSERT_TRUE(store.setProp(*second, 3, std::int64_t(22)).has_value());

        std::vector<BlockId> targets{*first, *second};

        auto light = store.rowsByIds(targets, false);
        ASSERT_TRUE(light.has_value()) << light.error().message();
        ASSERT_EQ(light.value().size(), 2u);
        EXPECT_TRUE(light.value().front().props.empty());

        auto heavy = store.rowsByIds(targets, true);
        ASSERT_TRUE(heavy.has_value()) << heavy.error().message();
        ASSERT_EQ(heavy.value().size(), 2u);
        EXPECT_EQ(heavy.value().at(0).props.size(), 1u);
        EXPECT_EQ(heavy.value().at(0).props.front().intValue, 11);
        EXPECT_EQ(heavy.value().at(1).props.front().intValue, 22);
    }
    dropDb(path);
}

TEST(BlockStoreLifecycleTest, RowsByIdsSpansMultipleIdChunks) {
    auto path = tempDb("lifecycle", "rows-chunks");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        std::vector<std::string> names;
        names.reserve(950);
        for (int i = 0; i < 950; ++i)
            names.push_back("bulk" + std::to_string(i));

        std::vector<std::pair<std::string_view, std::string_view>> rows;
        for (auto const& name : names)
            rows.emplace_back(name, "");

        auto batch = WriteBatch::begin(store);
        ASSERT_TRUE(batch.has_value()) << batch.error().message();
        auto ids = (*batch)->appendMany(0, 12, rows);
        ASSERT_TRUE(ids.has_value()) << ids.error().message();
        ASSERT_TRUE((*batch)->commit().has_value());

        auto loaded = store.rowsByIds(ids.value(), false);
        ASSERT_TRUE(loaded.has_value()) << loaded.error().message();
        EXPECT_EQ(loaded.value().size(), 950u);

        auto empty = store.rowsByIds({}, false);
        ASSERT_TRUE(empty.has_value()) << empty.error().message();
        EXPECT_EQ(empty.value().size(), 0u);
    }
    dropDb(path);
}

TEST(BlockStoreLifecycleTest, CacheEvictionKeepsPayloadsCorrect) {
    auto path = tempDb("lifecycle", "cache-eviction");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        std::vector<std::string> names;
        names.reserve(2100);
        std::vector<std::string> payloads;
        payloads.reserve(2100);
        for (int i = 0; i < 2100; ++i) {
            names.push_back("mass" + std::to_string(i));
            payloads.push_back("pay" + std::to_string(i));
        }

        std::vector<std::pair<std::string_view, std::string_view>> rows;
        for (std::size_t i = 0; i < names.size(); ++i)
            rows.emplace_back(names[i], payloads[i]);

        auto batch = WriteBatch::begin(store);
        ASSERT_TRUE(batch.has_value()) << batch.error().message();
        auto ids = (*batch)->appendMany(0, 13, rows);
        ASSERT_TRUE(ids.has_value()) << ids.error().message();
        ASSERT_TRUE((*batch)->commit().has_value());

        for (auto id : ids.value()) {
            auto record = store.load(id);
            ASSERT_TRUE(record.has_value()) << record.error().message();
            EXPECT_EQ(payloadText(record.value().payload),
                      "pay" + std::to_string(record.value().id - ids.value().front()));
        }

        for (auto offset : {0, 1, 1049, 2099}) {
            auto record = store.load(ids.value().at(offset));
            ASSERT_TRUE(record.has_value()) << record.error().message();
            EXPECT_EQ(payloadText(record.value().payload),
                      "pay" + std::to_string(ids.value().at(offset) - ids.value().front()));
        }

        for (auto offset : {0, 1, 1049, 2099}) {
            auto id = store.idOf(0, "mass" + std::to_string(offset));
            ASSERT_TRUE(id.has_value()) << id.error().message();
            ASSERT_TRUE(id.value().has_value());
            EXPECT_EQ(*id.value(), ids.value().at(offset));
        }
    }
    dropDb(path);
}
