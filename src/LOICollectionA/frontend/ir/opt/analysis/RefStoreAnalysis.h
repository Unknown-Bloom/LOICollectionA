#pragma once

#include "LOICollectionA/frontend/ir/Lir.h"

namespace LOICollection::frontend::ir::opt {
    class RefStoreAnalysis {
    public:
        explicit RefStoreAnalysis(Chunk& chunk) : mChunk(chunk) {}

        void run();

    private:
        Chunk& mChunk;
    };
}
