#pragma once

#include <vector>

#include "LOICollectionA/frontend/ir/Lir.h"

namespace LOICollection::frontend::ir::opt {
    class ControlFlowGraph {
    public:
        struct Block {
            int begin = 0;
            int end = 0;
            std::vector<int> successors;
            std::vector<int> predecessors;
        };

        explicit ControlFlowGraph(const std::vector<Instr>& code);

        const std::vector<Block>& blocks() const { return mBlocks; }

        int blockOf(int index) const;

        int size() const { return static_cast<int>(mBlocks.size()); }

    private:
        static std::vector<int> leaders(const std::vector<Instr>& code);

        void link(const std::vector<Instr>& code);

        std::vector<Block> mBlocks;
        std::vector<int> mBlockOfIndex;
    };
}
