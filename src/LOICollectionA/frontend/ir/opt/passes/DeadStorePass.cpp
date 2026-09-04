#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"

#include "LOICollectionA/frontend/ir/opt/passes/DeadStorePass.h"

namespace LOICollection::frontend::ir::opt {
    namespace {
        bool capturesLocals(OpCode op) {
            return op == OpCode::MAKE_LAMBDA;
        }

        bool closesBlock(OpCode op) {
            return isJump(op) || isTerminator(op) || canWriteVariables(op) || capturesLocals(op);
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

            const Instruction instr = mCtx.foldedCode[j];

            switch (instr.op) {
                case OpCode::LOAD_SLOT:
                    mPending.erase(VariableKey{ instr.operand });
                    break;

                case OpCode::LOAD_VAR:
                    mPending.erase(VariableKey{ std::get<std::string>(mChunk.constants[instr.operand]) });
                    break;

                case OpCode::STORE_SLOT:
                case OpCode::DUP_STORE_SLOT:
                    this->recordStore(VariableKey{ instr.operand }, static_cast<int>(j), instr.operand);
                    break;

                case OpCode::STORE_VAR:
                case OpCode::DUP_STORE:
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
        Instruction& dead = mCtx.foldedCode[at];

        switch (dead.op) {
            case OpCode::STORE_SLOT:
            case OpCode::STORE_VAR:
                dead.op = OpCode::POP;
                dead.operand = 0;
                break;

            case OpCode::DUP_STORE_SLOT:
            case OpCode::DUP_STORE:
                mCtx.dropped[at] = true;
                break;

            default:
                return;
        }

        ++mCtx.stats.removed;
    }
}
