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
#include "LOICollectionA/data/sqlite/connection/ConnectionPool.h"

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

TEST(BlockStoreConcurrencyTest, PoolAcquireReleaseChurnAcrossThreads) {
    auto path = tempDb("concurrent", "pool-churn");

    {
        auto pool = ConnectionPool::create(path.string(), 4);
        ASSERT_TRUE(pool.has_value()) << pool.error().message();

        std::vector<std::shared_ptr<SQLiteConnection>> warmed;
        for (int i = 0; i < 4; ++i) {
            auto conn = (*pool)->acquire(2000);
            ASSERT_TRUE(conn.has_value()) << conn.error().message();
            warmed.push_back(std::move(*conn));
        }
        for (auto& conn : warmed)
            (*pool)->release(std::move(conn));

        std::atomic<int> failures{0};

        auto worker = [&pool, &failures]() {
            for (int i = 0; i < 200; ++i) {
                auto conn = (*pool)->acquire(2000);
                if (!conn.has_value()) {
                    ++failures;
                    return;
                }

                if (conn->get() == nullptr) {
                    ++failures;
                    return;
                }

                (*pool)->release(std::move(*conn));
            }
        };

        std::vector<std::thread> threads;
        for (int i = 0; i < 8; ++i)
            threads.emplace_back(worker);
        for (auto& thread : threads)
            thread.join();

        EXPECT_EQ(failures.load(), 0);
    }
    dropDb(path);
}

TEST(BlockStoreConcurrencyTest, PoolAcquireOnColdFileAcrossThreadsSucceeds) {
    auto path = tempDb("concurrent", "pool-cold");

    {
        auto pool = ConnectionPool::create(path.string(), 4);
        ASSERT_TRUE(pool.has_value()) << pool.error().message();

        std::atomic<int> failures{0};

        auto opener = [&pool, &failures]() {
            auto conn = (*pool)->acquire(2000);
            if (!conn.has_value() || conn->get() == nullptr) {
                ++failures;
                return;
            }

            (*pool)->release(std::move(*conn));
        };

        std::vector<std::thread> threads;
        for (int i = 0; i < 8; ++i)
            threads.emplace_back(opener);
        for (auto& thread : threads)
            thread.join();

        EXPECT_EQ(failures.load(), 0);
    }
    dropDb(path);
}

TEST(BlockStoreConcurrencyTest, ConcurrentLoadByNameAgreesWithCreatedRecords) {
    auto path = tempDb("concurrent", "load-by-name");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        std::vector<std::string> names;
        names.reserve(240);
        for (int i = 0; i < 240; ++i) {
            auto name = "named" + std::to_string(i);
            ASSERT_TRUE(store.createBlock(0, 6, name).has_value());
            names.push_back(std::move(name));
        }

        std::atomic<int> failures{0};

        auto reader = [&store, &names, &failures]() {
            for (auto const& name : names) {
                auto record = store.load(0, name);
                if (!record.has_value() || record.value().name != name) {
                    ++failures;
                    continue;
                }

                auto again = store.load(record.value().id);
                if (!again.has_value() || again.value().name != name)
                    ++failures;
            }
        };

        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i)
            threads.emplace_back(reader);
        for (auto& thread : threads)
            thread.join();

        EXPECT_EQ(failures.load(), 0);
    }
    dropDb(path);
}

