#pragma once

#include <cstddef>

#include "LOICollectionA/frontend/ir/ByteCode.h"

namespace LOICollection::frontend::ir::opt {
    class FusePass {
    public:
        explicit FusePass(BytecodeChunk& chunk) : mChunk(chunk) {}

        size_t run();

    private:
        BytecodeChunk& mChunk;
    };
}
