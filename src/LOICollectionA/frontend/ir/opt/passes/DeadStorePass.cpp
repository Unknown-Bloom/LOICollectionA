#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"

#include "LOICollectionA/frontend/ir/opt/passes/DeadStorePass.h"

namespace LOICollection::frontend::ir::opt {
    namespace {
        bool closesBlock(MirOp op) {
            return isJump(op) || isTerminator(op) || canWriteVariables(op);
        }
    }

    void DeadStorePass::run(bool enabled) {
        mPending.clear();

        if (!enabled)
            return;

        for (size_t j = 0; j < mCtx.foldedCode.size(); ++j) {
            if (j < mCtx.newToOld.size() && mJumps.isTarget(mCtx.newToOld[j])) {
                mPending.clear();
                continue;
            }

            const MirInstr instr = mCtx.foldedCode[j];

            switch (instr.op) {
                case MirOp::LOAD_SLOT:
                    mPending.erase(VariableKey{ instr.operand });
                    break;

                case MirOp::LOAD_VAR:
                    mPending.erase(VariableKey{ std::get<std::string>(mChunk.constants[instr.operand]) });
                    break;

                case MirOp::STORE_SLOT:
                    this->recordStore(VariableKey{ instr.operand }, static_cast<int>(j), instr.operand);
                    break;

                case MirOp::STORE_VAR:
                    this->recordStore(
                        VariableKey{ std::get<std::string>(mChunk.constants[instr.operand]) },
                        static_cast<int>(j),
                        -1
                    );
                    break;

                default:
                    break;
            }

            if (closesBlock(instr.op))
                mPending.clear();
        }
    }

    void DeadStorePass::recordStore(const VariableKey& key, int at, int slot) {
        if (slot >= mChunk.slotCount) {
            mPending.clear();
            return;
        }

        auto [it, inserted] = mPending.emplace(key, at);
        if (inserted)
            return;

        this->killStore(it->second);
        it->second = at;
    }

    void DeadStorePass::killStore(int at) {
        MirInstr& dead = mCtx.foldedCode[at];

        switch (dead.op) {
            case MirOp::STORE_SLOT:
            case MirOp::STORE_VAR:
                mCtx.dropped[at] = true;
                break;

            default:
                return;
        }

        ++mCtx.stats.removed;
    }
}
