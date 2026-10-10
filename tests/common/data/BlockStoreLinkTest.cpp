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
#include "LOICollectionA/data/sqlite/block/ColumnText.h"

#include "common/data/StorageTestSupport.h"

using storage_test_support::dropDb;
using storage_test_support::expectBlockError;
using storage_test_support::tempDb;

namespace block_store_link_test_support {
    bool contains(std::vector<BlockId> const& ids, BlockId value) {
        return std::find(ids.begin(), ids.end(), value) != ids.end();
    }

    std::optional<BlockProp> findProp(BlockRecord const& record, PropKey key) {
        auto it = std::find_if(record.props.begin(), record.props.end(),
            [key](BlockProp const& p) { return p.key == key; });
        if (it == record.props.end())
            return std::nullopt;

        return *it;
    }
}

TEST(BlockStoreLinkTest, LinkRoundTripsThroughLinksAndBacklinks) {
    auto path = tempDb("link", "round-trip");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto src = store.createBlock(0, 1, "src");
        auto dst = store.createBlock(0, 1, "dst");
        ASSERT_TRUE(src.has_value()) << src.error().message();
        ASSERT_TRUE(dst.has_value()) << dst.error().message();

        ASSERT_TRUE(store.link(*src, *dst, 3).has_value());

        auto fromSrc = store.links(*src);
        ASSERT_TRUE(fromSrc.has_value()) << fromSrc.error().message();
        EXPECT_TRUE(block_store_link_test_support::contains(fromSrc.value(), *dst));

        auto fromDst = store.backlinks(*dst);
        ASSERT_TRUE(fromDst.has_value()) << fromDst.error().message();
        EXPECT_TRUE(block_store_link_test_support::contains(fromDst.value(), *src));
    }
    dropDb(path);
}

TEST(BlockStoreLinkTest, LinksFilterByKindAndIgnoreOtherSources) {
    auto path = tempDb("link", "kind-filter");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto src = store.createBlock(0, 1, "src");
        auto other = store.createBlock(0, 1, "other");
        auto first = store.createBlock(0, 1, "first");
        auto second = store.createBlock(0, 1, "second");
        ASSERT_TRUE(src.has_value()) << src.error().message();
        ASSERT_TRUE(other.has_value()) << other.error().message();
        ASSERT_TRUE(first.has_value()) << first.error().message();
        ASSERT_TRUE(second.has_value()) << second.error().message();

        ASSERT_TRUE(store.link(*src, *first, 3).has_value());
        ASSERT_TRUE(store.link(*src, *second, 4).has_value());
        ASSERT_TRUE(store.link(*other, *first, 4).has_value());

        auto kind3 = store.links(*src, 3);
        ASSERT_TRUE(kind3.has_value()) << kind3.error().message();
        EXPECT_EQ(kind3.value(), std::vector<BlockId>{*first});

        auto kind4 = store.links(*src, 4);
        ASSERT_TRUE(kind4.has_value()) << kind4.error().message();
        EXPECT_EQ(kind4.value(), std::vector<BlockId>{*second});

        auto any = store.links(*src);
        ASSERT_TRUE(any.has_value()) << any.error().message();
        EXPECT_EQ(any.value(), (std::vector<BlockId>{*first, *second}));

        auto fromOther = store.links(*other, 4);
        ASSERT_TRUE(fromOther.has_value()) << fromOther.error().message();
        EXPECT_EQ(fromOther.value(), std::vector<BlockId>{*first});

        auto none = store.links(*src, 9);
        ASSERT_TRUE(none.has_value()) << none.error().message();
        EXPECT_EQ(none.value().size(), 0u);

        auto backKind3 = store.backlinks(*first, 3);
        ASSERT_TRUE(backKind3.has_value()) << backKind3.error().message();
        EXPECT_EQ(backKind3.value(), std::vector<BlockId>{*src});

        auto backKind4 = store.backlinks(*first, 4);
        ASSERT_TRUE(backKind4.has_value()) << backKind4.error().message();
        EXPECT_EQ(backKind4.value(), std::vector<BlockId>{*other});
    }
    dropDb(path);
}

TEST(BlockStoreLinkTest, DuplicateLinkIsIgnored) {
    auto path = tempDb("link", "duplicate");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto src = store.createBlock(0, 1, "src");
        auto dst = store.createBlock(0, 1, "dst");
        ASSERT_TRUE(src.has_value()) << src.error().message();
        ASSERT_TRUE(dst.has_value()) << dst.error().message();

        ASSERT_TRUE(store.link(*src, *dst, 1).has_value());
        ASSERT_TRUE(store.link(*src, *dst, 1).has_value());

        auto linked = store.links(*src, 1);
        ASSERT_TRUE(linked.has_value()) << linked.error().message();
        EXPECT_EQ(linked.value().size(), 1u);
    }
    dropDb(path);
}

