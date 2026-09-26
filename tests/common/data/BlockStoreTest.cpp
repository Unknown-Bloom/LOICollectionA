#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SQLiteCpp/SQLiteCpp.h>

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"
#include "LOICollectionA/data/sqlite/block/TypedTable.h"

namespace BlockStoreTestModel {

    enum class RowCol { name, muted, level };

}

namespace LOICollection::data {
    template <>
    struct TypedIndexPolicy<BlockStoreTestModel::RowCol> {
        static constexpr auto kIndexed =
            makeIndexed<BlockStoreTestModel::RowCol, BlockStoreTestModel::RowCol::muted, BlockStoreTestModel::RowCol::level>();
    };
}

namespace {

    using BlockStoreTestModel::RowCol;
    using Table = LOICollection::data::TypedTable<RowCol, 1>;
    using Key = Table::Key;
    using FindMode = LOICollection::data::FindMode;

    std::filesystem::path tempDb(std::string_view tag) {
        auto base = std::filesystem::temp_directory_path() / ("loicollectiona-" + std::string(tag) + ".db");
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

    std::vector<std::string> indexColumns(std::filesystem::path const& db, std::string_view index) {
        SQLite::Database conn(db.string(), SQLite::OPEN_READWRITE);
        SQLite::Statement query(conn, "SELECT name FROM pragma_index_info(?)");
        query.bind(1, std::string(index));
        std::vector<std::string> columns;
        while (query.executeStep())
            columns.emplace_back(query.getColumn(0).getString());
        return columns;
    }

    int indexRootPage(std::filesystem::path const& db, std::string_view index) {
        SQLite::Database conn(db.string(), SQLite::OPEN_READWRITE);
        SQLite::Statement query(conn, "SELECT rootpage FROM sqlite_master WHERE type='index' AND name=?");
        query.bind(1, std::string(index));
        return query.executeStep() ? query.getColumn(0).getInt() : -1;
    }

    const std::vector<std::string> kIntCovering{"key", "ival", "block_id"};
    const std::vector<std::string> kTextCovering{"key", "tval", "block_id"};

}

TEST(BlockStoreTest, SchemaCreatesCoveringPropIndexes) {
    auto path = tempDb("covering");
    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << (repo.has_value() ? "" : repo.error().message());
    }
    EXPECT_EQ(indexColumns(path, "idx_prop_key_ival"), kIntCovering);
    EXPECT_EQ(indexColumns(path, "idx_prop_key_tval"), kTextCovering);
    EXPECT_EQ(indexColumns(path, "idx_block_parent_kind"), (std::vector<std::string>{"parent", "kind"}));
    dropDb(path);
}

TEST(BlockStoreTest, SchemaUpgradesLegacyPropIndexes) {
    auto path = tempDb("legacy-indexes");
    {
        SQLite::Database db(path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec(
            "CREATE TABLE block(id INTEGER PRIMARY KEY AUTOINCREMENT, parent INTEGER NOT NULL DEFAULT 0,"
            "name TEXT NOT NULL, kind INTEGER NOT NULL DEFAULT 0, state INTEGER NOT NULL DEFAULT 1,"
            "payload BLOB, created INTEGER NOT NULL, updated INTEGER NOT NULL)");
        db.exec(
            "CREATE TABLE prop(block_id INTEGER NOT NULL, key INTEGER NOT NULL, type INTEGER NOT NULL,"
            "ival INTEGER NOT NULL DEFAULT 0, rval REAL NOT NULL DEFAULT 0, tval TEXT,"
            "PRIMARY KEY(block_id,key))");
        db.exec("CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT)");
        db.exec("CREATE INDEX idx_prop_key_ival ON prop(key, ival)");
        db.exec("CREATE INDEX idx_prop_key_tval ON prop(key, tval)");
    }
    ASSERT_EQ(indexColumns(path, "idx_prop_key_ival"), (std::vector<std::string>{"key", "ival"}));

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << (repo.has_value() ? "" : repo.error().message());
    }

    EXPECT_EQ(indexColumns(path, "idx_prop_key_ival"), kIntCovering);
    EXPECT_EQ(indexColumns(path, "idx_prop_key_tval"), kTextCovering);
    dropDb(path);
}

