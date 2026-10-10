#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <magic_enum/magic_enum.hpp>

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"
#include "LOICollectionA/data/sqlite/block/TypedTable.h"

#include "common/data/StorageTestSupport.h"

using storage_test_support::dropDb;
using storage_test_support::tempDb;

namespace typed_table_test_model {
    enum class RowCol { name, muted, level };
}

namespace typed_table_test_small {
    enum class RowCol { name, muted };
}

namespace typed_table_test_large {
    enum class RowCol { name, muted, level, extra };
}

namespace typed_table_test_alt {
    enum class AltCol { name, muted, level };
}

namespace LOICollection::data {
    template <>
    struct TypedIndexPolicy<typed_table_test_model::RowCol> {
        static constexpr auto kIndexed =
            makeIndexed<typed_table_test_model::RowCol,
                        typed_table_test_model::RowCol::muted,
                        typed_table_test_model::RowCol::level>();
    };

    template <>
    struct TypedIndexPolicy<typed_table_test_small::RowCol> {
        static constexpr auto kIndexed =
            makeIndexed<typed_table_test_small::RowCol,
                        typed_table_test_small::RowCol::muted>();
    };

    template <>
    struct TypedIndexPolicy<typed_table_test_large::RowCol> {
        static constexpr auto kIndexed =
            makeIndexed<typed_table_test_large::RowCol,
                        typed_table_test_large::RowCol::muted,
                        typed_table_test_large::RowCol::level>();
    };
}

using namespace LOICollection::data;
using Table = TypedTable<typed_table_test_model::RowCol, 1>;
using ModelCol = typed_table_test_model::RowCol;
using Key = Table::Key;

namespace typed_table_test_support {
    template <class T>
    std::vector<T> sorted(std::vector<T> values) {
        std::sort(values.begin(), values.end());
        return values;
    }

    PropKey keyOf(ModelCol col) {
        return static_cast<PropKey>(magic_enum::enum_index(col).value());
    }
}

TEST(TypedTableTest, OpenCreatesRootBlockAndSchemaMeta) {
    auto path = tempDb("typed", "open-root");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto opened = Table::open(**repo, "players");
        ASSERT_TRUE(opened.has_value()) << opened.error().message();

        auto root = (*repo)->store().idOf(0, "players");
        ASSERT_TRUE(root.has_value()) << root.error().message();
        ASSERT_TRUE(root.value().has_value());

        auto schema = (*repo)->metaGet("schema:players");
        ASSERT_TRUE(schema.has_value()) << schema.error().message();
        ASSERT_TRUE(schema.value().has_value());
        EXPECT_FALSE(schema.value()->empty());
    }
    dropDb(path);
}

TEST(TypedTableTest, NameAndVersionExposeTemplateParameters) {
    auto path = tempDb("typed", "meta-accessors");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto opened = Table::open(**repo, "players");
        ASSERT_TRUE(opened.has_value()) << opened.error().message();
        EXPECT_EQ(opened.value().name(), "players");
        EXPECT_EQ(opened.value().version(), 1u);

        auto next = TypedTable<ModelCol, 2>::open(**repo, "roster");
        ASSERT_TRUE(next.has_value()) << next.error().message();
        EXPECT_EQ(next.value().name(), "roster");
        EXPECT_EQ(next.value().version(), 2u);
    }
    dropDb(path);
}

TEST(TypedTableTest, SetGetRoundTripsEveryCellType) {
    auto path = tempDb("typed", "codec-roundtrip");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto opened = Table::open(**repo, "players");
        ASSERT_TRUE(opened.has_value()) << opened.error().message();
        Table& table = *opened;

        ASSERT_TRUE(table.set("r1", ModelCol::name, std::string("alice")).has_value());
        ASSERT_TRUE(table.set("r1", ModelCol::muted, true).has_value());
        ASSERT_TRUE(table.set("r1", ModelCol::level, 42).has_value());
        ASSERT_TRUE(table.set("r1", ModelCol::name, std::string("bob")).has_value());

        auto name = table.get<std::string>("r1", ModelCol::name);
        ASSERT_TRUE(name.has_value()) << name.error().message();
        EXPECT_EQ(*name, "bob");

        auto muted = table.get<bool>("r1", ModelCol::muted);
        ASSERT_TRUE(muted.has_value()) << muted.error().message();
        EXPECT_TRUE(*muted);

        auto level = table.get<int>("r1", ModelCol::level);
        ASSERT_TRUE(level.has_value()) << level.error().message();
        EXPECT_EQ(*level, 42);
    }
    dropDb(path);
}

