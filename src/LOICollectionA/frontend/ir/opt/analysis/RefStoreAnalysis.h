#pragma once

#include "LOICollectionA/frontend/ir/Mir.h"

namespace LOICollection::frontend::ir::opt {
    class RefStoreAnalysis {
    public:
        explicit RefStoreAnalysis(MirChunk& chunk) : mChunk(chunk) {}

        void run();

    private:
        MirChunk& mChunk;
    };
}
