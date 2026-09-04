#include <vector>

#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"

#include "LOICollectionA/frontend/ir/opt/passes/DeadCodePass.h"

namespace LOICollection::frontend::ir::opt {
    namespace {
        class JumpChains {
        public:
            JumpChains(
                const std::vector<Instruction>& folded,
                const std::vector<int>& newToOld,
                const std::vector<int>& oldToNew
            )
            : mFolded(folded),
              mNewToOld(newToOld),
              mOldToNew(oldToNew) {}
    
            int targetOf(int j) const {
                if (j < 0 || j >= static_cast<int>(mNewToOld.size()))
                    return -1;
    
                int oldTarget = mNewToOld[j] + 1 + mFolded[j].operand;
                if (oldTarget < 0 || oldTarget >= static_cast<int>(mOldToNew.size()))
                    return -1;
    
                return mOldToNew[oldTarget];
            }
    
            int resolve(int j) const {
                int target = targetOf(j);
                int guard = 0;
    
                while (target >= 0 && target < static_cast<int>(mFolded.size()) && mFolded[target].op == OpCode::JMP) {
                    if (++guard > static_cast<int>(mFolded.size()))
                        break;
    
                    int next = targetOf(target);
                    if (next == target || next < 0)
                        break;
    
                    target = next;
                }
    
                return target;
            }
    
        private:
            const std::vector<Instruction>& mFolded;
            const std::vector<int>& mNewToOld;
            const std::vector<int>& mOldToNew;
        };
    
        void compact(OptContext& ctx) {
            std::vector<Instruction> compactCode;
            std::vector<int> compactNewToOld;
            std::vector<int> compactOldToNew(ctx.oldToNew.size(), -1);
    
            for (size_t j = 0; j < ctx.foldedCode.size(); ++j) {
                if (ctx.dropped[j])
                    continue;
    
                compactOldToNew[ctx.newToOld[j]] = static_cast<int>(compactCode.size());
                compactCode.push_back(ctx.foldedCode[j]);
                compactNewToOld.push_back(ctx.newToOld[j]);
            }
    
            ctx.foldedCode = std::move(compactCode);
            ctx.newToOld = std::move(compactNewToOld);
            ctx.oldToNew = std::move(compactOldToNew);
        }
    
        std::vector<bool> markReachable(const std::vector<Instruction>& folded, const JumpChains& chains) {
            std::vector<bool> reachable(folded.size(), false);
            std::vector<int> queue;
    
            reachable[0] = true;
            queue.push_back(0);
    
            while (!queue.empty()) {
                int j = queue.back();
                queue.pop_back();
    
                auto mark = [&](int target) {
                    if (target >= 0 && target < static_cast<int>(folded.size()) && !reachable[target]) {
                        reachable[target] = true;
                        queue.push_back(target);
                    }
                };
    
                if (isJump(folded[j].op))
                    mark(chains.resolve(j));
    
                if (!isTerminator(folded[j].op))
                    mark(j + 1);
            }
    
            return reachable;
        }
    
        void remapJumps(
            std::vector<Instruction>& finalCode,
            const std::vector<int>& finalToFolded,
            const std::vector<int>& foldedToFinal,
            const JumpChains& chains,
            bool thread
        ) {
            for (size_t f = 0; f < finalCode.size(); ++f) {
                auto& instr = finalCode[f];
                if (!isJump(instr.op))
                    continue;
    
                int foldedIdx = finalToFolded[f];
                int target = thread ? chains.resolve(foldedIdx) : chains.targetOf(foldedIdx);
                int finalTarget = (target >= 0 && target < static_cast<int>(foldedToFinal.size()))
                    ? foldedToFinal[target] : static_cast<int>(f) + 1;
    
                instr.operand = finalTarget - static_cast<int>(f) - 1;
            }
        }
    }

    void DeadCodePass::run(BytecodeChunk& chunk, OptContext& ctx, bool eliminate) {
        compact(ctx);

        const JumpChains chains{ ctx.foldedCode, ctx.newToOld, ctx.oldToNew };
        const std::vector<bool> reachable = eliminate
            ? markReachable(ctx.foldedCode, chains)
            : std::vector<bool>(ctx.foldedCode.size(), true);

        std::vector<Instruction> finalCode;
        std::vector<int> finalToFolded;
        std::vector<int> foldedToFinal(ctx.foldedCode.size(), -1);

        for (size_t j = 0; j < ctx.foldedCode.size(); ++j) {
            const bool dead = !reachable[j] ||
                (eliminate && isJump(ctx.foldedCode[j].op) && chains.resolve(static_cast<int>(j)) == static_cast<int>(j) + 1);

            if (dead) {
                ++ctx.stats.removed;
                continue;
            }

            foldedToFinal[j] = static_cast<int>(finalCode.size());
            finalToFolded.push_back(static_cast<int>(j));
            finalCode.push_back(ctx.foldedCode[j]);
        }

        remapJumps(finalCode, finalToFolded, foldedToFinal, chains, eliminate);

        chunk.code = std::move(finalCode);
    }
}
