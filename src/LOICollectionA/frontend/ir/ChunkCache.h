#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/AST.h"
#include "LOICollectionA/frontend/Callback.h"

#include "LOICollectionA/frontend/ir/Mir.h"

namespace LOICollection::frontend::ir {
    LOICOLLECTION_A_NDAPI std::uint64_t chunkGeneration();

    struct ChunkCache {
        std::unordered_map<int, FunctionCallCacheSlot> functionCallSlots;
        std::unordered_map<int, NativeMethodCacheSlot> nativeMethodSlots;
        std::unordered_map<int, NativeStaticMethodCacheSlot> nativeStaticMethodSlots;
        std::unordered_map<int, NativeValueMethodCacheSlot> nativeValueMethodSlots;
        std::unordered_map<int, NativeConstructorCacheSlot> nativeConstructorSlots;
        std::unordered_map<int, FieldLayoutPtr> classLayouts;
    };

    class ChunkCacheRegistry {
    public:
        LOICOLLECTION_A_NDAPI static ChunkCacheRegistry& getInstance();

        LOICOLLECTION_A_NDAPI std::shared_ptr<ChunkCache> forChunk(const MirChunk* chunk);

        LOICOLLECTION_A_API void release(const MirChunk* chunk);
        LOICOLLECTION_A_API void releaseAll();

    private:
        ChunkCacheRegistry() = default;

        std::mutex mMutex;
        std::unordered_map<const MirChunk*, std::shared_ptr<ChunkCache>> mCaches;
    };
}