TEST(BlockStoreTest, SchemaUpgradeIsIdempotent) {
    auto path = tempDb("idempotent");
    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value());
    }
    auto first = indexRootPage(path, "idx_prop_key_tval");
    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value());
    }
    auto second = indexRootPage(path, "idx_prop_key_tval");

    EXPECT_NE(first, -1);
    EXPECT_EQ(first, second) << "index was rebuilt on every open";
    EXPECT_EQ(indexColumns(path, "idx_prop_key_tval"), kTextCovering);
    dropDb(path);
}

TEST(BlockStoreTest, ChildrenKindFilterMatchesPostFilter) {
    auto path = tempDb("children-kind");
    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value());
        auto& store = (*repo)->store();

        auto root = store.createBlock(0, 0, "root");
        ASSERT_TRUE(root.has_value());
        for (int i = 0; i < 60; ++i) {
            auto child = store.createBlock(root.value(), i % 3, "child" + std::to_string(i));
            ASSERT_TRUE(child.has_value());
        }

        auto all = store.children(root.value());
        ASSERT_TRUE(all.has_value());
        EXPECT_EQ(all.value().size(), 60u);

        for (std::int32_t kind = 0; kind < 3; ++kind) {
            auto filtered = store.children(root.value(), kind);
            ASSERT_TRUE(filtered.has_value());
            std::size_t expected = 0;
            for (int i = 0; i < 60; ++i)
                if (i % 3 == kind)
                    ++expected;
            EXPECT_EQ(filtered.value().size(), expected) << "kind=" << kind;
        }
    }
    dropDb(path);
}

TEST(BlockStoreTest, QueryTextNamesAllMatchesManualIntersection) {
    auto path = tempDb("multi-condition");
    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value());
        auto& store = (*repo)->store();

        auto root = store.createBlock(0, 0, "root");
        ASSERT_TRUE(root.has_value());
        for (int i = 0; i < 240; ++i) {
            auto row = store.createBlock(root.value(), 0, "row" + std::to_string(i));
            ASSERT_TRUE(row.has_value());
            ASSERT_TRUE(store.setProp(row.value(), 1, (i % 2 == 0) ? std::string("a") : std::string("b")).has_value());
            ASSERT_TRUE(store.setProp(row.value(), 2, (i % 3 == 0) ? std::string("x") : std::string("y")).has_value());
        }

        auto left = store.queryTextNames(root.value(), 1, "a", 0);
        auto right = store.queryTextNames(root.value(), 2, "x", 0);
        ASSERT_TRUE(left.has_value());
        ASSERT_TRUE(right.has_value());

        auto pushed = store.queryTextNamesAll(root.value(), {{1, "a"}, {2, "x"}}, 0);
        ASSERT_TRUE(pushed.has_value());

        using Row = std::pair<BlockId, std::string>;
        auto byId = [](Row const& a, Row const& b) { return a.first < b.first; };
        std::vector<Row> expected;
        std::set_intersection(
            left.value().begin(), left.value().end(),
            right.value().begin(), right.value().end(),
            std::back_inserter(expected), byId);

        EXPECT_EQ(pushed.value(), expected);
        EXPECT_FALSE(expected.empty());
    }
    dropDb(path);
}

