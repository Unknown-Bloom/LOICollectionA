#include <utility>

#include "LOICollectionA/frontend/ir/ChunkCache.h"

namespace LOICollection::frontend::ir {
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

        // Drop the per-chunk execution cache so a later chunk that happens to be
        // allocated at this same address cannot inherit a stale layout/field cache.
        ChunkCacheRegistry::getInstance().release(this);
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
