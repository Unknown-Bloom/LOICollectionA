#pragma once

#include <vector>

#include "LOICollectionA/frontend/ir/Mir.h"

namespace LOICollection::frontend::ir::opt {
    class CSEPass {
    public:
        explicit CSEPass(MirChunk& chunk)
        : mChunk(chunk) {}

        size_t run();

    private:
        MirChunk& mChunk;
    };
}