TEST(BlockStoreTest, FindAndMatchesManualIntersection) {
    auto path = tempDb("find-and");
    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value());

        auto opened = Table::open(**repo, "rows");
        ASSERT_TRUE(opened.has_value()) << (opened.has_value() ? "" : opened.error().message());
        Table& table = *opened;

        for (int i = 0; i < 300; ++i) {
            std::string row = "row" + std::to_string(i);
            ASSERT_TRUE(table.set(row, RowCol::name, std::string("user") + std::to_string(i)).has_value());
            ASSERT_TRUE(table.set(row, RowCol::muted, (i % 2 == 0) ? 1 : 0).has_value());
            ASSERT_TRUE(table.set(row, RowCol::level, i % 5).has_value());
        }

        auto both = table.find(FindMode::And, {{Key{RowCol::muted}, "1"}, {Key{RowCol::level}, "3"}});
        auto muted = table.find(FindMode::And, {{Key{RowCol::muted}, "1"}});
        auto level = table.find(FindMode::And, {{Key{RowCol::level}, "3"}});
        ASSERT_TRUE(both.has_value());
        ASSERT_TRUE(muted.has_value());
        ASSERT_TRUE(level.has_value());

        std::vector<std::string> a = muted.value();
        std::vector<std::string> b = level.value();
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        std::vector<std::string> expected;
        std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(expected));

        std::vector<std::string> actual = both.value();
        std::sort(actual.begin(), actual.end());

        EXPECT_EQ(actual, expected);
        EXPECT_FALSE(expected.empty());
    }
    dropDb(path);
}

namespace {
    const std::string kProbePayload = "PAYLOAD-0123456789";

    std::string payloadText(std::vector<std::byte> const& bytes) {
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    BlockId seedProbe(std::filesystem::path const& path, BlockId& rootId) {
        auto repo = BlockRepository::open(path.string(), 2);
        if (!repo.has_value())
            return 0;
        auto& store = (*repo)->store();

        auto root = store.createBlock(0, 4242, "root");
        if (!root.has_value())
            return 0;
        rootId = root.value();

        auto probe = store.createBlock(rootId, 777, "probe", kProbePayload);
        if (!probe.has_value())
            return 0;

        BlockId id = probe.value();
        if (!store.setProp(id, 1, std::string("alpha")).has_value())
            return 0;
        if (!store.setProp(id, 2, std::int64_t(20260926)).has_value())
            return 0;
        return id;
    }

    void expectProbeRecord(BlockRecord const& r, std::string_view tag, BlockId id, BlockId rootId) {
        EXPECT_EQ(r.id, id) << tag;
        EXPECT_EQ(r.parent, rootId) << tag;
        EXPECT_EQ(r.name, "probe") << tag;
        EXPECT_EQ(r.kind, 777) << tag;
        EXPECT_EQ(r.state, BlockLifecycle::Active) << tag;
        EXPECT_GT(r.created, 0) << tag;
        EXPECT_GT(r.updated, 0) << tag;
        EXPECT_EQ(payloadText(r.payload), kProbePayload) << tag;
    }

    void expectProbeProps(std::vector<BlockProp> const& props, std::string_view tag) {
        ASSERT_EQ(props.size(), 2u) << tag;
        std::optional<std::string> text;
        std::optional<std::int64_t> number;
        for (auto const& p : props) {
            if (p.key == 1)
                text = p.textValue;
            if (p.key == 2)
                number = p.intValue;
        }
        ASSERT_TRUE(text.has_value()) << tag;
        ASSERT_TRUE(number.has_value()) << tag;
        EXPECT_EQ(*text, "alpha") << tag;
        EXPECT_EQ(*number, 20260926) << tag;
    }
}

TEST(BlockStoreTest, ReadPathsAgreeOnBlockColumnOrder) {
    auto path = tempDb("column-order");
    BlockId rootId = 0;
    BlockId probeId = seedProbe(path, rootId);
    ASSERT_NE(probeId, 0);
    ASSERT_NE(rootId, 0);

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value());
        auto byId = (*repo)->store().load(probeId);
        ASSERT_TRUE(byId.has_value()) << byId.error().message();
        expectProbeRecord(byId.value(), "load(id) / getBlockById", probeId, rootId);
        expectProbeProps(byId.value().props, "load(id) / getBlockById");
    }

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value());
        auto byName = (*repo)->store().load(rootId, "probe");
        ASSERT_TRUE(byName.has_value()) << byName.error().message();
        expectProbeRecord(byName.value(), "load(parent,name) / getBlockByName", probeId, rootId);
        expectProbeProps(byName.value().props, "load(parent,name) / getBlockByName");
    }

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value());
        bool visited = false;
        auto viewed = (*repo)->store().withBlock(probeId, [&](BlockView const& v) {
            visited = true;
            EXPECT_EQ(v.id, probeId) << "withBlock / getBlockById";
            EXPECT_EQ(v.parent, rootId) << "withBlock / getBlockById";
            EXPECT_EQ(std::string(v.name), "probe") << "withBlock / getBlockById";
            EXPECT_EQ(v.kind, 777) << "withBlock / getBlockById";
            EXPECT_EQ(v.state, BlockLifecycle::Active) << "withBlock / getBlockById";
            EXPECT_GT(v.created, 0) << "withBlock / getBlockById";
            EXPECT_GT(v.updated, 0) << "withBlock / getBlockById";
            EXPECT_EQ(std::string(v.payload), kProbePayload) << "withBlock / getBlockById";
        });
        ASSERT_TRUE(viewed.has_value()) << viewed.error().message();
        EXPECT_TRUE(visited);
    }

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value());
        auto rows = (*repo)->store().records(rootId);
        ASSERT_TRUE(rows.has_value()) << rows.error().message();
        ASSERT_EQ(rows.value().size(), 1u);
        expectProbeRecord(rows.value().front(), "records / getChildrenFull", probeId, rootId);
        expectProbeProps(rows.value().front().props, "records / getChildrenFull");
    }

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value());
        std::vector<BlockId> ids{probeId};
        auto rows = (*repo)->store().rowsByIds(ids, true);
        ASSERT_TRUE(rows.has_value()) << rows.error().message();
        ASSERT_EQ(rows.value().size(), 1u);
        expectProbeRecord(rows.value().front(), "rowsByIds", probeId, rootId);
        expectProbeProps(rows.value().front().props, "rowsByIds");
    }

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value());
        auto& store = (*repo)->store();

        auto named = store.childNames(rootId);
        ASSERT_TRUE(named.has_value()) << named.error().message();
        ASSERT_EQ(named.value().size(), 1u);
        EXPECT_EQ(named.value().front().first, probeId) << "childNames / listChildNames";
        EXPECT_EQ(named.value().front().second, "probe") << "childNames / listChildNames";

        auto resolved = store.idOf(rootId, "probe");
        ASSERT_TRUE(resolved.has_value()) << resolved.error().message();
        ASSERT_TRUE(resolved.value().has_value());
        EXPECT_EQ(*resolved.value(), probeId) << "idOf / getIdByName";

        auto state = store.stateOf(probeId);
        ASSERT_TRUE(state.has_value()) << state.error().message();
        EXPECT_EQ(state.value(), BlockLifecycle::Active) << "stateOf / getState";
    }

    dropDb(path);
}