TEST(BlockStoreLinkTest, UnlinkRemovesOnlyTheRequestedKind) {
    auto path = tempDb("link", "unlink-kind");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto src = store.createBlock(0, 1, "src");
        auto dst = store.createBlock(0, 1, "dst");
        ASSERT_TRUE(src.has_value()) << src.error().message();
        ASSERT_TRUE(dst.has_value()) << dst.error().message();

        ASSERT_TRUE(store.link(*src, *dst, 3).has_value());
        ASSERT_TRUE(store.link(*src, *dst, 4).has_value());

        ASSERT_TRUE(store.unlink(*src, *dst, 3).has_value());

        auto kind3 = store.links(*src, 3);
        ASSERT_TRUE(kind3.has_value()) << kind3.error().message();
        EXPECT_EQ(kind3.value().size(), 0u);

        auto kind4 = store.links(*src, 4);
        ASSERT_TRUE(kind4.has_value()) << kind4.error().message();
        EXPECT_EQ(kind4.value(), std::vector<BlockId>{*dst});

        auto any = store.links(*src);
        ASSERT_TRUE(any.has_value()) << any.error().message();
        EXPECT_EQ(any.value(), std::vector<BlockId>{*dst});
    }
    dropDb(path);
}

TEST(BlockStoreLinkTest, UnlinkUnknownEdgeSucceedsAsNoOp) {
    auto path = tempDb("link", "unlink-missing");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto src = store.createBlock(0, 1, "src");
        auto dst = store.createBlock(0, 1, "dst");
        ASSERT_TRUE(src.has_value()) << src.error().message();
        ASSERT_TRUE(dst.has_value()) << dst.error().message();

        ASSERT_TRUE(store.unlink(*src, *dst, 5).has_value());

        auto linked = store.links(*src);
        ASSERT_TRUE(linked.has_value()) << linked.error().message();
        EXPECT_EQ(linked.value().size(), 0u);
    }
    dropDb(path);
}

TEST(BlockStoreLinkTest, InternIsStableAcrossCallsAndNames) {
    auto path = tempDb("intern", "stable");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto first = store.intern("mode");
        auto again = store.intern("mode");
        auto other = store.intern("other");
        ASSERT_TRUE(first.has_value()) << first.error().message();
        ASSERT_TRUE(again.has_value()) << again.error().message();
        ASSERT_TRUE(other.has_value()) << other.error().message();

        EXPECT_EQ(*first, *again);
        EXPECT_NE(*first, *other);

        auto name = store.unintern(*first);
        ASSERT_TRUE(name.has_value()) << name.error().message();
        EXPECT_EQ(*name, "mode");
    }
    dropDb(path);
}

TEST(BlockStoreLinkTest, UninternUnknownIdReturnsNotFound) {
    auto path = tempDb("intern", "unknown");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto name = store.unintern(9999);
        ASSERT_FALSE(name.has_value());
        expectBlockError(name.error(), BlockError::BlockErrorCode::NotFound);
    }
    dropDb(path);
}

TEST(BlockStoreLinkTest, InternIdsSurviveReopen) {
    auto path = tempDb("intern", "persist");

    BlockId firstId = 0;
    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto id = (*repo)->store().intern("persisted");
        ASSERT_TRUE(id.has_value()) << id.error().message();
        firstId = *id;
    }

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto id = (*repo)->store().intern("persisted");
        ASSERT_TRUE(id.has_value()) << id.error().message();
        EXPECT_EQ(*id, firstId);

        auto name = (*repo)->store().unintern(*id);
        ASSERT_TRUE(name.has_value()) << name.error().message();
        EXPECT_EQ(*name, "persisted");
    }
    dropDb(path);
}

TEST(BlockStoreLinkTest, SetPropOverloadsPreserveTheirTypes) {
    auto path = tempDb("prop", "type-overloads");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto id = store.createBlock(0, 1, "props");
        ASSERT_TRUE(id.has_value()) << id.error().message();

        ASSERT_TRUE(store.setProp(*id, 11, std::int64_t(-7)).has_value());
        ASSERT_TRUE(store.setProp(*id, 12, 2.5).has_value());
        ASSERT_TRUE(store.setProp(*id, 13, std::string_view("hi")).has_value());

        auto record = store.load(*id);
        ASSERT_TRUE(record.has_value()) << record.error().message();
        EXPECT_EQ(record.value().props.size(), 3u);

        auto integer = block_store_link_test_support::findProp(record.value(), 11);
        ASSERT_TRUE(integer.has_value());
        EXPECT_EQ(integer->type, PayloadType::Int);
        EXPECT_EQ(integer->intValue, -7);

        auto real = block_store_link_test_support::findProp(record.value(), 12);
        ASSERT_TRUE(real.has_value());
        EXPECT_EQ(real->type, PayloadType::Double);
        EXPECT_DOUBLE_EQ(real->realValue, 2.5);

        auto text = block_store_link_test_support::findProp(record.value(), 13);
        ASSERT_TRUE(text.has_value());
        EXPECT_EQ(text->type, PayloadType::Text);
        EXPECT_EQ(text->textValue, "hi");
    }
    dropDb(path);
}

