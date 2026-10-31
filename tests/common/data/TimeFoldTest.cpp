#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"
#include "LOICollectionA/data/sqlite/block/WriteBatch.h"
#include "LOICollectionA/data/sqlite/block/TimeFold.h"

#include "common/data/StorageTestSupport.h"

using namespace LOICollection::data;
using storage_test_support::dropDb;
using storage_test_support::tempDb;

namespace time_fold_test_support {
    constexpr std::int64_t Day = 86'400'000'000'000LL;

    std::int64_t aligned(std::int64_t at) {
        return (at / Day) * Day;
    }

    bool add(TimeFold& fold, BlockRepository& repo, std::string_view group, std::int64_t at, std::int64_t value) {
        auto batch = WriteBatch::begin(repo.store());
        if (!batch.has_value())
            return false;

        if (!fold.add(**batch, group, at, value).has_value())
            return false;

        auto committed = (*batch)->commit();
        return committed.has_value();
    }
}

TEST(TimeFoldTest, OpenCreatesRoot) {
    auto path = tempDb("timefold", "open");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto fold = TimeFold::open(**repo, "events", TimeBucket::Day);
        ASSERT_TRUE(fold.has_value()) << fold.error().message();
        EXPECT_EQ(fold->name(), "events");

        auto root = (*repo)->store().idOf(0, "events");
        ASSERT_TRUE(root.has_value());
        ASSERT_TRUE(root.value().has_value());
    }

    dropDb(path);
}

TEST(TimeFoldTest, AddAggregatesWithinBucket) {
    auto path = tempDb("timefold", "aggregate");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto fold = TimeFold::open(**repo, "events", TimeBucket::Day);
        ASSERT_TRUE(fold.has_value()) << fold.error().message();

        auto base = 1'700'000'000'000'000'000LL;
        auto day = time_fold_test_support::aligned(base);
        EXPECT_EQ(fold->bucketStart(base), day);

        ASSERT_TRUE(time_fold_test_support::add(*fold, **repo, "alice", day + 3'600'000'000'000LL, 10));
        ASSERT_TRUE(time_fold_test_support::add(*fold, **repo, "alice", day + 7'200'000'000'000LL, 25));

        auto value = fold->get("alice", day + 9'000'000'000'000LL);
        ASSERT_TRUE(value.has_value()) << value.error().message();
        EXPECT_EQ(value->count, 2);
        EXPECT_EQ(value->sum, 35);
        EXPECT_EQ(value->min, 10);
        EXPECT_EQ(value->max, 25);

        auto nextDay = fold->get("alice", day + time_fold_test_support::Day);
        ASSERT_TRUE(nextDay.has_value()) << nextDay.error().message();
        EXPECT_EQ(nextDay->count, 0);
        EXPECT_EQ(nextDay->sum, 0);
    }

    dropDb(path);
}

TEST(TimeFoldTest, GroupsStayIsolated) {
    auto path = tempDb("timefold", "groups");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto fold = TimeFold::open(**repo, "events", TimeBucket::Day);
        ASSERT_TRUE(fold.has_value()) << fold.error().message();

        auto base = 1'700'000'000'000'000'000LL;
        auto day = time_fold_test_support::aligned(base);

        ASSERT_TRUE(time_fold_test_support::add(*fold, **repo, "bob", day + 1, 7));
        ASSERT_TRUE(time_fold_test_support::add(*fold, **repo, "alice", day + 2, 10));
        ASSERT_TRUE(time_fold_test_support::add(*fold, **repo, "alice", day + 3, 25));

        auto groups = fold->groups(day);
        ASSERT_TRUE(groups.has_value()) << groups.error().message();
        ASSERT_EQ(groups->size(), 2u);

        EXPECT_EQ(groups->at(0).first, "alice");
        EXPECT_EQ(groups->at(0).second.count, 2);
        EXPECT_EQ(groups->at(0).second.sum, 35);
        EXPECT_EQ(groups->at(1).first, "bob");
        EXPECT_EQ(groups->at(1).second.count, 1);
        EXPECT_EQ(groups->at(1).second.sum, 7);
    }

    dropDb(path);
}

TEST(TimeFoldTest, GroupKeysMayContainSeparators) {
    auto path = tempDb("timefold", "separators");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto fold = TimeFold::open(**repo, "events", TimeBucket::Day);
        ASSERT_TRUE(fold.has_value()) << fold.error().message();

        auto base = 1'700'000'000'000'000'000LL;
        auto day = time_fold_test_support::aligned(base);
        std::string group = "a:b|c/12";

        ASSERT_TRUE(time_fold_test_support::add(*fold, **repo, group, day + 5, 9));

        auto value = fold->get(group, day);
        ASSERT_TRUE(value.has_value()) << value.error().message();
        EXPECT_EQ(value->count, 1);
        EXPECT_EQ(value->sum, 9);

        auto groups = fold->groups(day);
        ASSERT_TRUE(groups.has_value()) << groups.error().message();
        ASSERT_EQ(groups->size(), 1u);
        EXPECT_EQ(groups->at(0).first, group);
    }

    dropDb(path);
}

