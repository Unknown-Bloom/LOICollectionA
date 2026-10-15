#include <utility>

#include "LOICollectionA/frontend/ir/ChunkCache.h"

namespace LOICollection::frontend::ir {
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