TEST(BlockStoreLinkTest, SetPropOverwritesExistingKeyInPlace) {
    auto path = tempDb("prop", "overwrite");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto id = store.createBlock(0, 1, "props");
        ASSERT_TRUE(id.has_value()) << id.error().message();

        ASSERT_TRUE(store.setProp(*id, 7, std::int64_t(1)).has_value());
        ASSERT_TRUE(store.setProp(*id, 7, std::int64_t(2)).has_value());

        auto record = store.load(*id);
        ASSERT_TRUE(record.has_value()) << record.error().message();
        EXPECT_EQ(record.value().props.size(), 1u);

        auto prop = block_store_link_test_support::findProp(record.value(), 7);
        ASSERT_TRUE(prop.has_value());
        EXPECT_EQ(prop->intValue, 2);
    }
    dropDb(path);
}

TEST(BlockStoreLinkTest, SetPropOnUnknownBlockLeavesReadsUnchanged) {
    auto path = tempDb("prop", "missing");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto written = store.setProp(4242, 7, std::int64_t(1));
        ASSERT_TRUE(written.has_value()) << written.error().message();

        auto loaded = store.load(4242);
        ASSERT_FALSE(loaded.has_value());
        expectBlockError(loaded.error(), BlockError::BlockErrorCode::NotFound);
    }
    dropDb(path);
}

TEST(BlockStoreLinkTest, SetPropChangesAreVisibleToCachedLoads) {
    auto path = tempDb("prop", "cache-refresh");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto id = store.createBlock(0, 1, "cached");
        ASSERT_TRUE(id.has_value()) << id.error().message();
        ASSERT_TRUE(store.setProp(*id, 5, std::int64_t(1)).has_value());

        auto first = store.load(*id);
        ASSERT_TRUE(first.has_value()) << first.error().message();
        auto before = block_store_link_test_support::findProp(first.value(), 5);
        ASSERT_TRUE(before.has_value());
        EXPECT_EQ(before->intValue, 1);

        ASSERT_TRUE(store.setProp(*id, 5, std::int64_t(9)).has_value());

        auto second = store.load(*id);
        ASSERT_TRUE(second.has_value()) << second.error().message();
        auto after = block_store_link_test_support::findProp(second.value(), 5);
        ASSERT_TRUE(after.has_value());
        EXPECT_EQ(after->intValue, 9);
    }
    dropDb(path);
}

TEST(BlockStoreLinkTest, QueryIntReturnsIdsInsideRangeOnly) {
    auto path = tempDb("query", "int-range");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto low = store.createBlock(0, 1, "low");
        auto mid = store.createBlock(0, 1, "mid");
        auto high = store.createBlock(0, 1, "high");
        ASSERT_TRUE(low.has_value()) << low.error().message();
        ASSERT_TRUE(mid.has_value()) << mid.error().message();
        ASSERT_TRUE(high.has_value()) << high.error().message();

        ASSERT_TRUE(store.setProp(*low, 3, std::int64_t(5)).has_value());
        ASSERT_TRUE(store.setProp(*mid, 3, std::int64_t(15)).has_value());
        ASSERT_TRUE(store.setProp(*high, 3, std::int64_t(25)).has_value());

        auto inside = store.queryInt(3, 10, 20);
        ASSERT_TRUE(inside.has_value()) << inside.error().message();
        EXPECT_EQ(inside.value(), std::vector<BlockId>{*mid});

        auto all = store.queryInt(3, 0, 100);
        ASSERT_TRUE(all.has_value()) << all.error().message();
        EXPECT_EQ(all.value(), (std::vector<BlockId>{*low, *mid, *high}));

        auto capped = store.queryInt(
            static_cast<PropKey>(3), std::int64_t{0}, std::int64_t{100}, std::size_t{2});
        ASSERT_TRUE(capped.has_value()) << capped.error().message();
        EXPECT_EQ(capped.value().size(), 2u);

        auto empty = store.queryInt(3, 100, 200);
        ASSERT_TRUE(empty.has_value()) << empty.error().message();
        EXPECT_EQ(empty.value().size(), 0u);

        auto otherKey = store.queryInt(99, 0, 100);
        ASSERT_TRUE(otherKey.has_value()) << otherKey.error().message();
        EXPECT_EQ(otherKey.value().size(), 0u);
    }
    dropDb(path);
}

