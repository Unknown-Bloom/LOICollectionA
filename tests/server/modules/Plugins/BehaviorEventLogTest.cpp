#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "LOICollectionA/data/sqlite/block/BlockStore.h"
#include "LOICollectionA/data/sqlite/connection/ConnectionPool.h"

#include "LOICollectionA/include/server/Plugins/BehaviorEvent/BehaviorEventLog.h"

class BehaviorEventLogTest : public testing::Test {
protected:
    static void SetUpTestSuite() {
        tempDir = std::filesystem::temp_directory_path() / "event_log_test";

        std::filesystem::create_directories(tempDir);

        pool = std::make_shared<ConnectionPool>((tempDir / "events.db").string(), 2);

        auto store = BlockStore::create(pool);
        ASSERT_TRUE(store.has_value());

        storage = std::move(store.value());
    }

    static void TearDownTestSuite() {
        storage.reset();
        pool.reset();

        std::filesystem::remove_all(tempDir);
    }

    static std::unique_ptr<BehaviorEventLog> makeLog(std::string_view root) {
        auto log = BehaviorEventLog::create(*storage, root);
        if (!log.has_value())
            return nullptr;

        return std::move(log.value());
    }

    static BehaviorEventLog::PreparedEvent makeEvent(
        std::string_view name,
        std::string_view type,
        std::int64_t timestamp,
        std::int64_t x,
        std::int64_t y,
        std::int64_t z,
        std::int64_t dimension) {
        BehaviorEventLog::PreparedEvent event;
        event.name = std::string(name);
        event.type = std::string(type);
        event.timestamp = timestamp;
        event.posX = x;
        event.posY = y;
        event.posZ = z;
        event.dimension = dimension;
        event.fields.emplace_back("event_time", "2026-01-01 00:00:00");
        event.fields.emplace_back("player_name", "loitietu");

        return event;
    }

    static std::filesystem::path tempDir;
    static std::shared_ptr<ConnectionPool> pool;
    static std::shared_ptr<BlockStore> storage;
};

std::filesystem::path BehaviorEventLogTest::tempDir;
std::shared_ptr<ConnectionPool> BehaviorEventLogTest::pool;
std::shared_ptr<BlockStore> BehaviorEventLogTest::storage;

TEST_F(BehaviorEventLogTest, AppendManyWritesEveryEvent) {
    auto log = makeLog("append_many");
    ASSERT_NE(log, nullptr);

    std::vector<BehaviorEventLog::PreparedEvent> events;
    events.emplace_back(makeEvent("PlayerChat", "Normal", 100, 1, 2, 3, 0));
    events.emplace_back(makeEvent("PlayerDie", "Normal", 200, 4, 5, 6, 1));
    events.emplace_back(makeEvent("PlayerPlaceBlock", "Operable", 300, 7, 8, 9, 2));

    auto ids = log->appendMany(events);
    ASSERT_TRUE(ids.has_value());
    EXPECT_EQ(ids->size(), 3);

    auto count = log->count();
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(*count, 3);
}

TEST_F(BehaviorEventLogTest, ReadRestoresEventFields) {
    auto log = makeLog("read_fields");
    ASSERT_NE(log, nullptr);

    std::vector<BehaviorEventLog::PreparedEvent> events;
    events.emplace_back(makeEvent("PlayerChat", "Normal", 100, -114514, 64, -32, 1));

    auto ids = log->appendMany(events);
    ASSERT_TRUE(ids.has_value());
    ASSERT_EQ(ids->size(), 1);

    auto row = log->read(ids->front());
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ(row->at("event_name"), "PlayerChat");
    EXPECT_EQ(row->at("event_type"), "Normal");
    EXPECT_EQ(row->at("event_time"), "2026-01-01 00:00:00");
    EXPECT_EQ(row->at("player_name"), "loitietu");
    EXPECT_EQ(row->at("position_x"), "-114514");
    EXPECT_EQ(row->at("position_y"), "64");
    EXPECT_EQ(row->at("position_z"), "-32");
    EXPECT_EQ(row->at("position_dimension"), "1");
}

TEST_F(BehaviorEventLogTest, ReadManySkipsUnknownIds) {
    auto log = makeLog("read_many");
    ASSERT_NE(log, nullptr);

    std::vector<BehaviorEventLog::PreparedEvent> events;
    events.emplace_back(makeEvent("PlayerChat", "Normal", 100, 0, 0, 0, 0));

    auto ids = log->appendMany(events);
    ASSERT_TRUE(ids.has_value());

    std::vector<BlockId> query{ ids->front(), ids->front() + 4096 };

    auto rows = log->read(query);
    ASSERT_TRUE(rows.has_value());
    EXPECT_EQ(rows->size(), 1);
    EXPECT_TRUE(rows->contains(std::to_string(ids->front())));
}

