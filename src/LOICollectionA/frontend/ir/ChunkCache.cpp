#include <utility>

#include "LOICollectionA/frontend/ir/ChunkCache.h"

namespace LOICollection::frontend::ir {
    namespace {
        std::atomic<std::uint64_t> generation{ 1 };
    }

    std::uint64_t chunkGeneration() {
        return generation.load(std::memory_order_acquire);
    }

    MirChunk::~MirChunk() {
        std::vector<std::unique_ptr<MirChunk>> pending;

        auto drain = [&pending](MirChunk* node) -> void {
            while (!node->methodBodies.empty()) {
                pending.push_back(std::move(node->methodBodies.back()));
                node->methodBodies.pop_back();
            }
        };

        drain(this);
        while (!pending.empty()) {
            auto owned = std::move(pending.back());
            pending.pop_back();

            drain(owned.get());
        }

        
        
        ChunkCacheRegistry::getInstance().release(this);
        generation.fetch_add(1, std::memory_order_release);
    }

    ChunkCacheRegistry& ChunkCacheRegistry::getInstance() {
        static ChunkCacheRegistry instance;
        return instance;
    }

    std::shared_ptr<ChunkCache> ChunkCacheRegistry::forChunk(const MirChunk* chunk) {
        std::lock_guard<std::mutex> guard(this->mMutex);

        auto& cache = this->mCaches[chunk];
        if (!cache)
            cache = std::make_shared<ChunkCache>();

        return cache;
    }

    void ChunkCacheRegistry::release(const MirChunk* chunk) {
        std::lock_guard<std::mutex> guard(this->mMutex);
        this->mCaches.erase(chunk);
    }

    void ChunkCacheRegistry::releaseAll() {
        std::lock_guard<std::mutex> guard(this->mMutex);
        this->mCaches.clear();
    }
}
