#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "LOICollectionA/base/Cache.h"

TEST(LRUCacheTest, PutAndGet) {
    LRUCache<int, std::string> cache(3);

    cache.put(1, std::make_shared<std::string>("one"));

    auto val = cache.get(1);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*(val.value()), "one");
}

TEST(LRUCacheTest, GetMissingKeyReturnsNullopt) {
    LRUCache<int, std::string> cache(2);

    EXPECT_FALSE(cache.get(42).has_value());
}

TEST(LRUCacheTest, PutUpdatesExistingKey) {
    LRUCache<int, std::string> cache(2);

    cache.put(1, std::make_shared<std::string>("first"));
    cache.put(1, std::make_shared<std::string>("second"));

    auto val = cache.get(1);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*(val.value()), "second");
}

TEST(LRUCacheTest, PutValueOverload) {
    LRUCache<int, std::string> cache(2);

    cache.put(10, std::string("hello"));

    auto val = cache.get(10);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*(val.value()), "hello");
}

TEST(LRUCacheTest, EvictsLeastRecentlyUsed) {
    LRUCache<int, std::string> cache(2);

    cache.put(1, std::make_shared<std::string>("a"));
    cache.put(2, std::make_shared<std::string>("b"));
    cache.get(1);
    cache.put(3, std::make_shared<std::string>("c"));

    EXPECT_FALSE(cache.get(2).has_value());
    EXPECT_TRUE(cache.get(1).has_value());
    EXPECT_TRUE(cache.get(3).has_value());
}

TEST(LRUCacheTest, Contains) {
    LRUCache<int, std::string> cache(2);

    cache.put(5, std::make_shared<std::string>("data"));

    EXPECT_TRUE(cache.contains(5));
    EXPECT_FALSE(cache.contains(999));
}

TEST(LRUCacheTest, Erase) {
    LRUCache<int, std::string> cache(3);

    cache.put(1, std::make_shared<std::string>("x"));

    EXPECT_TRUE(cache.erase(1));
    EXPECT_FALSE(cache.contains(1));
    EXPECT_FALSE(cache.erase(1));
}

TEST(LRUCacheTest, ClearAndSize) {
    LRUCache<int, std::string> cache(3);

    cache.put(1, std::make_shared<std::string>("a"));
    cache.put(2, std::make_shared<std::string>("b"));

    EXPECT_EQ(cache.size(), 2);

    cache.clear();

    EXPECT_EQ(cache.size(), 0);
    EXPECT_TRUE(cache.empty());
}

TEST(LRUCacheTest, UpdateModifiesValue) {
    LRUCache<int, std::string> cache(2);

    cache.put(1, std::make_shared<std::string>("old"));
    bool updated = cache.update(1, [](std::shared_ptr<std::string> val) -> void {
        *val = "new";
    });

    EXPECT_TRUE(updated);

    auto val = cache.get(1);

    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*(val.value()), "new");
}

TEST(LRUCacheTest, CachesOrder) {
    LRUCache<int, std::string> cache(3);

    cache.put(1, std::make_shared<std::string>("1"));
    cache.put(2, std::make_shared<std::string>("2"));
    cache.put(3, std::make_shared<std::string>("3"));

    auto keys = cache.caches();

    ASSERT_EQ(keys.size(), 3);
    EXPECT_EQ(keys[0], 3);
    EXPECT_EQ(keys[1], 2);
    EXPECT_EQ(keys[2], 1);
}

TEST(LRUKCacheInitialAccessTest, InitialAccessGoesToHistory) {
    LRUKCache<int, std::string> cache(2, 2, 3);

    cache.put(1, std::string("first"));

    auto val = cache.get(1);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val.value(), "first");

    EXPECT_FALSE(cache.contains(1));
    EXPECT_EQ(cache.historys().size(), 1);
}

class LRUKCacheTest : public testing::Test {
protected:
    void SetUp() override {
        cache = std::make_unique<LRUKCache<int, std::string>>(2, 2, 2);
    }

    std::unique_ptr<LRUKCache<int, std::string>> cache;
};

TEST_F(LRUKCacheTest, PromotionAfterKAcesses) {
    cache->put(1, std::string("data"));
    cache->get(1);

    EXPECT_TRUE(cache->contains(1));
    EXPECT_TRUE(cache->historys().empty());
    EXPECT_EQ(cache->caches().size(), 1);
}

TEST_F(LRUKCacheTest, PutUpdatesValueAndIncrementsCount) {
    cache->put(1, std::string("v1"));
    cache->put(1, std::string("v2"));

    EXPECT_TRUE(cache->contains(1));

    auto val = cache->get(1);

    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val.value(), "v2");
}

TEST_F(LRUKCacheTest, HistoryEviction) {
    cache->put(1, std::string("a"));
    cache->put(2, std::string("b"));
    cache->put(3, std::string("c"));

    EXPECT_FALSE(cache->get(1).has_value());
    EXPECT_TRUE(cache->get(2).has_value());
    EXPECT_TRUE(cache->get(3).has_value());
}

TEST_F(LRUKCacheTest, UpdateOnHistoryAndCache) {
    cache->put(1, std::string("old"));

    bool updated = cache->update(1, [](std::shared_ptr<std::string> val) -> void {
        *val = "new";
    });

    EXPECT_TRUE(updated);
    EXPECT_TRUE(cache->contains(1));

    auto val = cache->get(1);

    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val.value(), "new");
}

TEST_F(LRUKCacheTest, EraseFromBoth) {
    cache->put(1, std::string("x"));
    cache->get(1);
    cache->put(2, std::string("y"));
    
    EXPECT_TRUE(cache->erase(1));
    EXPECT_TRUE(cache->erase(2));
    EXPECT_TRUE(cache->empty());
}

TEST_F(LRUKCacheTest, ClearAll) {
    cache->put(1, std::string("x"));
    cache->put(2, std::string("y"));
    cache->get(1);
    cache->clear();
    
    EXPECT_TRUE(cache->empty());
    EXPECT_TRUE(cache->caches().empty());
    EXPECT_TRUE(cache->historys().empty());
}

TEST_F(LRUKCacheTest, InvalidKThrows) {
    EXPECT_THROW((LRUKCache<int, std::string>(1, 1, 1)), std::invalid_argument);
}

TEST(LRUCacheTest, ZeroCapacityStoresNothing) {
    LRUCache<int, std::string> cache(0);

    cache.put(1, std::string("x"));

    EXPECT_TRUE(cache.empty());
    EXPECT_FALSE(cache.contains(1));
    EXPECT_FALSE(cache.get(1).has_value());
}

TEST(LRUCacheTest, UpdateAcceptsConstReferenceModifier) {
    LRUCache<int, std::string> cache(1);

    cache.put(1, std::string("old"));

    const bool updated = cache.update(1, [](const std::shared_ptr<std::string>& value) {
        *value = "new";
    });

    EXPECT_TRUE(updated);
    EXPECT_EQ(*cache.get(1).value(), "new");
}

TEST_F(LRUKCacheTest, ZeroCapacityNeverPromotes) {
    LRUKCache<int, std::string> cache(0, 2, 2);

    cache.put(1, std::string("x"));
    EXPECT_TRUE(cache.caches().empty());

    const auto value = cache.get(1);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(**value, "x");

    EXPECT_TRUE(cache.caches().empty());
    EXPECT_TRUE(cache.historys().empty());
}