TEST(TypedTableTest, RealCellsRoundTripWithoutPrecisionLoss) {
    auto path = tempDb("typed", "real-cells");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto opened = Table::open(**repo, "players");
        ASSERT_TRUE(opened.has_value()) << opened.error().message();
        Table& table = *opened;

        ASSERT_TRUE(table.set("r1", ModelCol::level, 2.5).has_value());

        auto level = table.get<double>("r1", ModelCol::level);
        ASSERT_TRUE(level.has_value()) << level.error().message();
        EXPECT_DOUBLE_EQ(*level, 2.5);
    }
    dropDb(path);
}

TEST(TypedTableTest, GetReturnsDefaultsForMissingRowAndColumn) {
    auto path = tempDb("typed", "defaults");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto opened = Table::open(**repo, "players");
        ASSERT_TRUE(opened.has_value()) << opened.error().message();
        Table& table = *opened;

        auto ghostName = table.get<std::string>("ghost", ModelCol::name, "fallback");
        ASSERT_TRUE(ghostName.has_value()) << ghostName.error().message();
        EXPECT_EQ(*ghostName, "fallback");

        auto ghostLevel = table.get<int>("ghost", ModelCol::level, -1);
        ASSERT_TRUE(ghostLevel.has_value()) << ghostLevel.error().message();
        EXPECT_EQ(*ghostLevel, -1);

        ASSERT_TRUE(table.set("r1", ModelCol::name, std::string("only-name")).has_value());

        auto missingCell = table.get<int>("r1", ModelCol::level, -1);
        ASSERT_TRUE(missingCell.has_value()) << missingCell.error().message();
        EXPECT_EQ(*missingCell, -1);
    }
    dropDb(path);
}

TEST(TypedTableTest, ReopenKeepsRowsAndCells) {
    auto path = tempDb("typed", "reopen-rows");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto opened = Table::open(**repo, "players");
        ASSERT_TRUE(opened.has_value()) << opened.error().message();
        ASSERT_TRUE(opened.value().set("r1", ModelCol::name, std::string("alice")).has_value());
        ASSERT_TRUE(opened.value().set("r1", ModelCol::level, 42).has_value());
    }

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto reopened = Table::open(**repo, "players");
        ASSERT_TRUE(reopened.has_value()) << reopened.error().message();

        auto name = reopened.value().get<std::string>("r1", ModelCol::name);
        ASSERT_TRUE(name.has_value()) << name.error().message();
        EXPECT_EQ(*name, "alice");

        auto level = reopened.value().get<int>("r1", ModelCol::level);
        ASSERT_TRUE(level.has_value()) << level.error().message();
        EXPECT_EQ(*level, 42);
    }
    dropDb(path);
}

TEST(TypedTableTest, HasTracksRowExistence) {
    auto path = tempDb("typed", "has");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto opened = Table::open(**repo, "players");
        ASSERT_TRUE(opened.has_value()) << opened.error().message();
        Table& table = *opened;

        auto ghost = table.has("ghost");
        ASSERT_TRUE(ghost.has_value()) << ghost.error().message();
        EXPECT_FALSE(*ghost);

        ASSERT_TRUE(table.set("r1", ModelCol::name, std::string("alice")).has_value());

        auto present = table.has("r1");
        ASSERT_TRUE(present.has_value()) << present.error().message();
        EXPECT_TRUE(*present);

        ASSERT_TRUE(table.del("r1").has_value());

        auto deleted = table.has("r1");
        ASSERT_TRUE(deleted.has_value()) << deleted.error().message();
        EXPECT_FALSE(*deleted);
    }
    dropDb(path);
}

