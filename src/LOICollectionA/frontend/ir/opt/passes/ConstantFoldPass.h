#pragma once

#include <string>
#include <unordered_map>

#include "LOICollectionA/frontend/ir/Lir.h"

#include "LOICollectionA/frontend/ir/opt/OptContext.h"
#include "LOICollectionA/frontend/ir/opt/analysis/JumpTargetAnalysis.h"

namespace LOICollection::frontend::ir::opt {
    class ConstantFoldPass {
    public:
        ConstantFoldPass(Chunk& chunk, OptContext& ctx, const JumpTargetAnalysis& jumps)
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

        Step foldBinary(const Instr& instr);
        Step foldUnary(const Instr& instr);
        Step foldComparison(const Instr& instr);
        Step foldOptional(const Instr& instr);
        Step foldLoadSlot(const Instr& instr);
        Step foldStoreSlot(const Instr& instr);
        Step foldLoadVar(const Instr& instr);
        Step foldStoreVar(const Instr& instr);
        Step foldMakeArray(const Instr& instr);
        Step foldLoadIndex(const Instr& instr);
        Step foldStoreIndex(const Instr& instr);
        Step foldCall(const Instr& instr);
        Step foldNativeMethod(const Instr& instr);
        Step foldBranch(const Instr& instr);

        Step emitConst(int dst, const ValueNode::ValueType& value, const SourceLocation& loc);
        Step emitMove(int dst, int src, const SourceLocation& loc);
        Step emitOriginal(const Instr& instr);
        Step emitOriginalAfterForget(const Instr& instr);

        bool knownReg(int reg, Known& out) const;
        void forgetDst(int dst);

        Chunk& mChunk;
        OptContext& mCtx;
        const JumpTargetAnalysis& mJumps;

        std::unordered_map<int, Known> mRegValues;
        std::unordered_map<int, Known> mSlotValues;
        std::unordered_map<std::string, Known> mNameValues;
    };
}
