#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/AST.h"
#include "LOICollectionA/frontend/Callback.h"

#include "LOICollectionA/frontend/ir/Mir.h"

namespace LOICollection::frontend::ir {
    struct FieldCacheSlot {
        static constexpr int Capacity = 4;

        const FieldLayout* layouts[Capacity] = {};
        int slots[Capacity] = {};
        int head = -1;

        [[nodiscard]] int probe(const FieldLayout* layout) const {
            for (int i = 0; i < Capacity; ++i)
                if (layouts[i] == layout) return slots[i];

            return -1;
        }

        void store(const FieldLayout* layout, int slot) {
            head = (head + 1) % Capacity;
            layouts[head] = layout;
            slots[head] = slot;
        }
    };

    struct ChunkCache {
        std::unordered_map<int, FunctionCallCacheSlot> functionCallSlots;
        std::unordered_map<int, NativeMethodCacheSlot> nativeMethodSlots;
        std::unordered_map<int, NativeStaticMethodCacheSlot> nativeStaticMethodSlots;
        std::unordered_map<int, NativeValueMethodCacheSlot> nativeValueMethodSlots;
        std::unordered_map<int, NativeConstructorCacheSlot> nativeConstructorSlots;
        std::unordered_map<int, FieldLayoutPtr> classLayouts;
        std::unordered_map<const MirInstr*, FieldCacheSlot> fieldSlots;
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
