#pragma once

#include <string>

#include "LOICollectionA/frontend/ir/ByteCode.h"
#include "LOICollectionA/frontend/ir/OpCode.h"

#include "LOICollectionA/frontend/ir/opt/OptContext.h"
#include "LOICollectionA/frontend/ir/opt/analysis/JumpTargetAnalysis.h"

namespace LOICollection::frontend::ir::opt {
    class ConstantFoldPass {
    public:
        ConstantFoldPass(BytecodeChunk& chunk, OptContext& ctx, const JumpTargetAnalysis& jumps)
        : mChunk(chunk),
          mCtx(ctx),
          mJumps(jumps) {}

        void run(bool enabled);

    private:
        struct Step {
            int emittedAt = -1;
            bool skipNext = false;
        };

        Step fold(int oldIdx);

        Step foldPush(const Instruction& instr, int oldIdx);
        Step foldNullish(const Instruction& instr, int oldIdx);
        Step foldDupIsNone(const Instruction& instr);
        Step foldVariable(const Instruction& instr, int oldIdx);
        Step foldStack(const Instruction& instr, int oldIdx);
        Step foldArithmetic(const Instruction& instr, int oldIdx);
        Step foldUnary(const Instruction& instr, int oldIdx);
        Step foldComparison(const Instruction& instr, int oldIdx);
        Step foldMakeArray(const Instruction& instr, int oldIdx);
        Step foldLoadIndex(const Instruction& instr, int oldIdx);
        Step foldStoreIndex(const Instruction& instr);
        Step foldCall(const Instruction& instr, int oldIdx);
        Step foldBranch(const Instruction& instr, int oldIdx);

        Step emitUnknown(const Instruction& instr);
        Step emitOpaque(const Instruction& instr);

        template <typename Fold>
        Step foldOperand(const Instruction& instr, int oldIdx, Fold&& fold);

        int emitConstant(const ValueNode::ValueType& value, const SourceLocation& loc);

        void trackSlot(int slot, const StackEntry& value);
        void trackName(const std::string& name, const StackEntry& value);

        bool reachedOnlyByBackwardJumps(int producer) const;

        BytecodeChunk& mChunk;
        OptContext& mCtx;
        const JumpTargetAnalysis& mJumps;
    };
}
