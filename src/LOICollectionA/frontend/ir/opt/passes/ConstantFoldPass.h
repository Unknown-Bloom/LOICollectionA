#pragma once

#include <string>
#include <unordered_map>

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
        struct Known {
            ValueNode::ValueType value;
            int producer = -1;
            bool removable = true;
        };

        struct Step {
            int emittedAt = -1;
        };

        Step fold(int oldIdx);

        Step foldBinary(const MirInstr& instr);
        Step foldUnary(const MirInstr& instr);
        Step foldComparison(const MirInstr& instr);
        Step foldOptional(const MirInstr& instr);
        Step foldLoadSlot(const MirInstr& instr);
        Step foldStoreSlot(const MirInstr& instr);
        Step foldLoadVar(const MirInstr& instr);
        Step foldStoreVar(const MirInstr& instr);
        Step foldMakeArray(const MirInstr& instr);
        Step foldLoadIndex(const MirInstr& instr);
        Step foldStoreIndex(const MirInstr& instr);
        Step foldCall(const MirInstr& instr);
        Step foldNativeMethod(const MirInstr& instr);
        Step foldBranch(const MirInstr& instr);

        Step emitConst(int dst, const ValueNode::ValueType& value, const SourceLocation& loc);
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