TEST(TypedTableTest, DelRemovesRowFromListAndFind) {
    auto path = tempDb("typed", "del");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto opened = Table::open(**repo, "players");
        ASSERT_TRUE(opened.has_value()) << opened.error().message();
        Table& table = *opened;

        ASSERT_TRUE(table.set("r1", ModelCol::name, std::string("alice")).has_value());
        ASSERT_TRUE(table.set("r1", ModelCol::muted, 1).has_value());
        ASSERT_TRUE(table.set("r2", ModelCol::name, std::string("bob")).has_value());
        ASSERT_TRUE(table.set("r2", ModelCol::muted, 1).has_value());

        ASSERT_TRUE(table.del("r1").has_value());
        ASSERT_TRUE(table.del("ghost").has_value());

        auto listed = table.list();
        ASSERT_TRUE(listed.has_value()) << listed.error().message();
        EXPECT_EQ(typed_table_test_support::sorted(listed.value()), std::vector<std::string>{"r2"});

        auto muted = table.find(FindMode::And, {{Key{ModelCol::muted}, "1"}});
        ASSERT_TRUE(muted.has_value()) << muted.error().message();
        EXPECT_EQ(muted.value(), std::vector<std::string>{"r2"});
    }
    dropDb(path);
}

TEST(TypedTableTest, ListReturnsEveryRowKey) {
    auto path = tempDb("typed", "list");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto opened = Table::open(**repo, "players");
        ASSERT_TRUE(opened.has_value()) << opened.error().message();
        Table& table = *opened;

        for (auto row : {"r3", "r1", "r2"})
            ASSERT_TRUE(table.set(row, ModelCol::name, std::string("user")).has_value());

        auto listed = table.list();
        ASSERT_TRUE(listed.has_value()) << listed.error().message();
        EXPECT_EQ(typed_table_test_support::sorted(listed.value()),
                  (std::vector<std::string>{"r1", "r2", "r3"}));
    }
    dropDb(path);
}

TEST(TypedTableTest, FindWithEmptyConditionsListsAllRows) {
    auto path = tempDb("typed", "find-empty");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto opened = Table::open(**repo, "players");
        ASSERT_TRUE(opened.has_value()) << opened.error().message();
        Table& table = *opened;

        ASSERT_TRUE(table.set("r1", ModelCol::name, std::string("alice")).has_value());
        ASSERT_TRUE(table.set("r2", ModelCol::name, std::string("bob")).has_value());

        auto all = table.find(FindMode::And, {});
        ASSERT_TRUE(all.has_value()) << all.error().message();
        EXPECT_EQ(typed_table_test_support::sorted(all.value()),
                  (std::vector<std::string>{"r1", "r2"}));
    }
    dropDb(path);
}

TEST(TypedTableTest, FindAndIntersectsAndFindOrUnions) {
    auto path = tempDb("typed", "find-modes");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto opened = Table::open(**repo, "players");
        ASSERT_TRUE(opened.has_value()) << opened.error().message();
        Table& table = *opened;

        ASSERT_TRUE(table.set("r1", ModelCol::muted, 1).has_value());
        ASSERT_TRUE(table.set("r2", ModelCol::muted, 1).has_value());
        ASSERT_TRUE(table.set("r2", ModelCol::level, 5).has_value());
        ASSERT_TRUE(table.set("r3", ModelCol::level, 5).has_value());

        auto both = table.find(FindMode::And, {{Key{ModelCol::muted}, "1"}, {Key{ModelCol::level}, "5"}});
        ASSERT_TRUE(both.has_value()) << both.error().message();
        EXPECT_EQ(both.value(), std::vector<std::string>{"r2"});

        auto either = table.find(FindMode::Or, {{Key{ModelCol::muted}, "1"}, {Key{ModelCol::level}, "5"}});
        ASSERT_TRUE(either.has_value()) << either.error().message();
        EXPECT_EQ(typed_table_test_support::sorted(either.value()),
                  (std::vector<std::string>{"r1", "r2", "r3"}));
    }
    dropDb(path);
}

TEST(TypedTableTest, FindAgreesBetweenIndexedAndUnindexedColumns) {
    auto path = tempDb("typed", "find-index-parity");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto opened = Table::open(**repo, "players");
        ASSERT_TRUE(opened.has_value()) << opened.error().message();
        Table& table = *opened;

        for (auto row : {"r1", "r2"}) {
            ASSERT_TRUE(table.set(row, ModelCol::name, std::string("same")).has_value());
            ASSERT_TRUE(table.set(row, ModelCol::muted, 1).has_value());
        }

        auto byIndexed = table.find(FindMode::And, {{Key{ModelCol::muted}, "1"}});
        ASSERT_TRUE(byIndexed.has_value()) << byIndexed.error().message();

        auto byUnindexed = table.find(FindMode::And, {{Key{ModelCol::name}, "same"}});
        ASSERT_TRUE(byUnindexed.has_value()) << byUnindexed.error().message();

        EXPECT_EQ(typed_table_test_support::sorted(byIndexed.value()),
                  typed_table_test_support::sorted(byUnindexed.value()));
    }
    dropDb(path);
}

