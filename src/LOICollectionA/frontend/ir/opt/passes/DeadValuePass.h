#pragma once

#include "LOICollectionA/frontend/ir/Mir.h"

namespace LOICollection::frontend::ir::opt {
    class DeadValuePass {
    public:
        explicit DeadValuePass(MirChunk& chunk)
        : mChunk(chunk) {}

        size_t run();

    private:
        MirChunk& mChunk;
    };
}
