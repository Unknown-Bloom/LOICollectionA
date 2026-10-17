#include <algorithm>
#include <vector>

#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"
#include "LOICollectionA/frontend/ir/opt/passes/BranchFusionPass.h"

namespace LOICollection::frontend::ir::opt::detail {
    bool isComparison(MirOp op) {
        switch (op) {
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
}

namespace LOICollection::frontend::ir::opt {
    using namespace detail;

    std::size_t BranchFusionPass::run(bool enabled) {
        if (!enabled || this->mChunk.code.size() < 2)
            return 0;

        const std::size_t slots = this->mChunk.slotCount < 0
            ? 0
            : static_cast<std::size_t>(this->mChunk.slotCount);

        std::vector<std::size_t> reads(slots, 0);
        for (const auto& instr : this->mChunk.code) {
            const auto read = [&reads](int reg) {
                if (reg >= 0 && static_cast<std::size_t>(reg) < reads.size())
                    ++reads[static_cast<std::size_t>(reg)];
            };

            read(instr.src1);
            read(instr.src2);
            read(instr.src3);

            if (instr.op == MirOp::LOAD_SLOT)
                read(instr.operand);
        }

        std::vector<bool> dropped(this->mChunk.code.size(), false);
        std::size_t fused = 0;

        for (std::size_t i = 1; i < this->mChunk.code.size(); ++i) {
            MirInstr& branch = this->mChunk.code[i];
            if (branch.op != MirOp::JMP_IF_FALSE || this->mJumps.isTarget(static_cast<int>(i)))
                continue;

            MirInstr& compare = this->mChunk.code[i - 1];
            if (!isComparison(compare.op) || compare.dst < 0 || compare.dst != branch.src1)
                continue;

            const std::size_t result = static_cast<std::size_t>(compare.dst);
            if (result >= reads.size() || reads[result] != 1)
                continue;

            branch.op = MirOp::JMP_IF_CMP_FALSE;
            branch.src1 = compare.src1;
            branch.src2 = compare.src2;
            branch.imm = static_cast<int>(compare.op);
            branch.type = compare.type;

            dropped[i - 1] = true;
            ++fused;
        }

        if (fused != 0)
            this->compact(dropped);

        return fused;
    }

    void BranchFusionPass::compact(const std::vector<bool>& dropped) {
        const std::size_t size = this->mChunk.code.size();

        std::vector<int> remap(size, -1);
        int next = 0;
        for (std::size_t i = 0; i < size; ++i)
            if (!dropped[i])
                remap[i] = next++;

        std::vector<MirInstr> code;
        std::vector<int> origin;
        code.reserve(static_cast<std::size_t>(next));
        origin.reserve(static_cast<std::size_t>(next));

        for (std::size_t i = 0; i < size; ++i) {
            if (dropped[i])
                continue;

            code.push_back(this->mChunk.code[i]);
            origin.push_back(static_cast<int>(i));
        }

        for (std::size_t i = 0; i < code.size(); ++i) {
            MirInstr& instr = code[i];
            if (!isJump(instr.op))
                continue;

            int target = origin[i] + 1 + instr.operand;
            if (target < 0)
                continue;

            if (static_cast<std::size_t>(target) >= size) {
                instr.operand = static_cast<int>(code.size()) - static_cast<int>(i) - 1;
                continue;
            }

            while (static_cast<std::size_t>(target) < size && remap[static_cast<std::size_t>(target)] < 0)
                ++target;

            const int resolved = static_cast<std::size_t>(target) >= size
                ? static_cast<int>(code.size())
                : remap[static_cast<std::size_t>(target)];

            instr.operand = resolved - static_cast<int>(i) - 1;
        }

        this->mChunk.code = std::move(code);
    }
}
