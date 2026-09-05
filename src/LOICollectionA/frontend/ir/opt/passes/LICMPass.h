#pragma once

#include <vector>

#include "LOICollectionA/frontend/ir/Mir.h"

#include "LOICollectionA/frontend/ir/opt/analysis/ControlFlowGraph.h"
#include "LOICollectionA/frontend/ir/opt/analysis/LoopAnalysis.h"

namespace LOICollection::frontend::ir::opt {
    // Loop-invariant code motion on the register-based MIR.
    //
    // A contiguous prefix of pure instructions at the top of a loop header whose
    // operands are all loop-invariant (constants or values produced by earlier
    // hoisted instructions) is moved into the preheader. Because the operands are
    // loop-invariant, the computation yields the same value every iteration, so
    // running it once before the loop is correct.
    class LICMPass {
    public:
        explicit LICMPass(MirChunk& chunk)
        : mChunk(chunk) {}

        size_t run();

    private:
        struct Edit {
            int at = 0;
            bool erase = false;
            std::vector<MirInstr> insert;
        };

        size_t hoist(const ControlFlowGraph& cfg, const NaturalLoop& loop);

        static void apply(std::vector<MirInstr>& code, const std::vector<Edit>& edits);

        MirChunk& mChunk;
    };
}