TEST(BlockStoreConcurrencyTest, ConcurrentSoftDeletesKeepReadersOnKnownStates) {
    auto path = tempDb("concurrent", "soft-deletes");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        constexpr int kBlocks = 200;
        std::vector<BlockId> ids;
        ids.reserve(kBlocks);
        for (int i = 0; i < kBlocks; ++i) {
            auto id = store.createBlock(0, 9, "del" + std::to_string(i));
            ASSERT_TRUE(id.has_value()) << id.error().message();
            ids.push_back(*id);
        }

        std::atomic<bool> stop{false};
        std::atomic<int> failures{0};

        auto deleter = [&store, &ids, &failures](int offset, int stride) {
            for (std::size_t i = offset; i < ids.size(); i += stride) {
                auto removed = store.control(ids[i], BlockLifecycle::Deleted);
                if (!removed.has_value())
                    ++failures;
            }
        };

        auto reader = [&store, &ids, &stop, &failures]() {
            while (!stop.load(std::memory_order_relaxed)) {
                for (auto id : ids) {
                    auto record = store.load(id);
                    if (!record.has_value()) {
                        if (record.error().isA<ll::ErrorCodeError>())
                            continue;

                        ++failures;
                        continue;
                    }

                    if (record.value().state != BlockLifecycle::Active
                        && record.value().state != BlockLifecycle::Deleted)
                        ++failures;
                }
            }
        };

        std::vector<std::thread> deleters;
        deleters.emplace_back(deleter, 0, 2);
        deleters.emplace_back(deleter, 1, 2);

        std::vector<std::thread> readers;
        readers.emplace_back(reader);
        readers.emplace_back(reader);

        for (auto& thread : deleters)
            thread.join();

        stop.store(true, std::memory_order_relaxed);
        for (auto& thread : readers)
            thread.join();

        EXPECT_EQ(failures.load(), 0);

        auto children = store.children(0, 9);
        ASSERT_TRUE(children.has_value()) << children.error().message();
        EXPECT_TRUE(children.value().empty());

        auto counted = store.countChildren(0, 9);
        ASSERT_TRUE(counted.has_value()) << counted.error().message();
        EXPECT_EQ(counted.value(), 0u);
    }
    dropDb(path);
}

TEST(BlockStoreConcurrencyTest, ConcurrentSetPayloadEndsOnAWrittenValue) {
    auto path = tempDb("concurrent", "set-payload");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto id = store.createBlock(0, 2, "hot");
        ASSERT_TRUE(id.has_value()) << id.error().message();
        ASSERT_TRUE(store.setPayload(*id, "seed").has_value());

        std::atomic<int> failures{0};

        auto writer = [&store, &id, &failures](int thread) {
            for (int i = 0; i < 60; ++i) {
                auto written = store.setPayload(*id, "w" + std::to_string(thread));
                if (!written.has_value())
                    ++failures;
            }
        };

        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i)
            threads.emplace_back(writer, i);
        for (auto& thread : threads)
            thread.join();

        EXPECT_EQ(failures.load(), 0);

        auto record = store.load(*id);
        ASSERT_TRUE(record.has_value()) << record.error().message();
        auto payload = payloadText(record.value().payload);
        EXPECT_TRUE(payload == "seed" || payload == "w0"
            || payload == "w1" || payload == "w2" || payload == "w3")
            << "torn payload " << payload;
        EXPECT_GE(record.value().updated, record.value().created);
    }
    dropDb(path);
}

