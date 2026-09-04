#include "LOICollectionA/frontend/ir/opt/analysis/JumpTargetAnalysis.h"
#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"

#include "LOICollectionA/frontend/ir/opt/passes/FusePass.h"

namespace LOICollection::frontend::ir::opt {
    namespace {
        bool isFusableBinary(OpCode op) {
            switch (op) {
                case OpCode::ADD_I:
                case OpCode::SUB_I:
                case OpCode::MUL_I:
                case OpCode::MOD_I:
                case OpCode::CMP_EQ_I:
                case OpCode::CMP_NE_I:
                case OpCode::CMP_GT_I:
                case OpCode::CMP_LT_I:
                case OpCode::CMP_GE_I:
                case OpCode::CMP_LE_I:
                    return true;
                default:
                    return false;
            }
        }

        OpCode fusedOp(OpCode op) {
            switch (op) {
                case OpCode::ADD_I: return OpCode::ADD_SS;
                case OpCode::SUB_I: return OpCode::SUB_SS;
                case OpCode::MUL_I: return OpCode::MUL_SS;
                case OpCode::MOD_I: return OpCode::MOD_SS;
                case OpCode::CMP_EQ_I: return OpCode::CMP_EQ_SS;
                case OpCode::CMP_NE_I: return OpCode::CMP_NE_SS;
                case OpCode::CMP_GT_I: return OpCode::CMP_GT_SS;
                case OpCode::CMP_LT_I: return OpCode::CMP_LT_SS;
                case OpCode::CMP_GE_I: return OpCode::CMP_GE_SS;
                case OpCode::CMP_LE_I: return OpCode::CMP_LE_SS;
                default: return op;
            }
        }

        bool packable(int slot) {
            return slot >= 0 && slot < (1 << 15);
        }
    }

    size_t FusePass::run() {
        const JumpTargetAnalysis jumps{ mChunk.code };

        std::vector<Instruction> out;
        std::vector<int> origin;
        out.reserve(mChunk.code.size());
        origin.reserve(mChunk.code.size());

        for (size_t i = 0; i < mChunk.code.size(); ++i) {
            if (i + 2 < mChunk.code.size()) {
                const Instruction& a = mChunk.code[i];
                const Instruction& b = mChunk.code[i + 1];
                const Instruction& c = mChunk.code[i + 2];

                if (a.op == OpCode::LOAD_SLOT && b.op == OpCode::LOAD_SLOT &&
                    isFusableBinary(c.op) &&
                    !jumps.isTarget(static_cast<int>(i + 1)) &&
                    !jumps.isTarget(static_cast<int>(i + 2)) &&
                    packable(a.operand) && packable(b.operand)) {
                    origin.push_back(static_cast<int>(i));
                    out.push_back({ fusedOp(c.op), (a.operand << 16) | b.operand, c.loc });
                    i += 2;
                    continue;
                }
            }

            origin.push_back(static_cast<int>(i));
            out.push_back(mChunk.code[i]);
        }

        std::vector<int> oldToNew(mChunk.code.size(), -1);
        for (size_t k = 0; k < out.size(); ++k)
            oldToNew[static_cast<size_t>(origin[k])] = static_cast<int>(k);

        for (size_t k = 0; k < out.size(); ++k) {
            if (!isJump(out[k].op))
                continue;

            const int oldTarget = origin[k] + 1 + out[k].operand;
            int newTarget = static_cast<int>(out.size());
            if (oldTarget >= 0 && oldTarget < static_cast<int>(mChunk.code.size())) {
                newTarget = oldToNew[static_cast<size_t>(oldTarget)];
                for (int j = oldTarget; j < static_cast<int>(mChunk.code.size()) && newTarget < 0; ++j)
                    newTarget = oldToNew[static_cast<size_t>(j)];
            }

            out[k].operand = newTarget - static_cast<int>(k) - 1;
        }

        const size_t fused = mChunk.code.size() - out.size();
        mChunk.code = std::move(out);
        return fused;
    }
}
