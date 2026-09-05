#pragma once

#include <vector>

#include "LOICollectionA/frontend/ir/Mir.h"

namespace LOICollection::frontend::ir::opt {
    class CSEPass {
    public:
        explicit CSEPass(MirChunk& chunk)
        : mChunk(chunk) {}

        size_t run();

        struct Edit {
            int at = 0;
            int eraseTo = -1;
            std::vector<MirInstr> insert;
        };

    private:
        MirChunk& mChunk;
    };
}
