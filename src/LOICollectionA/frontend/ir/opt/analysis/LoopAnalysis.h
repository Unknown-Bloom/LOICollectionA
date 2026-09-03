#pragma once

#include <vector>

#include "LOICollectionA/frontend/ir/opt/analysis/ControlFlowGraph.h"

namespace LOICollection::frontend::ir::opt {
    struct NaturalLoop {
        int header = -1;
        std::vector<int> blocks;
        std::vector<int> exits;
        std::vector<int> entries;

        bool contains(int block) const;
    };

    std::vector<NaturalLoop> findNaturalLoops(const ControlFlowGraph& cfg);
}
