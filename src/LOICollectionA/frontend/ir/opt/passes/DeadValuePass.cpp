#include <unordered_set>
#include <vector>

#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"

#include "LOICollectionA/frontend/ir/opt/passes/DeadValuePass.h"

namespace LOICollection::frontend::ir::opt {
    size_t DeadValuePass::run() {
        if (mChunk.code.empty())
            return 0;

        std::unordered_set<int> read;
        std::unordered_set<int> pinned;

        for (const MirInstr& instr : mChunk.code) {
            if (instr.src1 >= 0)
                read.insert(instr.src1);
            if (instr.src2 >= 0)
                read.insert(instr.src2);
            if (instr.src3 >= 0)
                read.insert(instr.src3);

            if (instr.imm > 0 && instr.src1 >= 0) {
                for (int i = 0; i < instr.imm; ++i)
                    read.insert(instr.src1 + i);
            }

            if (instr.op == MirOp::STORE_SLOT || instr.op == MirOp::LOAD_SLOT)
                pinned.insert(instr.operand);

            if (instr.op == MirOp::HALT)
                pinned.insert(0);
        }

        std::vector<MirInstr> out;
        out.reserve(mChunk.code.size());
        std::vector<int> oldToNew(mChunk.code.size(), -1);
        std::vector<int> origin;
        size_t eliminated = 0;

        for (size_t i = 0; i < mChunk.code.size(); ++i) {
            const MirInstr& instr = mChunk.code[i];

            if (instr.op == MirOp::LOAD_CONST && instr.dst >= 0 &&
                read.find(instr.dst) == read.end() &&
                pinned.find(instr.dst) == pinned.end()) {
                ++eliminated;
                continue;
            }

            out.push_back(instr);
            oldToNew[i] = static_cast<int>(out.size()) - 1;
            origin.push_back(static_cast<int>(i));
        }

        if (eliminated == 0)
            return 0;

        for (size_t k = 0; k < out.size(); ++k) {
            if (!isJump(out[k].op))
                continue;

            const int oldTarget = origin[k] + 1 + out[k].operand;

            int newTarget = oldToNew[oldTarget];
            for (int j = oldTarget; j < static_cast<int>(oldToNew.size()) && newTarget < 0; ++j)
                newTarget = oldToNew[j];

            if (newTarget < 0)
                newTarget = static_cast<int>(out.size());

            out[k].operand = newTarget - static_cast<int>(k) - 1;
        }

        mChunk.code = std::move(out);

        return eliminated;
    }
}
