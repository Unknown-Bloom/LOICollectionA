#pragma once

#include <vector>

#include "LOICollectionA/frontend/ir/Lir.h"

namespace LOICollection::frontend::ir::opt {
    class CSEPass {
    public:
        explicit CSEPass(Chunk& chunk)
        : mChunk(chunk) {}

        size_t run();

    private:
        Chunk& mChunk;
    };
}
