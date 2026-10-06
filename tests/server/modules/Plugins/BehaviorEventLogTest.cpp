#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "LOICollectionA/data/sqlite/block/BlockStore.h"
#include "LOICollectionA/data/sqlite/connection/ConnectionPool.h"

#include "LOICollectionA/include/server/Plugins/behaviorevent/BehaviorEventLog.h"

class BehaviorEventLogTest : public testing::Test {
protected:
    static void SetUpTestSuite() {
        tempDir = std::filesystem::temp_directory_path() / "event_log_test";

        std::filesystem::create_directories(tempDir);

        auto created = ConnectionPool::create((tempDir / "events.db").string(), 2);
        ASSERT_TRUE(created.has_value());

        pool = std::move(created.value());

        auto store = BlockStore::create(pool);
        ASSERT_TRUE(store.has_value());

        storage = std::move(store.value());
    }

    static void TearDownTestSuite() {
        storage.reset();
        pool.reset();

        std::filesystem::remove_all(tempDir);
    }

    static std::unique_ptr<LOICollection::server::Plugins::BehaviorEventLog> makeLog(std::string_view root) {
        auto log = LOICollection::server::Plugins::BehaviorEventLog::create(*storage, root);
        if (!log.has_value())
            return nullptr;

        return std::move(log.value());
    }

