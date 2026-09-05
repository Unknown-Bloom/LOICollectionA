#pragma once

#include <vector>

#include "LOICollectionA/frontend/ir/Mir.h"

#include "LOICollectionA/frontend/ir/opt/analysis/ControlFlowGraph.h"
#include "LOICollectionA/frontend/ir/opt/analysis/LoopAnalysis.h"

namespace LOICollection::frontend::ir::opt {
    class LICMPass {
    public:
        explicit LICMPass(MirChunk& chunk)
        : mChunk(chunk) {}

        size_t run();

    private:
        struct Edit {
            int at = 0;
            bool erase = false;
            int retarget = -1;
            std::vector<MirInstr> insert;
        };

        size_t hoist(const ControlFlowGraph& cfg, const NaturalLoop& loop);

        std::vector<bool> writtenSlots(const ControlFlowGraph& cfg, const NaturalLoop& loop) const;

        bool isHoistable(const MirInstr& instr, const std::vector<bool>& written) const;

        static void apply(std::vector<MirInstr>& code, const std::vector<Edit>& edits);

        MirChunk& mChunk;
    };
}