TEST(TypedTableTest, FindAndFallsBackToPayloadWhenSideTableLacksColumn) {
    auto path = tempDb("typed", "find-payload-fallback");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto opened = Table::open(**repo, "players");
        ASSERT_TRUE(opened.has_value()) << opened.error().message();
        Table& table = *opened;

        ASSERT_TRUE(table.set("r1", ModelCol::name, std::string("alice")).has_value());
        ASSERT_TRUE(table.set("r1", ModelCol::level, 7).has_value());

        auto byLevel = table.find(FindMode::And, {{Key{ModelCol::level}, "7"}});
        ASSERT_TRUE(byLevel.has_value()) << byLevel.error().message();
        EXPECT_EQ(byLevel.value(), std::vector<std::string>{"r1"});
    }
    dropDb(path);
}

TEST(TypedTableTest, FindFirstReturnsSingleKeyOrEmptyString) {
    auto path = tempDb("typed", "find-first");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto opened = Table::open(**repo, "players");
        ASSERT_TRUE(opened.has_value()) << opened.error().message();
        Table& table = *opened;

        ASSERT_TRUE(table.set("r1", ModelCol::muted, 1).has_value());

        auto present = table.findFirst(FindMode::And, {{Key{ModelCol::muted}, "1"}});
        ASSERT_TRUE(present.has_value()) << present.error().message();
        EXPECT_EQ(*present, "r1");

        auto absent = table.findFirst(FindMode::And, {{Key{ModelCol::muted}, "9"}});
        ASSERT_TRUE(absent.has_value()) << absent.error().message();
        EXPECT_EQ(*absent, std::string{});
    }
    dropDb(path);
}

TEST(TypedTableTest, FindValuesProjectsTheRequestedColumn) {
    auto path = tempDb("typed", "find-values");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto opened = Table::open(**repo, "players");
        ASSERT_TRUE(opened.has_value()) << opened.error().message();
        Table& table = *opened;

        ASSERT_TRUE(table.set("r1", ModelCol::name, std::string("alice")).has_value());
        ASSERT_TRUE(table.set("r1", ModelCol::muted, 1).has_value());
        ASSERT_TRUE(table.set("r1", ModelCol::level, 10).has_value());
        ASSERT_TRUE(table.set("r2", ModelCol::name, std::string("bob")).has_value());
        ASSERT_TRUE(table.set("r2", ModelCol::muted, 1).has_value());
        ASSERT_TRUE(table.set("r2", ModelCol::level, 20).has_value());
        ASSERT_TRUE(table.set("r3", ModelCol::name, std::string("carol")).has_value());
        ASSERT_TRUE(table.set("r3", ModelCol::muted, 0).has_value());

        auto names = table.findValues<std::string>(Key{ModelCol::name}, FindMode::And, {{Key{ModelCol::muted}, "1"}});
        ASSERT_TRUE(names.has_value()) << names.error().message();
        EXPECT_EQ(typed_table_test_support::sorted(names.value()),
                  (std::vector<std::string>{"alice", "bob"}));

        auto levels = table.findValues<int>(Key{ModelCol::level}, FindMode::And, {{Key{ModelCol::muted}, "1"}});
        ASSERT_TRUE(levels.has_value()) << levels.error().message();
        EXPECT_EQ(typed_table_test_support::sorted(
                      std::vector<int>(levels.value().begin(), levels.value().end())),
                  (std::vector<int>{10, 20}));

        auto none = table.findValues<int>(Key{ModelCol::level}, FindMode::And, {{Key{ModelCol::muted}, "9"}});
        ASSERT_TRUE(none.has_value()) << none.error().message();
        EXPECT_EQ(none.value().size(), 0u);
    }
    dropDb(path);
}

TEST(TypedTableTest, GetRowAndSetRowRoundTripCells) {
    auto path = tempDb("typed", "row-map");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto opened = Table::open(**repo, "players");
        ASSERT_TRUE(opened.has_value()) << opened.error().message();
        Table& table = *opened;

        std::unordered_map<std::string, std::string> cells{
            {"name", "alice"},
            {"level", "42"},
        };
        auto written = table.setRow("r1", cells);
        ASSERT_TRUE(written.has_value()) << written.error().message();

        auto row = table.getRow("r1");
        ASSERT_TRUE(row.has_value()) << row.error().message();
        EXPECT_EQ(row.value().size(), 2u);
        EXPECT_EQ(row.value().at("name"), "alice");
        EXPECT_EQ(row.value().at("level"), "42");

        auto ghost = table.getRow("ghost");
        ASSERT_TRUE(ghost.has_value()) << ghost.error().message();
        EXPECT_TRUE(ghost.value().empty());
    }
    dropDb(path);
}