    static LOICollection::server::Plugins::BehaviorEventLog::PreparedEvent makeEvent(
        std::string_view name,
        std::string_view type,
        std::int64_t timestamp,
        std::int64_t x,
        std::int64_t y,
        std::int64_t z,
        std::int64_t dimension) {
        LOICollection::server::Plugins::BehaviorEventLog::PreparedEvent event;
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

    std::vector<LOICollection::server::Plugins::BehaviorEventLog::PreparedEvent> events;
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

    std::vector<LOICollection::server::Plugins::BehaviorEventLog::PreparedEvent> events;
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

    std::vector<LOICollection::server::Plugins::BehaviorEventLog::PreparedEvent> events;
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

    std::vector<LOICollection::server::Plugins::BehaviorEventLog::PreparedEvent> events;
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

    std::vector<LOICollection::server::Plugins::BehaviorEventLog::PreparedEvent> events;
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

    std::vector<LOICollection::server::Plugins::BehaviorEventLog::PreparedEvent> events;
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

    std::vector<LOICollection::server::Plugins::BehaviorEventLog::PreparedEvent> events;
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

TEST_F(BehaviorEventLogTest, ByPositionKeepsHitsBeyondLimit) {
    auto log = makeLog("position_limit");
    ASSERT_NE(log, nullptr);

    std::vector<LOICollection::server::Plugins::BehaviorEventLog::PreparedEvent> events;
    events.emplace_back(makeEvent("Noise", "Normal", 100, 10, 0, 0, 0));
    events.emplace_back(makeEvent("Noise", "Normal", 100, 10, 0, 0, 0));
    events.emplace_back(makeEvent("Noise", "Normal", 100, 0, 20, 0, 0));
    events.emplace_back(makeEvent("Noise", "Normal", 100, 0, 0, 30, 0));
    events.emplace_back(makeEvent("Hit", "Normal", 100, 10, 20, 30, 0));
    events.emplace_back(makeEvent("Hit", "Normal", 100, 10, 20, 30, 0));

    auto ids = log->appendMany(events);
    ASSERT_TRUE(ids.has_value());
    ASSERT_EQ(ids->size(), 6);

    auto hits = log->byPosition(10, 20, 30, 2);
    ASSERT_TRUE(hits.has_value());
    ASSERT_EQ(hits->size(), 2);
    EXPECT_EQ((*hits)[0], (*ids)[4]);
    EXPECT_EQ((*hits)[1], (*ids)[5]);
}

TEST_F(BehaviorEventLogTest, ByPositionHonoursLimitOnFinalResult) {
    auto log = makeLog("position_final_limit");
    ASSERT_NE(log, nullptr);

    std::vector<LOICollection::server::Plugins::BehaviorEventLog::PreparedEvent> events;
    events.emplace_back(makeEvent("Hit", "Normal", 100, 7, 8, 9, 0));
    events.emplace_back(makeEvent("Hit", "Normal", 100, 7, 8, 9, 0));
    events.emplace_back(makeEvent("Hit", "Normal", 100, 7, 8, 9, 0));

    auto ids = log->appendMany(events);
    ASSERT_TRUE(ids.has_value());
    ASSERT_EQ(ids->size(), 3);

    auto hits = log->byPosition(7, 8, 9, 2);
    ASSERT_TRUE(hits.has_value());
    EXPECT_EQ(hits->size(), 2);
}

TEST_F(BehaviorEventLogTest, CountIgnoresArchivedAndDeleted) {
    auto log = makeLog("count_live");
    ASSERT_NE(log, nullptr);

    std::vector<LOICollection::server::Plugins::BehaviorEventLog::PreparedEvent> events;
    events.emplace_back(makeEvent("A", "Normal", 100, 0, 0, 0, 0));
    events.emplace_back(makeEvent("B", "Normal", 200, 0, 0, 0, 0));
    events.emplace_back(makeEvent("C", "Normal", 300, 0, 0, 0, 0));

    auto ids = log->appendMany(events);
    ASSERT_TRUE(ids.has_value());
    ASSERT_EQ(ids->size(), 3);

    auto total = log->count();
    ASSERT_TRUE(total.has_value());
    EXPECT_EQ(*total, 3);

    ASSERT_TRUE(log->archiveBefore(150).has_value());

    auto afterArchive = log->count();
    ASSERT_TRUE(afterArchive.has_value());
    EXPECT_EQ(*afterArchive, 2);

    std::vector<BlockId> removed{ (*ids)[1] };
    ASSERT_TRUE(log->erase(removed).has_value());

    auto afterErase = log->count();
    ASSERT_TRUE(afterErase.has_value());
    EXPECT_EQ(*afterErase, 1);
}

TEST_F(BehaviorEventLogTest, AppendManyWithNoEventsWritesNothing) {
    auto log = makeLog("append_empty");
    ASSERT_NE(log, nullptr);

    std::vector<LOICollection::server::Plugins::BehaviorEventLog::PreparedEvent> events;

    auto ids = log->appendMany(events);
    ASSERT_TRUE(ids.has_value());
    EXPECT_TRUE(ids->empty());

    auto total = log->count();
    ASSERT_TRUE(total.has_value());
    EXPECT_EQ(*total, 0);
}

TEST_F(BehaviorEventLogTest, TimestampZeroIsPersisted) {
    auto log = makeLog("timestamp_zero");
    ASSERT_NE(log, nullptr);

    std::vector<LOICollection::server::Plugins::BehaviorEventLog::PreparedEvent> events;
    events.emplace_back(makeEvent("Epoch", "Normal", 0, 0, 0, 0, 0));
    events.emplace_back(makeEvent("Later", "Normal", 500, 0, 0, 0, 0));

    auto ids = log->appendMany(events);
    ASSERT_TRUE(ids.has_value());
    ASSERT_EQ(ids->size(), 2);

    auto archived = log->archiveBefore(100);
    ASSERT_TRUE(archived.has_value());
    EXPECT_EQ(*archived, 1);

    auto alive = log->all();
    ASSERT_TRUE(alive.has_value());
    ASSERT_EQ(alive->size(), 1);
    EXPECT_EQ(alive->front(), (*ids)[1]);
}

TEST_F(BehaviorEventLogTest, MissingTimestampIsNotIndexed) {
    auto log = makeLog("timestamp_missing");
    ASSERT_NE(log, nullptr);

    LOICollection::server::Plugins::BehaviorEventLog::PreparedEvent event = makeEvent("NoTime", "Normal", 100, 0, 0, 0, 0);
    event.timestamp.reset();

    std::vector<LOICollection::server::Plugins::BehaviorEventLog::PreparedEvent> events{ event };

    auto ids = log->appendMany(events);
    ASSERT_TRUE(ids.has_value());
    ASSERT_EQ(ids->size(), 1);

    auto range = log->byTimeRange(0, 1000);
    ASSERT_TRUE(range.has_value());
    EXPECT_TRUE(range->empty());

    auto row = log->read(ids->front());
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ(row->at("event_name"), "NoTime");
}

TEST_F(BehaviorEventLogTest, LongFieldValuesRoundTrip) {
    auto log = makeLog("long_fields");
    ASSERT_NE(log, nullptr);

    std::string longValue(70000, 'x');

    LOICollection::server::Plugins::BehaviorEventLog::PreparedEvent event = makeEvent("Long", "Normal", 100, 0, 0, 0, 0);
    event.fields.emplace_back("event_blob", longValue);

    std::vector<LOICollection::server::Plugins::BehaviorEventLog::PreparedEvent> events{ event };

    auto ids = log->appendMany(events);
    ASSERT_TRUE(ids.has_value());
    ASSERT_EQ(ids->size(), 1);

    auto row = log->read(ids->front());
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ(row->at("event_blob"), longValue);
}

TEST_F(BehaviorEventLogTest, ReadUnknownIdReturnsEmptyRow) {
    auto log = makeLog("read_unknown");
    ASSERT_NE(log, nullptr);

    auto row = log->read(static_cast<BlockId>(999999));
    ASSERT_TRUE(row.has_value());
    EXPECT_TRUE(row->empty());
}
