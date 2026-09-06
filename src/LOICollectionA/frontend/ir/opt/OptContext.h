#pragma once

#include <vector>

#include "LOICollectionA/frontend/ir/Mir.h"

#include "LOICollectionA/frontend/ir/Optimizer.h"

namespace LOICollection::frontend::ir::opt {
    class OptContext {
    public:
        explicit OptContext(size_t codeSize)
        : oldToNew(codeSize, -1),
          dropped(codeSize, false) {}

        std::vector<MirInstr> foldedCode;
        std::vector<int> newToOld;
        std::vector<int> oldToNew;
        std::vector<bool> dropped;
        Optimizer::Stats stats;

        int emit(const MirInstr& instr) {
            int at = static_cast<int>(foldedCode.size());
            foldedCode.push_back(instr);
            return at;
        }

        void drop(int producer) { dropped[producer] = true; }
    };
}
