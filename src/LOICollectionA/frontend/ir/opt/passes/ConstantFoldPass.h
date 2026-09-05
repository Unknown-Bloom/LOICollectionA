#pragma once

#include <string>

#include "LOICollectionA/frontend/ir/Mir.h"
#include "LOICollectionA/frontend/ir/Mir.h"

#include "LOICollectionA/frontend/ir/opt/OptContext.h"
#include "LOICollectionA/frontend/ir/opt/analysis/JumpTargetAnalysis.h"

namespace LOICollection::frontend::ir::opt {
    class ConstantFoldPass {
    public:
        ConstantFoldPass(MirChunk& chunk, OptContext& ctx, const JumpTargetAnalysis& jumps)
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

        Step foldPush(const MirInstr& instr, int oldIdx);
        Step foldNullish(const MirInstr& instr, int oldIdx);
        Step foldDupIsNone(const MirInstr& instr);
        Step foldVariable(const MirInstr& instr);
        Step foldStack(const MirInstr& instr, int oldIdx);
        Step foldArithmetic(const MirInstr& instr, int oldIdx);
        Step foldUnary(const MirInstr& instr, int oldIdx);
        Step foldComparison(const MirInstr& instr, int oldIdx);
        Step foldMakeArray(const MirInstr& instr, int oldIdx);
        Step foldLoadIndex(const MirInstr& instr, int oldIdx);
        Step foldStoreIndex(const MirInstr& instr);
        Step foldCall(const MirInstr& instr, int oldIdx);
        Step foldBranch(const MirInstr& instr, int oldIdx);
        Step foldNativeMethod(const MirInstr& instr, int oldIdx);

        Step emitUnknown(const MirInstr& instr);
        Step emitOpaque(const MirInstr& instr);

        template <typename Fold>
        Step foldOperand(const MirInstr& instr, int oldIdx, Fold&& fold);

        int emitConstant(const ValueNode::ValueType& value, const SourceLocation& loc);

        void trackSlot(int slot, const StackEntry& value);
        void trackName(const std::string& name, const StackEntry& value);

        bool reachedOnlyByBackwardJumps(int producer) const;

        MirChunk& mChunk;
        OptContext& mCtx;
        const JumpTargetAnalysis& mJumps;
    };
}