TEST(BlockStoreConcurrencyTest, ConcurrentLinksKeepEveryEdgeVisible) {
    auto path = tempDb("concurrent", "links");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        auto src = store.createBlock(0, 1, "src");
        ASSERT_TRUE(src.has_value()) << src.error().message();

        std::vector<BlockId> nearTargets;
        std::vector<BlockId> farTargets;
        for (int i = 0; i < 100; ++i) {
            auto near = store.createBlock(0, 1, "near" + std::to_string(i));
            auto far = store.createBlock(0, 1, "far" + std::to_string(i));
            ASSERT_TRUE(near.has_value()) << near.error().message();
            ASSERT_TRUE(far.has_value()) << far.error().message();
            nearTargets.push_back(*near);
            farTargets.push_back(*far);
        }

        std::atomic<int> failures{0};

        auto linker = [&store, &src, &failures](std::vector<BlockId> targets, int kind) {
            for (auto target : targets) {
                auto linked = store.link(*src, target, kind);
                if (!linked.has_value())
                    ++failures;
            }
        };

        std::vector<std::thread> threads;
        threads.emplace_back(linker, nearTargets, 3);
        threads.emplace_back(linker, farTargets, 4);
        for (auto& thread : threads)
            thread.join();

        EXPECT_EQ(failures.load(), 0);

        auto nearLinks = store.links(*src, 3);
        ASSERT_TRUE(nearLinks.has_value()) << nearLinks.error().message();
        EXPECT_EQ(nearLinks.value().size(), 100u);

        auto farLinks = store.links(*src, 4);
        ASSERT_TRUE(farLinks.has_value()) << farLinks.error().message();
        EXPECT_EQ(farLinks.value().size(), 100u);

        auto allLinks = store.links(*src);
        ASSERT_TRUE(allLinks.has_value()) << allLinks.error().message();
        EXPECT_EQ(allLinks.value().size(), 200u);

        for (auto target : nearTargets) {
            auto back = store.backlinks(target, 3);
            ASSERT_TRUE(back.has_value()) << back.error().message();
            EXPECT_EQ(back.value(), std::vector<BlockId>{*src});
        }
    }
    dropDb(path);
}

TEST(BlockStoreConcurrencyTest, ConcurrentInternDistinctNamesYieldDistinctIds) {
    auto path = tempDb("concurrent", "intern");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        constexpr int kThreads = 4;
        constexpr int kNamesPerThread = 40;
        std::atomic<int> failures{0};

        auto interner = [&store, &failures](int thread) {
            for (int i = 0; i < kNamesPerThread; ++i) {
                auto name = "key" + std::to_string(thread) + "-" + std::to_string(i);
                auto id = store.intern(name);
                if (!id.has_value() || *id < 1) {
                    ++failures;
                    continue;
                }

                auto resolved = store.unintern(*id);
                if (!resolved.has_value() || *resolved != name)
                    ++failures;
            }
        };

        std::vector<std::thread> threads;
        for (int i = 0; i < kThreads; ++i)
            threads.emplace_back(interner, i);
        for (auto& thread : threads)
            thread.join();

        EXPECT_EQ(failures.load(), 0);

        for (int thread = 0; thread < kThreads; ++thread) {
            for (int i = 0; i < kNamesPerThread; ++i) {
                auto name = "key" + std::to_string(thread) + "-" + std::to_string(i);
                auto first = store.intern(name);
                auto second = store.intern(name);
                ASSERT_TRUE(first.has_value()) << first.error().message();
                ASSERT_TRUE(second.has_value()) << second.error().message();
                EXPECT_EQ(*first, *second);
            }
        }
    }
    dropDb(path);
}

TEST(BlockStoreConcurrencyTest, ConcurrentMetaWritesKeepEveryKeyReadable) {
    auto path = tempDb("concurrent", "meta");

    {
        auto repo = BlockRepository::open(path.string(), 2);
        ASSERT_TRUE(repo.has_value()) << repo.error().message();
        BlockStore& store = (*repo)->store();

        std::atomic<int> failures{0};

        auto writer = [&store, &failures](int thread) {
            for (int i = 0; i < 50; ++i) {
                auto written = store.metaSet("mk" + std::to_string(thread),
                                             "v" + std::to_string(thread) + "-" + std::to_string(i));
                if (!written.has_value())
                    ++failures;
            }
        };

        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i)
            threads.emplace_back(writer, i);
        for (auto& thread : threads)
            thread.join();

        EXPECT_EQ(failures.load(), 0);

        for (int thread = 0; thread < 4; ++thread) {
            auto value = store.metaGet("mk" + std::to_string(thread));
            ASSERT_TRUE(value.has_value()) << value.error().message();
            ASSERT_TRUE(value.value().has_value());
            EXPECT_EQ(*value.value(), "v" + std::to_string(thread) + "-49");
        }
    }
    dropDb(path);
}
