#include <algorithm>
#include <unordered_set>

#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"

#include "LOICollectionA/frontend/ir/opt/passes/LICMPass.h"

namespace LOICollection::frontend::ir::opt {
    namespace {
        bool isPureBinary(MirOp op) {
            switch (op) {
                case MirOp::ADD: case MirOp::SUB: case MirOp::MUL: case MirOp::DIV:
                case MirOp::MOD: case MirOp::POW:
                case MirOp::ADD_I: case MirOp::SUB_I: case MirOp::MUL_I: case MirOp::MOD_I:
                case MirOp::CMP_EQ: case MirOp::CMP_NE: case MirOp::CMP_GT: case MirOp::CMP_LT:
                case MirOp::CMP_GE: case MirOp::CMP_LE:
                case MirOp::CMP_EQ_I: case MirOp::CMP_NE_I: case MirOp::CMP_GT_I: case MirOp::CMP_LT_I:
                case MirOp::CMP_GE_I: case MirOp::CMP_LE_I:
                case MirOp::LOGIC_AND: case MirOp::LOGIC_OR:
                    return true;
                default:
                    return false;
            }
        }

        bool isPureUnary(MirOp op) {
            switch (op) {
                case MirOp::NEG:
                case MirOp::NEG_I:
                case MirOp::NOT:
                case MirOp::INSTANCEOF:
                case MirOp::LOAD_LEN:
                    return true;
                default:
                    return false;
            }
        }

        bool isInvariant(const MirInstr& instr, const std::unordered_set<int>& invariantRegs) {
            if (instr.op == MirOp::LOAD_CONST)
                return true;

            if (isPureBinary(instr.op))
                return invariantRegs.count(instr.src1) > 0 && invariantRegs.count(instr.src2) > 0;

            if (isPureUnary(instr.op))
                return invariantRegs.count(instr.src1) > 0;

            return false;
        }
    }

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

        const auto& header = cfg.blocks()[loop.header];

        std::vector<MirInstr> invariant;
        std::unordered_set<int> invariantRegs;

        for (int i = header.begin; i < header.end; ++i) {
            const MirInstr& instr = mChunk.code[i];

            if (!isInvariant(instr, invariantRegs))
                break;

            invariant.push_back(instr);
            if (instr.dst >= 0)
                invariantRegs.insert(instr.dst);
        }

        if (invariant.empty())
            return 0;

        const int insertAt = mChunk.code[entry.end - 1].op == MirOp::JMP ? entry.end - 1 : entry.end;

        std::vector<Edit> edits;
        edits.push_back({ insertAt, false, invariant });
        for (int k = 0; k < static_cast<int>(invariant.size()); ++k)
            edits.push_back({ header.begin + k, true, {} });

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
            int owner = -1;
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