TEST(TypedTableTest, SetRowAndGetRejectUnknownColumns) {
    auto path = tempDb("typed", "unknown-column");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto opened = Table::open(**repo, "players");
        ASSERT_TRUE(opened.has_value()) << opened.error().message();
        Table& table = *opened;

        std::unordered_map<std::string, std::string> cells{{{"nope", "1"}}};
        auto written = table.setRow("r1", cells);
        ASSERT_FALSE(written.has_value());
        EXPECT_NE(written.error().message().find("unknown column"), std::string::npos);

        auto parsed = Table::parseColumn("muted");
        ASSERT_TRUE(parsed.has_value()) << parsed.error().message();
        EXPECT_EQ(*parsed, ModelCol::muted);

        auto bad = Table::parseColumn("zzz");
        ASSERT_FALSE(bad.has_value());
        EXPECT_NE(bad.error().message().find("unknown column"), std::string::npos);
    }
    dropDb(path);
}

TEST(TypedTableTest, MatchesAllAndMatchesAnyEvaluateConditions) {
    std::vector<std::pair<Key, std::string>> conds{
        {Key{ModelCol::muted}, "1"},
        {Key{ModelCol::level}, "5"},
    };

    RowCells empty;
    EXPECT_FALSE(Table::matchesAll(empty, conds));
    EXPECT_FALSE(Table::matchesAny(empty, conds));

    BlockProp muted{};
    muted.type = PayloadType::Text;
    muted.textValue = "1";

    BlockProp level{};
    level.type = PayloadType::Text;
    level.textValue = "5";

    RowCells both;
    both[typed_table_test_support::keyOf(ModelCol::muted)] = muted;
    both[typed_table_test_support::keyOf(ModelCol::level)] = level;
    EXPECT_TRUE(Table::matchesAll(both, conds));
    EXPECT_TRUE(Table::matchesAny(both, conds));

    RowCells partial;
    partial[typed_table_test_support::keyOf(ModelCol::muted)] = muted;
    EXPECT_FALSE(Table::matchesAll(partial, conds));
    EXPECT_TRUE(Table::matchesAny(partial, conds));
}

TEST(TypedTableTest, SchemaVersionMismatchReturnsBlockErrorCode) {
    auto path = tempDb("typed", "version-mismatch");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto seeded = TypedTable<ModelCol, 1>::open(**repo, "players");
        ASSERT_TRUE(seeded.has_value()) << seeded.error().message();

        auto drifted = TypedTable<ModelCol, 2>::open(**repo, "players");
        ASSERT_FALSE(drifted.has_value());
        EXPECT_TRUE(drifted.error().isA<ll::ErrorCodeError>());
        EXPECT_EQ(drifted.error().as<ll::ErrorCodeError>().ec,
                  BlockError::makeErrorCode(BlockError::BlockErrorCode::SchemaMismatch));
    }
    dropDb(path);
}

TEST(TypedTableTest, SchemaTypeMismatchReturnsBlockErrorCode) {
    auto path = tempDb("typed", "type-mismatch");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto seeded = TypedTable<ModelCol, 1>::open(**repo, "players");
        ASSERT_TRUE(seeded.has_value()) << seeded.error().message();

        auto drifted = TypedTable<typed_table_test_alt::AltCol, 1>::open(**repo, "players");
        ASSERT_FALSE(drifted.has_value());
        EXPECT_TRUE(drifted.error().isA<ll::ErrorCodeError>());
        EXPECT_EQ(drifted.error().as<ll::ErrorCodeError>().ec,
                  BlockError::makeErrorCode(BlockError::BlockErrorCode::SchemaMismatch));
    }
    dropDb(path);
}

