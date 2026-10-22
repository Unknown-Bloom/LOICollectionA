#include <algorithm>
#include <unordered_set>

#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"

#include "LOICollectionA/frontend/ir/opt/passes/LICMPass.h"

namespace LOICollection::frontend::ir::opt::detail {
    
    
    
    
    
    
    
    
    
    
    
    
    bool isInvariant(
        const MirInstr& instr,
        const std::unordered_set<int>& invariantRegs,
        const std::unordered_set<int>& loopWritten
    ) {
        if (instr.op == MirOp::LOAD_CONST)
            return true;

        
        
        
        
        
        
        if (instr.op == MirOp::LOAD_SLOT)
            return loopWritten.count(instr.operand) == 0;

        if (instr.op == MirOp::MOVE)
            return invariantRegs.count(instr.src1) > 0;

        if (isPureBinary(instr.op)) {
            if (instr.op == MirOp::DIV || instr.op == MirOp::MOD || instr.op == MirOp::POW)
                return false;
            return invariantRegs.count(instr.src1) > 0 && invariantRegs.count(instr.src2) > 0;
        }

        if (isPureUnary(instr.op))
            return invariantRegs.count(instr.src1) > 0;

        return false;
    }
}

namespace LOICollection::frontend::ir::opt {
    using namespace detail;

    size_t LICMPass::run() {
        size_t hoisted = 0;

        for (int round = 0; round < 4; ++round) {
            const ControlFlowGraph cfg{ mChunk.code };

            size_t moved = 0;
            for (const auto& loop : findNaturalLoops(cfg))
                moved += this->hoist(cfg, loop);

            if (moved == 0)
                break;

            hoisted += moved;
        }

        return hoisted;
    }