TEST(BlockStoreTest, IndexedColumnSurvivesUpdateOfUnindexedColumn) {
    auto path = tempDb("mirror-survives");
    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value());

        auto opened = Table::open(**repo, "rows");
        ASSERT_TRUE(opened.has_value());
        Table& table = *opened;

        for (int i = 0; i < 200; ++i) {
            std::string row = "row" + std::to_string(i);
            ASSERT_TRUE(table.set(row, RowCol::name, std::string("user") + std::to_string(i)).has_value());
            ASSERT_TRUE(table.set(row, RowCol::muted, (i % 7 == 0) ? 1 : 0).has_value());
        }

        auto before = table.find(FindMode::And, {{Key{RowCol::muted}, "1"}});
        ASSERT_TRUE(before.has_value());
        ASSERT_FALSE(before.value().empty());

        for (int i = 0; i < 200; ++i)
            ASSERT_TRUE(table.set("row" + std::to_string(i), RowCol::name, std::string("renamed")).has_value());

        auto after = table.find(FindMode::And, {{Key{RowCol::muted}, "1"}});
        ASSERT_TRUE(after.has_value());
        EXPECT_EQ(after.value().size(), before.value().size())
            << "updating an unindexed column must not clear the indexed column mirror";
    }
    dropDb(path);
}
