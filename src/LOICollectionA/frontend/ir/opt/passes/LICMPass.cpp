#include <algorithm>
#include <unordered_set>

#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"

#include "LOICollectionA/frontend/ir/opt/passes/LICMPass.h"

namespace LOICollection::frontend::ir::opt::detail {
    // An instruction is loop-invariant when its value is identical on every
    // iteration. We treat:
    //   * LOAD_CONST   -> always invariant
    //   * LOAD_SLOT    -> invariant iff the slot is never written inside the loop
    //   * MOVE / pure unary / pure binary -> invariant iff every operand register
    //                                       already holds an invariant value
    // LOAD_VAR is deliberately NOT treated as memory-invariant: the MIR can
    // assign different variable indices to the read and write of one logical
    // variable (e.g. via closure capture), so a per-index "never written" check
    // would be unsound. Side-effecting ops (CALL, STORE, NEW, field/index
    // writes, ...) and DIV/MOD/POW (which can panic on zero) are also excluded so
    // that hoisting never changes observable behaviour or the timing of a panic.
    bool isInvariant(
        const MirInstr& instr,
        const std::unordered_set<int>& invariantRegs,
        const std::unordered_set<int>& loopWritten
    ) {
        if (instr.op == MirOp::LOAD_CONST)
            return true;

        // Only LOAD_SLOT may be treated as loop-invariant memory here: slot
        // indices are stable across reads and writes of the same local. LOAD_VAR
        // must be excluded -- the MIR can assign different variable indices to
        // the read and write of one logical variable (e.g. via closure capture),
        // so a per-index "never written" check is unsound and could hoist a load
        // of an undefined variable out of its defining loop.
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
        // Only handle well-formed single-entry / single-exit loops whose every
        // block is reached on every iteration (each non-header loop block has all
        // of its predecessors inside the loop). This guarantees that moving the
        // loop-invariant prefix into the preheader neither changes how often an
        // instruction executes nor skips an iteration.
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

        // Collect every memory location written inside the loop. A LOAD_SLOT /
        // LOAD_VAR of a location that is never written in the loop is loop
        // invariant and may be hoisted.
        std::unordered_set<int> loopWritten;
        for (const int block : loop.blocks) {
            const auto& b = cfg.blocks()[block];
            for (int i = b.begin; i < b.end; ++i) {
                const MirInstr& instr = mChunk.code[i];
                if (instr.op == MirOp::STORE_SLOT || instr.op == MirOp::STORE_VAR)
                    loopWritten.insert(instr.operand);
            }
        }

        // Only code that runs on *every* iteration may be hoisted out of a loop.
        // A block is safe when it is reached unconditionally from the header and
        // falls through unconditionally to the next safe block (or the back-edge),
        // with no internal branches or merges. This deliberately excludes blocks
        // behind a condition -- e.g. the arms of a match/if inside the loop --
        // whose leading instructions are only conditionally executed. Hoisting
        // those to the unconditional preheader would change which value a later
        // selection observes and would silently corrupt the program.
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

                safeBlocks.insert(cur);

                const int next = cb.successors[0];
                if (next == loop.header)
                    break;
                cur = next;
            }
        }

        // Walk each safe block in program order and collect its leading
        // loop-invariant prefix. Stopping at the first non-invariant instruction
        // keeps the transformation safe: nothing after it can be moved without
        // risking a dependence on a loop-varying value.
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

        // Insert the hoisted code at the end of the unique preheader (the loop's
        // single entry block), just before it falls into the loop header. If the
        // preheader ends with an unconditional jump, insert before it so the jump
        // still transfers control into the loop.
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
