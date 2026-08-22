#pragma once

#include <cstddef>

#include "LOICollectionA/frontend/ir/ByteCode.h"

#include "LOICollectionA/base/Macro.h"

namespace LOICollection::frontend::ir {
    class Optimizer {
    public:
        struct Stats {
            size_t folded = 0;
            size_t removed = 0;
        };

        LOICOLLECTION_A_API Stats optimize(BytecodeChunk& chunk);

    private:
        Stats optimizeChunk(BytecodeChunk& chunk);
        Stats optimizeChunkOnce(BytecodeChunk& chunk);
    };
}