TEST(TypedTableTest, ColumnDriftRebuildsSideTableAndKeepsCells) {
    auto path = tempDb("typed", "column-drift");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto seeded = TypedTable<typed_table_test_small::RowCol, 1>::open(**repo, "players");
        ASSERT_TRUE(seeded.has_value()) << seeded.error().message();
        ASSERT_TRUE(seeded.value().set("r1", typed_table_test_small::RowCol::name, std::string("alice")).has_value());
        ASSERT_TRUE(seeded.value().set("r1", typed_table_test_small::RowCol::muted, 1).has_value());

        auto widened = TypedTable<typed_table_test_large::RowCol, 1>::open(**repo, "players");
        ASSERT_TRUE(widened.has_value()) << widened.error().message();

        auto name = widened.value().get<std::string>("r1", typed_table_test_large::RowCol::name);
        ASSERT_TRUE(name.has_value()) << name.error().message();
        EXPECT_EQ(*name, "alice");

        auto muted = widened.value().get<int>("r1", typed_table_test_large::RowCol::muted);
        ASSERT_TRUE(muted.has_value()) << muted.error().message();
        EXPECT_EQ(*muted, 1);
    }
    dropDb(path);
}

TEST(TypedTableTest, SideTableFingerprintDriftRebuildsFromSnapshot) {
    auto path = tempDb("typed", "fingerprint-drift");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto opened = Table::open(**repo, "players");
        ASSERT_TRUE(opened.has_value()) << opened.error().message();
        ASSERT_TRUE(opened.value().set("r1", ModelCol::name, std::string("alice")).has_value());
        ASSERT_TRUE(opened.value().set("r1", ModelCol::level, 42).has_value());

        ASSERT_TRUE((*repo)->metaSet("sidecol:players", "bogus").has_value());

        auto reopened = Table::open(**repo, "players");
        ASSERT_TRUE(reopened.has_value()) << reopened.error().message();

        auto name = reopened.value().get<std::string>("r1", ModelCol::name);
        ASSERT_TRUE(name.has_value()) << name.error().message();
        EXPECT_EQ(*name, "alice");

        auto level = reopened.value().get<int>("r1", ModelCol::level);
        ASSERT_TRUE(level.has_value()) << level.error().message();
        EXPECT_EQ(*level, 42);

        auto fingerprint = (*repo)->metaGet("sidecol:players");
        ASSERT_TRUE(fingerprint.has_value()) << fingerprint.error().message();
        ASSERT_TRUE(fingerprint.value().has_value());
        EXPECT_NE(*fingerprint.value(), "bogus");
    }
    dropDb(path);
}

TEST(TypedTableTest, BatchTxCommitsPendingRowsAndCells) {
    auto path = tempDb("typed", "tx-commit");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto opened = Table::open(**repo, "players");
        ASSERT_TRUE(opened.has_value()) << opened.error().message();
        Table& table = *opened;

        auto tx = table.tx();
        ASSERT_TRUE(tx.has_value()) << tx.error().message();

        ASSERT_TRUE(tx.value().set("r1", ModelCol::name, std::string("alice")).has_value());
        ASSERT_TRUE(tx.value().set("r1", ModelCol::level, 42).has_value());

        auto pending = tx.value().get<std::string>("r1", ModelCol::name);
        ASSERT_TRUE(pending.has_value()) << pending.error().message();
        EXPECT_EQ(*pending, "alice");

        ASSERT_TRUE(tx.value().commit().has_value());

        auto name = table.get<std::string>("r1", ModelCol::name);
        ASSERT_TRUE(name.has_value()) << name.error().message();
        EXPECT_EQ(*name, "alice");

        auto level = table.get<int>("r1", ModelCol::level);
        ASSERT_TRUE(level.has_value()) << level.error().message();
        EXPECT_EQ(*level, 42);
    }
    dropDb(path);
}

TEST(TypedTableTest, BatchTxRollbackDiscardsPendingRows) {
    auto path = tempDb("typed", "tx-rollback");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto opened = Table::open(**repo, "players");
        ASSERT_TRUE(opened.has_value()) << opened.error().message();
        Table& table = *opened;

        auto tx = table.tx();
        ASSERT_TRUE(tx.has_value()) << tx.error().message();

        ASSERT_TRUE(tx.value().set("r1", ModelCol::name, std::string("alice")).has_value());
        ASSERT_TRUE(tx.value().rollback().has_value());

        auto ghost = table.has("r1");
        ASSERT_TRUE(ghost.has_value()) << ghost.error().message();
        EXPECT_FALSE(*ghost);
    }
    dropDb(path);
}

