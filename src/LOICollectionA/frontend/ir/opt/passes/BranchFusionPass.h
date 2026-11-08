#pragma once

#include <cstddef>
#include <vector>

#include "LOICollectionA/frontend/ir/Lir.h"

#include "LOICollectionA/frontend/ir/opt/analysis/JumpTargetAnalysis.h"

namespace LOICollection::frontend::ir::opt {
    class BranchFusionPass {
    public:
        BranchFusionPass(Chunk& chunk, const JumpTargetAnalysis& jumps)
        : mChunk(chunk), mJumps(jumps) {}

        std::size_t run(bool enabled);

    private:
        void compact(const std::vector<bool>& dropped);

        Chunk& mChunk;
        const JumpTargetAnalysis& mJumps;
    };
}
