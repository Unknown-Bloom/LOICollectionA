#include <algorithm>

#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"

#include "LOICollectionA/frontend/ir/opt/analysis/ControlFlowGraph.h"

namespace LOICollection::frontend::ir::opt {
    namespace {
        int jumpTarget(const Instruction& instr, int at) {
            return at + 1 + instr.operand;
        }
    }

    std::vector<int> ControlFlowGraph::leaders(const std::vector<Instruction>& code) {
        const int size = static_cast<int>(code.size());

        std::vector<bool> isLeader(size + 1, false);
        isLeader[0] = true;

        for (int i = 0; i < size; ++i) {
            if (isJump(code[i].op)) {
                const int target = jumpTarget(code[i], i);

                if (target >= 0 && target <= size)
                    isLeader[target] = true;

                isLeader[i + 1] = true;
            }

            if (isTerminator(code[i].op))
                isLeader[i + 1] = true;
        }

        std::vector<int> out;
        for (int i = 0; i <= size; ++i)
            if (isLeader[i])
                out.push_back(i);

        return out;
    }

    ControlFlowGraph::ControlFlowGraph(const std::vector<Instruction>& code) {
        mBlockOfIndex.assign(code.size() + 1, 0);

        const std::vector<int> starts = leaders(code);

        for (size_t k = 0; k < starts.size(); ++k) {
            Block block;
            block.begin = starts[k];
            block.end = k + 1 < starts.size() ? starts[k + 1] : static_cast<int>(code.size());

            mBlocks.push_back(std::move(block));
        }

        for (size_t b = 0; b < mBlocks.size(); ++b)
            for (int i = mBlocks[b].begin; i < mBlocks[b].end; ++i)
                mBlockOfIndex[i] = static_cast<int>(b);

        if (!code.empty())
            mBlockOfIndex[code.size()] = static_cast<int>(mBlocks.size()) - 1;

        this->link(code);
    }

    int ControlFlowGraph::blockOf(int index) const {
        if (index < 0 || index >= static_cast<int>(mBlockOfIndex.size()))
            return -1;

        return mBlockOfIndex[index];
    }

    void ControlFlowGraph::link(const std::vector<Instruction>& code) {
        for (size_t b = 0; b < mBlocks.size(); ++b) {
            Block& block = mBlocks[b];

            if (block.begin >= block.end)
                continue;

            const int last = block.end - 1;
            const Instruction& term = code[last];

            auto addEdge = [&](int target) {
                const int successor = this->blockOf(target);

                if (successor < 0 || successor == static_cast<int>(b))
                    return;

                if (std::ranges::find(block.successors, successor) == block.successors.end())
                    block.successors.push_back(successor);
            };

            if (isJump(term.op)) {
                const int target = jumpTarget(term, last);

                if (target >= 0 && target <= static_cast<int>(code.size()))
                    addEdge(target);
                else
                    addEdge(last + 1);

                if (term.op != OpCode::JMP)
                    addEdge(last + 1);
            } else if (!isTerminator(term.op)) {
                addEdge(last + 1);
            }
        }

        for (size_t b = 0; b < mBlocks.size(); ++b)
            for (const int successor : mBlocks[b].successors)
                mBlocks[successor].predecessors.push_back(static_cast<int>(b));
    }
}
