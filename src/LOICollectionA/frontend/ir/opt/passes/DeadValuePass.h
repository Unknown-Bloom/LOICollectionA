#pragma once

#include "LOICollectionA/frontend/ir/Lir.h"

namespace LOICollection::frontend::ir::opt {
    class DeadValuePass {
    public:
        explicit DeadValuePass(Chunk& chunk)
        : mChunk(chunk) {}

        size_t run();

    private:
        Chunk& mChunk;
    };
}
