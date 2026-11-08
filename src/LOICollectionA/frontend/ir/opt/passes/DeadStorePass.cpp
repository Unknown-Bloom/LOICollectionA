#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"

#include "LOICollectionA/frontend/ir/opt/passes/DeadStorePass.h"

namespace LOICollection::frontend::ir::opt::detail {
    bool closesBlock(Op op) {
        return isJump(op) || isTerminator(op) || canWriteVariables(op);
    }
}

namespace LOICollection::frontend::ir::opt {
    using namespace detail;

    void DeadStorePass::run(bool enabled) {
        mPending.clear();

        if (!enabled)
            return;

        for (size_t j = 0; j < mCtx.foldedCode.size(); ++j) {
            if (j < mCtx.newToOld.size() && mJumps.isTarget(mCtx.newToOld[j])) {
                mPending.clear();
                continue;
            }

            const Instr instr = mCtx.foldedCode[j];

            switch (instr.op) {
                case Op::LOAD_SLOT:
                    mPending.erase(VariableKey{ instr.operand });
                    break;

                case Op::LOAD_VAR:
                    mPending.erase(VariableKey{ std::get<std::string>(mChunk.constants[instr.operand]) });
                    break;

                case Op::STORE_SLOT:
                    this->recordStore(VariableKey{ instr.operand }, static_cast<int>(j), instr.operand);
                    break;

                case Op::STORE_VAR:
                    this->recordStore(
                        VariableKey{ std::get<std::string>(mChunk.constants[instr.operand]) },
                        static_cast<int>(j),
                        -1
                    );
                    break;

                case Op::MAKE_LAMBDA: {
                    const auto& meta = mChunk.lambdas[instr.operand];
                    for (const auto& capture : meta.captures) {
                        if (capture.sourceSlot >= 0)
                            mPending.erase(VariableKey{ capture.sourceSlot });
                        else
                            mPending.erase(VariableKey{ capture.name });
                    }
                    break;
                }

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
        Instr& dead = mCtx.foldedCode[at];

        switch (dead.op) {
            case Op::STORE_SLOT:
            case Op::STORE_VAR:
                mCtx.dropped[at] = true;
                break;

            default:
                return;
        }

        ++mCtx.stats.removed;
    }
}
