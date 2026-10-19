#include <unordered_set>
#include <vector>

#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"

#include "LOICollectionA/frontend/ir/opt/passes/DeadValuePass.h"

namespace LOICollection::frontend::ir::opt {
    size_t DeadValuePass::run() {
        if (mChunk.code.empty())
            return 0;

        // An instruction is safe to delete as dead code when (a) it has no
        // observable side effect and (b) its destination register is never read.
        // We restrict (a) to value-producing ops that cannot panic or mutate
        // state: LOAD_CONST / LOAD_SLOT / MOVE plus the pure arithmetic,
        // comparison and logic families (DIV/MOD/POW excluded -- they can panic
        // on zero and deleting them would change a panic's timing). LOAD_VAR is
        // deliberately excluded: reading an uninitialised captured variable can
        // panic, and removing a reachable-but-dead load would mask that panic.
        // Stores, calls, array/field access and allocation all have effects or
        // can panic, so they are never removed here.
        auto isDeadValueSafe = [](MirOp op) {
            switch (op) {
                case MirOp::LOAD_CONST:
                case MirOp::LOAD_SLOT:
                case MirOp::MOVE:
                    return true;
                default:
                    break;
            }
            if (isPureBinary(op))
                return op != MirOp::DIV && op != MirOp::MOD && op != MirOp::POW;
            if (isPureUnary(op))
                return true;
            return false;
        };

        std::unordered_set<int> read;
        std::unordered_set<int> pinned;

        for (const MirInstr& instr : mChunk.code) {
            if (instr.src1 >= 0)
                read.insert(instr.src1);
            if (instr.src2 >= 0)
                read.insert(instr.src2);
            if (instr.src3 >= 0)
                read.insert(instr.src3);

            // The imm field is a register window count rooted at src1. The call
            // conventions read registers src1 .. src1+imm INCLUSIVE: call args
            // occupy src1 .. src1+imm-1 and the receiver/callee lives at
            // src1+imm (CALL_METHOD-family and CALL_NATIVE_METHOD read the
            // receiver from src1+imm; CALL_FUNC/CALL_LAMBDA read the function
            // from src2). Marking the window inclusive is required so the
            // receiver register is recognised as live and never deleted, and is a
            // safe over-approximation for any other op that uses imm as a count.
            if (instr.imm > 0 && instr.src1 >= 0) {
                for (int i = 0; i <= instr.imm; ++i)
                    read.insert(instr.src1 + i);
            }

            if (instr.op == MirOp::STORE_SLOT || instr.op == MirOp::LOAD_SLOT)
                pinned.insert(instr.operand);

            if (instr.op == MirOp::HALT)
                pinned.insert(0);

            if (instr.op == MirOp::PANIC && instr.src1 >= 0)
                read.insert(instr.src1);

            // MAKE_LAMBDA captures values from registers (capture.sourceSlot, a
            // register index into the frame's localPool) recorded in chunk
            // metadata rather than in the instruction's src fields. Those source
            // registers are live -- failing to treat them as read would let a
            // dead-value deletion discard the very value a closure captures.
            if (instr.op == MirOp::MAKE_LAMBDA) {
                const auto& meta = mChunk.lambdas[instr.operand];
                for (const auto& capture : meta.captures)
                    if (capture.sourceSlot >= 0)
                        read.insert(capture.sourceSlot);
            }
        }

        std::vector<MirInstr> out;
        out.reserve(mChunk.code.size());
        std::vector<int> oldToNew(mChunk.code.size(), -1);
        std::vector<int> origin;
        size_t eliminated = 0;

        for (size_t i = 0; i < mChunk.code.size(); ++i) {
            const MirInstr& instr = mChunk.code[i];

            if (isDeadValueSafe(instr.op) && instr.dst >= 0 &&
                read.find(instr.dst) == read.end() &&
                pinned.find(instr.dst) == pinned.end()) {
                ++eliminated;
                continue;
            }

            out.push_back(instr);
            oldToNew[i] = static_cast<int>(out.size()) - 1;
            origin.push_back(static_cast<int>(i));
        }

        if (eliminated == 0)
            return 0;

        for (size_t k = 0; k < out.size(); ++k) {
            if (!isJump(out[k].op))
                continue;

            const int oldTarget = origin[k] + 1 + out[k].operand;

            int newTarget = oldToNew[oldTarget];
            for (int j = oldTarget; j < static_cast<int>(oldToNew.size()) && newTarget < 0; ++j)
                newTarget = oldToNew[j];

            if (newTarget < 0)
                newTarget = static_cast<int>(out.size());

            out[k].operand = newTarget - static_cast<int>(k) - 1;
        }

        mChunk.code = std::move(out);

        return eliminated;
    }
}
