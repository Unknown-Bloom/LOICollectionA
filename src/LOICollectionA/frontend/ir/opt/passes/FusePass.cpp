#include "LOICollectionA/frontend/ir/opt/analysis/JumpTargetAnalysis.h"
#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"

#include "LOICollectionA/frontend/ir/opt/passes/FusePass.h"

namespace LOICollection::frontend::ir::opt {
    namespace {
        bool isFusableBinary(const MirInstr& instr) {
            if (instr.type.kind != TypeKind::Int)
                return false;

            switch (instr.op) {
                case MirOp::ADD:
                case MirOp::SUB:
                case MirOp::MUL:
                case MirOp::MOD:
                case MirOp::CMP_EQ:
                case MirOp::CMP_NE:
                case MirOp::CMP_GT:
                case MirOp::CMP_LT:
                case MirOp::CMP_GE:
                case MirOp::CMP_LE:
                    return true;
                default:
                    return false;
            }
        }

        MirOp fusedOp(MirOp op) {
            switch (op) {
                case MirOp::ADD: return MirOp::ADD_SS;
                case MirOp::SUB: return MirOp::SUB_SS;
                case MirOp::MUL: return MirOp::MUL_SS;
                case MirOp::MOD: return MirOp::MOD_SS;
                case MirOp::CMP_EQ: return MirOp::CMP_EQ_SS;
                case MirOp::CMP_NE: return MirOp::CMP_NE_SS;
                case MirOp::CMP_GT: return MirOp::CMP_GT_SS;
                case MirOp::CMP_LT: return MirOp::CMP_LT_SS;
                case MirOp::CMP_GE: return MirOp::CMP_GE_SS;
                case MirOp::CMP_LE: return MirOp::CMP_LE_SS;
                default: return op;
            }
        }

        bool packable(int slot) {
            return slot >= 0 && slot < (1 << 15);
        }
    }

    size_t FusePass::run() {
        const JumpTargetAnalysis jumps{ mChunk.code };

        std::vector<MirInstr> out;
        std::vector<int> origin;
        out.reserve(mChunk.code.size());
        origin.reserve(mChunk.code.size());

        for (size_t i = 0; i < mChunk.code.size(); ++i) {
            if (i + 2 < mChunk.code.size()) {
                const MirInstr& a = mChunk.code[i];
                const MirInstr& b = mChunk.code[i + 1];
                const MirInstr& c = mChunk.code[i + 2];

                if (a.op == MirOp::LOAD_SLOT && b.op == MirOp::LOAD_SLOT &&
                    isFusableBinary(c) &&
                    !jumps.isTarget(static_cast<int>(i + 1)) &&
                    !jumps.isTarget(static_cast<int>(i + 2)) &&
                    packable(a.operand) && packable(b.operand)) {
                    origin.push_back(static_cast<int>(i));
                    out.push_back({ fusedOp(c.op), (a.operand << 16) | b.operand, c.loc, c.type });
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