    size_t LICMPass::hoist(const ControlFlowGraph& cfg, const NaturalLoop& loop) {
        
        
        
        
        
        if (loop.entries.size() != 1 || loop.exits.size() != 1)
            return 0;

        const auto& entry = cfg.blocks()[loop.entries[0]];

        if (entry.begin >= entry.end)
            return 0;

        if (entry.successors.size() != 1 || entry.successors[0] != loop.header)
            return 0;

        const int exitBlock = loop.exits[0];
        for (const int predecessor : cfg.blocks()[exitBlock].predecessors)
            if (!loop.contains(predecessor))
                return 0;

        for (const int block : loop.blocks) {
            if (block == loop.header)
                continue;

            for (const int predecessor : cfg.blocks()[block].predecessors)
                if (!loop.contains(predecessor))
                    return 0;
        }

        
        
        
        std::unordered_set<int> loopWritten;
        for (const int block : loop.blocks) {
            const auto& b = cfg.blocks()[block];
            for (int i = b.begin; i < b.end; ++i) {
                const MirInstr& instr = mChunk.code[i];
                if (instr.op == MirOp::STORE_SLOT || instr.op == MirOp::STORE_VAR)
                    loopWritten.insert(instr.operand);
            }
        }

        
        
        
        
        
        
        
        
        int latch = -1;
        for (const int block : loop.blocks) {
            for (const int s : cfg.blocks()[block].successors)
                if (s == loop.header) { latch = block; break; }
            if (latch >= 0) break;
        }
        if (latch < 0 || latch == loop.header)
            return 0;

        std::vector<int> loopBlockIndex(cfg.size(), -1);
        std::vector<int> loopBlockList;
        for (const int block : loop.blocks) {
            loopBlockIndex[block] = static_cast<int>(loopBlockList.size());
            loopBlockList.push_back(block);
        }
        const int lc = static_cast<int>(loopBlockList.size());
        std::vector<std::vector<bool>> dom(lc, std::vector<bool>(lc, true));
        for (int j = 0; j < lc; ++j)
            dom[loopBlockIndex[loop.header]][j] = (j == loopBlockIndex[loop.header]);

        bool domChanged = true;
        while (domChanged) {
            domChanged = false;
            for (const int block : loop.blocks) {
                if (block == loop.header) continue;
                const int bi = loopBlockIndex[block];
                std::vector<bool> inter(lc, true);
                bool firstPred = true;
                for (const int p : cfg.blocks()[block].predecessors) {
                    if (loopBlockIndex[p] < 0) continue;
                    const int pi = loopBlockIndex[p];
                    if (firstPred) { inter = dom[pi]; firstPred = false; }
                    else { for (int j = 0; j < lc; ++j) inter[j] = inter[j] && dom[pi][j]; }
                }
                inter[bi] = true;
                if (inter != dom[bi]) { dom[bi] = inter; domChanged = true; }
            }
        }

        auto dominates = [&](int a, int b) {
            const int ai = loopBlockIndex[a], bi = loopBlockIndex[b];
            if (ai < 0 || bi < 0) return false;
            return static_cast<bool>(dom[bi][ai]);
        };

        std::unordered_set<int> safeBlocks;
        {
            int start = -1;
            for (int s : cfg.blocks()[loop.header].successors)
                if (loop.contains(s)) { start = s; break; }

            int cur = start;
            while (cur >= 0) {
                const auto& cb = cfg.blocks()[cur];
                if (cb.successors.size() != 1 || cb.predecessors.size() != 1)
                    break;

                const int pred = cb.predecessors[0];
                const bool predOk = (cur == start) ? (pred == loop.header)
                                                   : safeBlocks.count(pred) > 0;
                if (!predOk)
                    break;

                if (!dominates(cur, latch))
                    break;

                safeBlocks.insert(cur);

                const int next = cb.successors[0];
                if (next == loop.header)
                    break;
                cur = next;
            }
        }

        
        
        
        
        std::vector<MirInstr> invariant;
        std::vector<int> hoistOrigIdx;
        std::unordered_set<int> invariantRegs;

        for (const int block : safeBlocks) {
            const auto& b = cfg.blocks()[block];
            for (int i = b.begin; i < b.end; ++i) {
                const MirInstr& instr = mChunk.code[i];

                if (!isInvariant(instr, invariantRegs, loopWritten))
                    break;

                invariant.push_back(instr);
                hoistOrigIdx.push_back(i);
                if (instr.dst >= 0)
                    invariantRegs.insert(instr.dst);
            }
        }

        if (invariant.empty())
            return 0;

        
        
        
        
        const int insertAt = mChunk.code[entry.end - 1].op == MirOp::JMP
            ? entry.end - 1 : entry.end;

        std::vector<Edit> edits;
        edits.push_back({ insertAt, false, invariant });
        for (const int idx : hoistOrigIdx)
            edits.push_back({ idx, true, {} });

        apply(mChunk.code, edits);

        return 1;
    }

    void LICMPass::apply(std::vector<MirInstr>& code, const std::vector<Edit>& edits) {
        const int last = static_cast<int>(code.size()) - 1;

        std::vector<MirInstr> rewritten;
        std::vector<int> oldToNew(code.size(), -1);
        std::vector<int> origin;
        std::vector<std::vector<int>> placed(edits.size());

        for (size_t i = 0; i <= code.size(); ++i) {
            bool erased = false;

            for (size_t e = 0; e < edits.size(); ++e) {
                if (edits[e].at != static_cast<int>(i))
                    continue;

                for (const MirInstr& instr : edits[e].insert) {
                    placed[e].push_back(static_cast<int>(rewritten.size()));
                    rewritten.push_back(instr);
                    origin.push_back(-1);
                }

                if (edits[e].erase)
                    erased = true;
            }

            if (i == code.size())
                break;

            if (erased)
                continue;

            oldToNew[i] = static_cast<int>(rewritten.size());
            origin.push_back(static_cast<int>(i));
            rewritten.push_back(code[i]);
        }

        for (size_t n = 0; n < rewritten.size(); ++n) {
            if (origin[n] < 0 || !isJump(rewritten[n].op))
                continue;

            const int oldTarget = origin[n] + 1 + rewritten[n].operand;

            int newTarget = oldToNew[oldTarget];
            for (int j = oldTarget; j <= last && newTarget < 0; ++j)
                newTarget = oldToNew[j];

            if (newTarget < 0)
                newTarget = static_cast<int>(rewritten.size());

            rewritten[n].operand = newTarget - static_cast<int>(n) - 1;
        }

        code = std::move(rewritten);
    }
}