TEST_F(BehaviorEventLogTest, QueryByNameTypeDimensionAndPosition) {
    auto log = makeLog("query_props");
    ASSERT_NE(log, nullptr);

    std::vector<BehaviorEventLog::PreparedEvent> events;
    events.emplace_back(makeEvent("PlayerChat", "Normal", 100, 10, 20, 30, 0));
    events.emplace_back(makeEvent("PlayerDie", "Normal", 200, 10, 20, 30, 1));
    events.emplace_back(makeEvent("PlayerPlaceBlock", "Operable", 300, 40, 50, 60, 2));

    auto ids = log->appendMany(events);
    ASSERT_TRUE(ids.has_value());
    ASSERT_EQ(ids->size(), 3);

    auto byName = log->byName("PlayerDie");
    ASSERT_TRUE(byName.has_value());
    ASSERT_EQ(byName->size(), 1);
    EXPECT_EQ(byName->front(), (*ids)[1]);

    auto byType = log->byType("Operable");
    ASSERT_TRUE(byType.has_value());
    ASSERT_EQ(byType->size(), 1);
    EXPECT_EQ(byType->front(), (*ids)[2]);

    auto byDimension = log->byDimension(1);
    ASSERT_TRUE(byDimension.has_value());
    ASSERT_EQ(byDimension->size(), 1);
    EXPECT_EQ(byDimension->front(), (*ids)[1]);

    auto byPosition = log->byPosition(10, 20, 30);
    ASSERT_TRUE(byPosition.has_value());
    EXPECT_EQ(byPosition->size(), 2);
}

TEST_F(BehaviorEventLogTest, QueryByTimeRange) {
    auto log = makeLog("query_time");
    ASSERT_NE(log, nullptr);

    std::vector<BehaviorEventLog::PreparedEvent> events;
    events.emplace_back(makeEvent("PlayerChat", "Normal", 100, 0, 0, 0, 0));
    events.emplace_back(makeEvent("PlayerDie", "Normal", 500, 0, 0, 0, 0));

    auto ids = log->appendMany(events);
    ASSERT_TRUE(ids.has_value());
    ASSERT_EQ(ids->size(), 2);

    auto range = log->byTimeRange(200, 600);
    ASSERT_TRUE(range.has_value());
    ASSERT_EQ(range->size(), 1);
    EXPECT_EQ(range->front(), (*ids)[1]);
}

TEST_F(BehaviorEventLogTest, ArchiveBeforeHidesExpiredEvents) {
    auto log = makeLog("archive");
    ASSERT_NE(log, nullptr);

    std::vector<BehaviorEventLog::PreparedEvent> events;
    events.emplace_back(makeEvent("PlayerChat", "Normal", 100, 0, 0, 0, 0));
    events.emplace_back(makeEvent("PlayerDie", "Normal", 900, 0, 0, 0, 0));

    auto ids = log->appendMany(events);
    ASSERT_TRUE(ids.has_value());
    ASSERT_EQ(ids->size(), 2);

    auto archived = log->archiveBefore(500);
    ASSERT_TRUE(archived.has_value());
    EXPECT_EQ(*archived, 1);

    auto alive = log->all();
    ASSERT_TRUE(alive.has_value());
    ASSERT_EQ(alive->size(), 1);
    EXPECT_EQ(alive->front(), (*ids)[1]);

    auto row = log->read(ids->front());
    ASSERT_TRUE(row.has_value());
    EXPECT_TRUE(row->empty());
}

TEST_F(BehaviorEventLogTest, EraseRemovesEvents) {
    auto log = makeLog("erase");
    ASSERT_NE(log, nullptr);

    std::vector<BehaviorEventLog::PreparedEvent> events;
    events.emplace_back(makeEvent("PlayerChat", "Normal", 100, 0, 0, 0, 0));
    events.emplace_back(makeEvent("PlayerDie", "Normal", 200, 0, 0, 0, 0));

    auto ids = log->appendMany(events);
    ASSERT_TRUE(ids.has_value());
    ASSERT_EQ(ids->size(), 2);

    std::vector<BlockId> removed{ ids->front() };

    ASSERT_TRUE(log->erase(removed).has_value());

    auto alive = log->all();
    ASSERT_TRUE(alive.has_value());
    ASSERT_EQ(alive->size(), 1);
    EXPECT_EQ(alive->front(), (*ids)[1]);
}
