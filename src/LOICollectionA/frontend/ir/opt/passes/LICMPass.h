#pragma once

#include <vector>

#include "LOICollectionA/frontend/ir/ByteCode.h"

#include "LOICollectionA/frontend/ir/opt/analysis/ControlFlowGraph.h"
#include "LOICollectionA/frontend/ir/opt/analysis/LoopAnalysis.h"

namespace LOICollection::frontend::ir::opt {
    class LICMPass {
    public:
        explicit LICMPass(BytecodeChunk& chunk)
        : mChunk(chunk) {}

        size_t run();

    private:
        // `retarget` names which inserted instruction inherits the jumps that used
        // to land on `at`, so a back edge keeps pointing at the loop header after
        // the instruction it targeted is deleted.
        struct Edit {
            int at = 0;
            bool erase = false;
            int retarget = -1;
            std::vector<Instruction> insert;
        };

        size_t hoist(const ControlFlowGraph& cfg, const NaturalLoop& loop);

        std::vector<bool> writtenSlots(const ControlFlowGraph& cfg, const NaturalLoop& loop) const;

        bool isHoistable(const Instruction& instr, const std::vector<bool>& written) const;

        static void apply(std::vector<Instruction>& code, const std::vector<Edit>& edits);

        BytecodeChunk& mChunk;
    };
}
