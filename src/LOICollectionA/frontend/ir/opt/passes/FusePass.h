#pragma once

#include <cstddef>

#include "LOICollectionA/frontend/ir/Mir.h"

namespace LOICollection::frontend::ir::opt {
    class FusePass {
    public:
        explicit FusePass(MirChunk& chunk) : mChunk(chunk) {}

        size_t run();

    private:
        MirChunk& mChunk;
    };
}