TEST(TypedTableTest, BatchTxDelHidesRowAndMergesPersistedCells) {
    auto path = tempDb("typed", "tx-del");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto opened = Table::open(**repo, "players");
        ASSERT_TRUE(opened.has_value()) << opened.error().message();
        Table& table = *opened;

        ASSERT_TRUE(table.set("r1", ModelCol::name, std::string("alice")).has_value());
        ASSERT_TRUE(table.set("r1", ModelCol::level, 5).has_value());

        auto tx = table.tx();
        ASSERT_TRUE(tx.has_value()) << tx.error().message();

        ASSERT_TRUE(tx.value().del("r1").has_value());

        auto deleted = tx.value().has("r1");
        ASSERT_TRUE(deleted.has_value()) << deleted.error().message();
        EXPECT_FALSE(*deleted);

        ASSERT_TRUE(tx.value().set("r2", ModelCol::level, 9).has_value());

        auto merged = tx.value().get<std::string>("r2", ModelCol::name, "fallback");
        ASSERT_TRUE(merged.has_value()) << merged.error().message();

        ASSERT_TRUE(tx.value().commit().has_value());

        auto gone = table.has("r1");
        ASSERT_TRUE(gone.has_value()) << gone.error().message();
        EXPECT_FALSE(*gone);

        auto fresh = table.has("r2");
        ASSERT_TRUE(fresh.has_value()) << fresh.error().message();
        EXPECT_TRUE(*fresh);
    }
    dropDb(path);
}

TEST(TypedTableTest, BatchTxGetFallsBackToPersistedCells) {
    auto path = tempDb("typed", "tx-pending-reads");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto opened = Table::open(**repo, "players");
        ASSERT_TRUE(opened.has_value()) << opened.error().message();
        Table& table = *opened;

        ASSERT_TRUE(table.set("r1", ModelCol::level, 5).has_value());

        auto tx = table.tx();
        ASSERT_TRUE(tx.has_value()) << tx.error().message();

        auto persisted = tx.value().get<int>("r1", ModelCol::level, -1);
        ASSERT_TRUE(persisted.has_value()) << persisted.error().message();
        EXPECT_EQ(*persisted, 5);

        ASSERT_TRUE(tx.value().set("r1", ModelCol::level, 9).has_value());

        auto pending = tx.value().get<int>("r1", ModelCol::level, -1);
        ASSERT_TRUE(pending.has_value()) << pending.error().message();
        EXPECT_EQ(*pending, 9);

        ASSERT_TRUE(tx.value().rollback().has_value());

        auto reverted = table.get<int>("r1", ModelCol::level, -1);
        ASSERT_TRUE(reverted.has_value()) << reverted.error().message();
        EXPECT_EQ(*reverted, 5);
    }
    dropDb(path);
}

TEST(TypedTableTest, BatchTxSetAcceptsStringColumns) {
    auto path = tempDb("typed", "tx-string-column");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto opened = Table::open(**repo, "players");
        ASSERT_TRUE(opened.has_value()) << opened.error().message();
        Table& table = *opened;

        auto tx = table.tx();
        ASSERT_TRUE(tx.has_value()) << tx.error().message();

        ASSERT_TRUE(tx.value().set("r1", "level", 7).has_value());
        ASSERT_TRUE(tx.value().commit().has_value());

        auto level = table.get<int>("r1", ModelCol::level, -1);
        ASSERT_TRUE(level.has_value()) << level.error().message();
        EXPECT_EQ(*level, 7);
    }
    dropDb(path);
}

TEST(TypedTableTest, ClearTypedTableMarksEveryRowDeleted) {
    auto path = tempDb("typed", "clear");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto opened = Table::open(**repo, "players");
        ASSERT_TRUE(opened.has_value()) << opened.error().message();
        Table& table = *opened;

        ASSERT_TRUE(table.set("r1", ModelCol::name, std::string("alice")).has_value());
        ASSERT_TRUE(table.set("r2", ModelCol::name, std::string("bob")).has_value());

        auto cleared = clearTypedTable(**repo, "players");
        ASSERT_TRUE(cleared.has_value()) << cleared.error().message();

        auto listed = table.list();
        ASSERT_TRUE(listed.has_value()) << listed.error().message();
        EXPECT_TRUE(listed.value().empty());

        auto gone = table.has("r1");
        ASSERT_TRUE(gone.has_value()) << gone.error().message();
        EXPECT_FALSE(*gone);
    }
    dropDb(path);
}
