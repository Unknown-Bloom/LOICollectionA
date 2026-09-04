#pragma once

#include <vector>

#include "LOICollectionA/frontend/ir/ByteCode.h"

namespace LOICollection::frontend::ir::opt {
    class CSEPass {
    public:
        explicit CSEPass(BytecodeChunk& chunk)
        : mChunk(chunk) {}

        size_t run();

        struct Edit {
            int at = 0;
            int eraseTo = -1;
            std::vector<Instruction> insert;
        };

    private:
        BytecodeChunk& mChunk;
    };
}