TEST(BlockStoreLinkTest, QueryIntWithParentScopesToDirectChildren) {
    auto path = tempDb("query", "int-parent");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto left = store.createBlock(0, 1, "left");
        auto right = store.createBlock(0, 1, "right");
        ASSERT_TRUE(left.has_value()) << left.error().message();
        ASSERT_TRUE(right.has_value()) << right.error().message();

        auto leftChild = store.createBlock(*left, 1, "leftChild");
        auto rightChild = store.createBlock(*right, 1, "rightChild");
        ASSERT_TRUE(leftChild.has_value()) << leftChild.error().message();
        ASSERT_TRUE(rightChild.has_value()) << rightChild.error().message();

        ASSERT_TRUE(store.setProp(*leftChild, 4, std::int64_t(50)).has_value());
        ASSERT_TRUE(store.setProp(*rightChild, 4, std::int64_t(50)).has_value());

        auto fromLeft = store.queryInt(
            *left, static_cast<PropKey>(4), std::int64_t{0}, std::int64_t{100});
        ASSERT_TRUE(fromLeft.has_value()) << fromLeft.error().message();
        EXPECT_EQ(fromLeft.value(), std::vector<BlockId>{*leftChild});

        auto fromRoot = store.queryInt(
            static_cast<BlockId>(0), static_cast<PropKey>(4), std::int64_t{0}, std::int64_t{100});
        ASSERT_TRUE(fromRoot.has_value()) << fromRoot.error().message();
        EXPECT_TRUE(fromRoot.value().empty());
    }
    dropDb(path);
}

TEST(BlockStoreLinkTest, QueryTextMatchesExactValues) {
    auto path = tempDb("query", "text-match");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto apple = store.createBlock(0, 1, "apple");
        auto pear = store.createBlock(0, 1, "pear");
        ASSERT_TRUE(apple.has_value()) << apple.error().message();
        ASSERT_TRUE(pear.has_value()) << pear.error().message();

        ASSERT_TRUE(store.setProp(*apple, 9, std::string_view("fruit")).has_value());
        ASSERT_TRUE(store.setProp(*pear, 9, std::string_view("fruit")).has_value());
        ASSERT_TRUE(store.setProp(*pear, 8, std::string_view("berry")).has_value());

        auto fruits = store.queryText(9, "fruit");
        ASSERT_TRUE(fruits.has_value()) << fruits.error().message();
        EXPECT_EQ(fruits.value(), (std::vector<BlockId>{*apple, *pear}));

        auto berries = store.queryText(8, "berry");
        ASSERT_TRUE(berries.has_value()) << berries.error().message();
        EXPECT_EQ(berries.value(), std::vector<BlockId>{*pear});

        auto none = store.queryText(8, "cherry");
        ASSERT_TRUE(none.has_value()) << none.error().message();
        EXPECT_EQ(none.value().size(), 0u);

        auto fromParent = store.queryText(0, 9, "fruit");
        ASSERT_TRUE(fromParent.has_value()) << fromParent.error().message();
        EXPECT_EQ(fromParent.value(), (std::vector<BlockId>{*apple, *pear}));

        auto fromGhost = store.queryText(77, 9, "fruit");
        ASSERT_TRUE(fromGhost.has_value()) << fromGhost.error().message();
        EXPECT_EQ(fromGhost.value().size(), 0u);
    }
    dropDb(path);
}

TEST(BlockStoreLinkTest, WithQueryBindsTypedParamsPerRow) {
    auto path = tempDb("query", "with-query-params");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        ASSERT_TRUE(store.exec("CREATE TABLE probe(a INTEGER, b REAL, c TEXT)").has_value());

        std::vector<BlockProp> params;
        BlockProp integer{};
        integer.type = PayloadType::Int;
        integer.intValue = 42;
        params.push_back(integer);

        BlockProp real{};
        real.type = PayloadType::Double;
        real.realValue = 1.5;
        params.push_back(real);

        BlockProp text{};
        text.type = PayloadType::Text;
        text.textValue = "hi";
        params.push_back(text);

        int rows = 0;
        auto queried = store.withQuery(
            "probeSelect", "SELECT ?1 AS a, ?2 AS b, ?3 AS c", params,
            [&](SQLite::Statement& stmt) {
                ++rows;
                EXPECT_EQ(stmt.getColumn(0).getInt64(), 42);
                EXPECT_DOUBLE_EQ(stmt.getColumn(1).getDouble(), 1.5);
                EXPECT_EQ(columnToString(stmt.getColumn(2)), "hi");
            });
        ASSERT_TRUE(queried.has_value()) << queried.error().message();
        EXPECT_EQ(rows, 1);
    }
    dropDb(path);
}
