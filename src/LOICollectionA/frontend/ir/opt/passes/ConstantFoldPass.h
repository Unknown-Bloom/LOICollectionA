#pragma once

#include <string>
#include <unordered_map>

#include "LOICollectionA/frontend/ir/Mir.h"

#include "LOICollectionA/frontend/ir/opt/OptContext.h"
#include "LOICollectionA/frontend/ir/opt/analysis/JumpTargetAnalysis.h"

namespace LOICollection::frontend::ir::opt {
    // Constant folding on the register-based MIR.
    //
    // Each instruction names its inputs (`src1`/`src2`/`src3`) and output
    // (`dst`) explicitly, so we track a per-register map of known constant
    // values instead of a value stack. Tracking is dropped at jump targets and
    // after any instruction that may write variables (calls, field stores,
    // object construction), because those can invalidate locally cached values.
    class ConstantFoldPass {
    public:
        ConstantFoldPass(MirChunk& chunk, OptContext& ctx, const JumpTargetAnalysis& jumps)
        : mChunk(chunk),
          mCtx(ctx),
          mJumps(jumps) {}

        void run(bool enabled);

    private:
        struct Known {
            ValueNode::ValueType value;
            int producer = -1;
            bool removable = true;
        };

        struct Step {
            int emittedAt = -1;
        };

        Step fold(int oldIdx);

        Step foldBinary(const MirInstr& instr, int oldIdx);
        Step foldUnary(const MirInstr& instr, int oldIdx);
        Step foldComparison(const MirInstr& instr, int oldIdx);
        Step foldOptional(const MirInstr& instr);
        Step foldLoadSlot(const MirInstr& instr);
        Step foldStoreSlot(const MirInstr& instr);
        Step foldLoadVar(const MirInstr& instr);
        Step foldStoreVar(const MirInstr& instr);
        Step foldMakeArray(const MirInstr& instr, int oldIdx);
        Step foldLoadIndex(const MirInstr& instr, int oldIdx);
        Step foldStoreIndex(const MirInstr& instr);
        Step foldCall(const MirInstr& instr, int oldIdx);
        Step foldNativeMethod(const MirInstr& instr, int oldIdx);
        Step foldBranch(const MirInstr& instr, int oldIdx);

        Step emitConst(const ValueNode::ValueType& value, const SourceLocation& loc);
        Step emitMove(int dst, int src, const SourceLocation& loc);
        Step emitOriginal(const MirInstr& instr);
        Step emitOriginalAfterForget(const MirInstr& instr);

        bool knownReg(int reg, Known& out) const;
        void forgetDst(int dst);

        MirChunk& mChunk;
        OptContext& mCtx;
        const JumpTargetAnalysis& mJumps;

        std::unordered_map<int, Known> mRegValues;
        std::unordered_map<int, Known> mSlotValues;
        std::unordered_map<std::string, Known> mNameValues;
    };
}
