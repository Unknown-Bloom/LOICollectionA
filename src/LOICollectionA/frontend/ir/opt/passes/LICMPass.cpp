#include <algorithm>

#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"
#include "LOICollectionA/frontend/ir/opt/analysis/StackEffect.h"

#include "LOICollectionA/frontend/ir/opt/passes/LICMPass.h"

namespace LOICollection::frontend::ir::opt {
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

    std::vector<bool> LICMPass::writtenSlots(const ControlFlowGraph& cfg, const NaturalLoop& loop) const {
        const int slots = mChunk.slotCount > 0 ? mChunk.slotCount : 0;

        std::vector<bool> written(slots, false);

        for (const int block : loop.blocks) {
            const auto& info = cfg.blocks()[block];

            for (int i = info.begin; i < info.end; ++i) {
                const Instruction& instr = mChunk.code[i];

                if (instr.op != OpCode::STORE_SLOT && instr.op != OpCode::DUP_STORE_SLOT)
                    continue;

                if (instr.operand >= 0 && instr.operand < slots)
                    written[instr.operand] = true;
            }
        }

        return written;
    }

    bool LICMPass::isHoistable(const Instruction& instr, const std::vector<bool>& written) const {
        switch (instr.op) {
            case OpCode::PUSH_INT:
            case OpCode::PUSH_FLOAT:
            case OpCode::PUSH_STR:
            case OpCode::PUSH_BOOL:
            case OpCode::PUSH_NONE:
            case OpCode::ADD:
            case OpCode::SUB:
            case OpCode::MUL:
            case OpCode::DIV:
            case OpCode::MOD:
            case OpCode::POW:
            case OpCode::CMP_EQ:
            case OpCode::CMP_NE:
            case OpCode::CMP_GT:
            case OpCode::CMP_LT:
            case OpCode::CMP_GE:
            case OpCode::CMP_LE:
            case OpCode::LOGIC_AND:
            case OpCode::LOGIC_OR:
            case OpCode::NEG:
            case OpCode::NOT:
            case OpCode::UNWRAP:
            case OpCode::TYPE_OF:
            case OpCode::HAS_VALUE:
            case OpCode::IS_NONE:
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
            case OpCode::NEG_I:
                return true;

            case OpCode::LOAD_SLOT:
                return instr.operand >= 0 && instr.operand < static_cast<int>(written.size()) &&
                    !written[instr.operand];

            default:
                return false;
        }
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

        const std::vector<bool> written = this->writtenSlots(cfg, loop);
        const auto& header = cfg.blocks()[loop.header];

        std::vector<Instruction> invariant;
        int depth = 0;
        int net = 0;

        for (int i = header.begin; i < header.end; ++i) {
            StackEffect effect;
            if (!stackEffectOf(mChunk.code[i], mChunk, effect))
                break;
            if (!this->isHoistable(mChunk.code[i], written))
                break;
            if (depth < effect.reach())
                break;

            invariant.push_back(mChunk.code[i]);
            depth += effect.net();
            net += effect.net();
        }

        if (invariant.empty() || net != 1)
            return 0;

        const int insertAt = mChunk.code[entry.end - 1].op == OpCode::JMP ? entry.end - 1 : entry.end;

        const int exitBegin = cfg.blocks()[exitBlock].begin;

        std::vector<Edit> edits;

        edits.push_back({ insertAt, false, -1, invariant });

        edits.push_back({ header.begin, true, 0, { Instruction{ OpCode::DUP, 0, invariant.front().loc } } });
        for (int k = 1; k < static_cast<int>(invariant.size()); ++k)
            edits.push_back({ header.begin + k, true, -1, {} });

        edits.push_back({ exitBegin, false, 0, { Instruction{ OpCode::POP, 0, {} } } });

        apply(mChunk.code, edits);

        return 1;
    }

    void LICMPass::apply(std::vector<Instruction>& code, const std::vector<Edit>& edits) {
        const int last = static_cast<int>(code.size()) - 1;

        std::vector<Instruction> rewritten;
        std::vector<int> oldToNew(code.size(), -1);
        std::vector<int> origin;
        std::vector<std::vector<int>> placed(edits.size());

        for (size_t i = 0; i <= code.size(); ++i) {
            int owner = -1;
            bool erased = false;

            for (size_t e = 0; e < edits.size(); ++e) {
                if (edits[e].at != static_cast<int>(i))
                    continue;

                for (const Instruction& instr : edits[e].insert) {
                    placed[e].push_back(static_cast<int>(rewritten.size()));
                    rewritten.push_back(instr);
                    origin.push_back(-1);
                }

                if (edits[e].retarget >= 0 && owner < 0)
                    owner = static_cast<int>(e);

                if (edits[e].erase)
                    erased = true;
            }

            if (i == code.size())
                break;

            const bool redirected = owner >= 0 &&
                edits[owner].retarget < static_cast<int>(placed[owner].size());

            if (redirected)
                oldToNew[i] = placed[owner][edits[owner].retarget];

            if (erased)
                continue;

            if (!redirected)
                oldToNew[i] = static_cast<int>(rewritten.size());

            origin.push_back(static_cast<int>(i));
            rewritten.push_back(code[i]);
        }

        for (size_t n = 0; n < rewritten.size(); ++n) {
            if (origin[n] < 0 || !isJump(rewritten[n].op))
                continue;

            const int oldTarget = std::clamp(origin[n] + 1 + rewritten[n].operand, 0, last);

            int newTarget = oldToNew[oldTarget];
            if (newTarget < 0) {
                int j = oldTarget;
                while (j <= last && oldToNew[j] < 0)
                    ++j;

                newTarget = j > last ? static_cast<int>(rewritten.size()) : oldToNew[j];
            }

            rewritten[n].operand = newTarget - static_cast<int>(n) - 1;
        }

        code = std::move(rewritten);
    }
}