TEST(TimeFoldTest, PutReplacesValues) {
    auto path = tempDb("timefold", "put");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto fold = TimeFold::open(**repo, "events", TimeBucket::Day);
        ASSERT_TRUE(fold.has_value()) << fold.error().message();

        auto base = 1'700'000'000'000'000'000LL;
        auto day = time_fold_test_support::aligned(base);

        ASSERT_TRUE(time_fold_test_support::add(*fold, **repo, "alice", day + 1, 10));

        auto batch = WriteBatch::begin((*repo)->store());
        ASSERT_TRUE(batch.has_value()) << batch.error().message();
        ASSERT_TRUE(fold->put(**batch, "alice", day, TimeFoldValue{5, 100, 1, 9}).has_value());
        ASSERT_TRUE((*batch)->commit().has_value());

        auto value = fold->get("alice", day);
        ASSERT_TRUE(value.has_value()) << value.error().message();
        EXPECT_EQ(value->count, 5);
        EXPECT_EQ(value->sum, 100);
        EXPECT_EQ(value->min, 1);
        EXPECT_EQ(value->max, 9);
    }

    dropDb(path);
}

TEST(TimeFoldTest, ReplaceDropsStaleGroups) {
    auto path = tempDb("timefold", "replace");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto fold = TimeFold::open(**repo, "events", TimeBucket::Day);
        ASSERT_TRUE(fold.has_value()) << fold.error().message();

        auto base = 1'700'000'000'000'000'000LL;
        auto day = time_fold_test_support::aligned(base);

        ASSERT_TRUE(time_fold_test_support::add(*fold, **repo, "alice", day + 1, 10));
        ASSERT_TRUE(time_fold_test_support::add(*fold, **repo, "bob", day + 2, 20));

        std::vector<std::pair<std::string, TimeFoldValue>> entries = {
            { "carol", TimeFoldValue{1, 4, 4, 4} }
        };
        ASSERT_TRUE(fold->replace(day, entries).has_value());

        auto groups = fold->groups(day);
        ASSERT_TRUE(groups.has_value()) << groups.error().message();
        ASSERT_EQ(groups->size(), 1u);
        EXPECT_EQ(groups->at(0).first, "carol");
        EXPECT_EQ(groups->at(0).second.sum, 4);
    }

    dropDb(path);
}

TEST(TimeFoldTest, TrimDropsOldBuckets) {
    auto path = tempDb("timefold", "trim");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto fold = TimeFold::open(**repo, "events", TimeBucket::Day);
        ASSERT_TRUE(fold.has_value()) << fold.error().message();

        auto base = 1'700'000'000'000'000'000LL;
        auto day = time_fold_test_support::aligned(base);
        auto nextDay = day + time_fold_test_support::Day;

        ASSERT_TRUE(time_fold_test_support::add(*fold, **repo, "alice", day + 1, 10));
        ASSERT_TRUE(time_fold_test_support::add(*fold, **repo, "alice", nextDay + 1, 20));

        ASSERT_TRUE(fold->trim(nextDay).has_value());

        auto old = fold->get("alice", day);
        ASSERT_TRUE(old.has_value()) << old.error().message();
        EXPECT_EQ(old->count, 0);

        auto current = fold->get("alice", nextDay);
        ASSERT_TRUE(current.has_value()) << current.error().message();
        EXPECT_EQ(current->count, 1);
        EXPECT_EQ(current->sum, 20);
    }

    dropDb(path);
}

TEST(TimeFoldTest, ClearRemovesEveryRow) {
    auto path = tempDb("timefold", "clear");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto fold = TimeFold::open(**repo, "events", TimeBucket::Day);
        ASSERT_TRUE(fold.has_value()) << fold.error().message();

        auto base = 1'700'000'000'000'000'000LL;
        auto day = time_fold_test_support::aligned(base);

        ASSERT_TRUE(time_fold_test_support::add(*fold, **repo, "alice", day + 1, 10));
        ASSERT_TRUE(clearTimeFold(**repo, "events").has_value());

        auto reopened = TimeFold::open(**repo, "events", TimeBucket::Day);
        ASSERT_TRUE(reopened.has_value()) << reopened.error().message();

        auto empty = reopened->groups(day);
        ASSERT_TRUE(empty.has_value()) << empty.error().message();
        EXPECT_TRUE(empty->empty());

        ASSERT_TRUE(time_fold_test_support::add(*reopened, **repo, "alice", day + 2, 3));

        auto value = reopened->get("alice", day);
        ASSERT_TRUE(value.has_value()) << value.error().message();
        EXPECT_EQ(value->count, 1);
        EXPECT_EQ(value->sum, 3);
    }

    dropDb(path);
}

TEST(TimeFoldTest, ValuesPersistAcrossReopen) {
    auto path = tempDb("timefold", "persist");
    auto base = 1'700'000'000'000'000'000LL;
    auto day = time_fold_test_support::aligned(base);

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto fold = TimeFold::open(**repo, "events", TimeBucket::Day);
        ASSERT_TRUE(fold.has_value()) << fold.error().message();
        ASSERT_TRUE(time_fold_test_support::add(*fold, **repo, "alice", day + 1, 11));
    }

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();

        auto fold = TimeFold::open(**repo, "events", TimeBucket::Day);
        ASSERT_TRUE(fold.has_value()) << fold.error().message();

        auto value = fold->get("alice", day);
        ASSERT_TRUE(value.has_value()) << value.error().message();
        EXPECT_EQ(value->count, 1);
        EXPECT_EQ(value->sum, 11);
        EXPECT_EQ(value->min, 11);
        EXPECT_EQ(value->max, 11);
    }

    dropDb(path);
}
