#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"
#include "LOICollectionA/data/sqlite/block/BlockStore.h"

#include "common/data/StorageTestSupport.h"

using storage_test_support::dropDb;
using storage_test_support::payloadText;
using storage_test_support::tempDb;

namespace block_store_concurrency_test_support {
    constexpr int kThreads = 4;
    constexpr int kBlocksPerThread = 120;

    std::string payloadFor(int thread, int index) {
        return "w" + std::to_string(thread) + "-" + std::to_string(index);
    }
}

TEST(BlockStoreConcurrencyTest, ConcurrentCreatesStayDistinctAndCounted) {
    auto path = tempDb("concurrent", "creates");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        std::atomic<int> failures{0};

        auto worker = [&store, &failures](int thread) {
            for (int i = 0; i < block_store_concurrency_test_support::kBlocksPerThread; ++i) {
                auto id = store.createBlock(
                    0, 3,
                    "t" + std::to_string(thread) + "-" + std::to_string(i),
                    block_store_concurrency_test_support::payloadFor(thread, i));
                if (!id.has_value())
                    ++failures;
            }
        };

        std::vector<std::thread> threads;
        for (int thread = 0; thread < block_store_concurrency_test_support::kThreads; ++thread)
            threads.emplace_back(worker, thread);
        for (auto& thread : threads)
            thread.join();

        EXPECT_EQ(failures.load(), 0);

        auto counted = store.countChildren(0, 3);
        ASSERT_TRUE(counted.has_value()) << counted.error().message();
        EXPECT_EQ(counted.value(),
                  static_cast<size_t>(block_store_concurrency_test_support::kThreads)
                  * block_store_concurrency_test_support::kBlocksPerThread);
    }
    dropDb(path);
}

TEST(BlockStoreConcurrencyTest, ConcurrentSetPropOnDistinctKeysKeepsEveryColumn) {
    auto path = tempDb("concurrent", "props");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto id = store.createBlock(0, 1, "shared");
        ASSERT_TRUE(id.has_value()) << id.error().message();

        std::atomic<int> failures{0};

        auto worker = [&store, &id, &failures](int thread) {
            for (int i = 0; i < 40; ++i) {
                auto res = store.setProp(*id, 100 + thread, std::int64_t(thread * 1000 + i));
                if (!res.has_value())
                    ++failures;
            }
        };

        std::vector<std::thread> threads;
        for (int thread = 0; thread < block_store_concurrency_test_support::kThreads; ++thread)
            threads.emplace_back(worker, thread);
        for (auto& thread : threads)
            thread.join();

        EXPECT_EQ(failures.load(), 0);

        auto record = store.load(*id);
        ASSERT_TRUE(record.has_value()) << record.error().message();
        EXPECT_EQ(record.value().props.size(),
                  static_cast<size_t>(block_store_concurrency_test_support::kThreads));

        for (auto const& prop : record.value().props)
            EXPECT_GE(prop.intValue, 0);
    }
    dropDb(path);
}

TEST(BlockStoreConcurrencyTest, ReadersNeverObserveForeignPayloadsWhileWritersUpdate) {
    auto path = tempDb("concurrent", "mixed");

    {
        auto repo = BlockRepository::open(path.string(), 3);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        constexpr int kBlocks = 60;

        std::vector<BlockId> ids;
        ids.reserve(kBlocks);
        for (int i = 0; i < kBlocks; ++i) {
            auto id = store.createBlock(0, 2, "mix" + std::to_string(i), "seed" + std::to_string(i));
            ASSERT_TRUE(id.has_value()) << id.error().message();
            ids.push_back(*id);
        }

        std::atomic<int> failures{0};
        std::atomic<bool> stop{false};

        auto writer = [&store, &ids, &failures, &stop](int thread) {
            std::string payload = block_store_concurrency_test_support::payloadFor(thread, 0);
            while (!stop.load(std::memory_order_relaxed)) {
                for (auto id : ids) {
                    auto res = store.setPayload(id, payload);
                    if (!res.has_value())
                        ++failures;
                }
            }
        };

        auto reader = [&store, &ids, &failures, &stop]() {
            while (!stop.load(std::memory_order_relaxed)) {
                for (auto id : ids) {
                    auto record = store.load(id);
                    if (!record.has_value()) {
                        ++failures;
                        continue;
                    }

                    auto payload = payloadText(record.value().payload);
                    if (payload.rfind("w", 0) != 0 && payload.rfind("seed", 0) != 0)
                        ++failures;
                }
            }
        };

        std::vector<std::thread> threads;
        threads.emplace_back(writer, 0);
        threads.emplace_back(writer, 1);
        threads.emplace_back(reader);
        threads.emplace_back(reader);

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        stop.store(true, std::memory_order_relaxed);
        for (auto& thread : threads)
            thread.join();

        EXPECT_EQ(failures.load(), 0);

        for (auto id : ids) {
            auto record = store.load(id);
            ASSERT_TRUE(record.has_value()) << record.error().message();
            auto payload = payloadText(record.value().payload);
            bool zero = payload == block_store_concurrency_test_support::payloadFor(0, 0);
            bool one = payload == block_store_concurrency_test_support::payloadFor(1, 0);
            EXPECT_TRUE(zero || one) << "unexpected final payload " << payload;
        }
    }
    dropDb(path);
}

TEST(BlockStoreConcurrencyTest, ConcurrentIdOfResolvesEveryCreatedName) {
    auto path = tempDb("concurrent", "idof");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        std::vector<std::string> names;
        names.reserve(block_store_concurrency_test_support::kThreads
                      * block_store_concurrency_test_support::kBlocksPerThread);
        for (int thread = 0; thread < block_store_concurrency_test_support::kThreads; ++thread) {
            for (int i = 0; i < block_store_concurrency_test_support::kBlocksPerThread; ++i) {
                auto name = "idof" + std::to_string(thread) + "-" + std::to_string(i);
                ASSERT_TRUE(store.createBlock(0, 4, name).has_value());
                names.push_back(std::move(name));
            }
        }

        std::atomic<int> failures{0};

        auto resolver = [&store, &names, &failures]() {
            for (auto const& name : names) {
                auto id = store.idOf(0, name);
                if (!id.has_value() || !id.value().has_value()) {
                    ++failures;
                    continue;
                }

                auto record = store.load(*id.value());
                if (!record.has_value() || record.value().name != name)
                    ++failures;
            }
        };

        std::vector<std::thread> threads;
        for (int thread = 0; thread < block_store_concurrency_test_support::kThreads; ++thread)
            threads.emplace_back(resolver);
        for (auto& thread : threads)
            thread.join();

        EXPECT_EQ(failures.load(), 0);
    }
    dropDb(path);
}
