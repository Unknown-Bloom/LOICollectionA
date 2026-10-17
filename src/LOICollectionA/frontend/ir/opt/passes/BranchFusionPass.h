#pragma once

#include <cstddef>
#include <vector>

#include "LOICollectionA/frontend/ir/Mir.h"

#include "LOICollectionA/frontend/ir/opt/analysis/JumpTargetAnalysis.h"

namespace LOICollection::frontend::ir::opt {
    class BranchFusionPass {
    public:
        BranchFusionPass(MirChunk& chunk, const JumpTargetAnalysis& jumps)
        : mChunk(chunk), mJumps(jumps) {}

        std::size_t run(bool enabled);

    private:
        void compact(const std::vector<bool>& dropped);

        MirChunk& mChunk;
        const JumpTargetAnalysis& mJumps;
    };
}
