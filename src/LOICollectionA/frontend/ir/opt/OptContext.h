#pragma once

#include <vector>

#include "LOICollectionA/frontend/ir/Lir.h"

#include "LOICollectionA/frontend/ir/Optimizer.h"

namespace LOICollection::frontend::ir::opt {
    class OptContext {
    public:
        explicit OptContext(size_t codeSize)
        : oldToNew(codeSize, -1),
          dropped(codeSize, false) {}

        std::vector<Instr> foldedCode;
        std::vector<int> newToOld;
        std::vector<int> oldToNew;
        std::vector<bool> dropped;
        Optimizer::Stats stats;

        int emit(const Instr& instr) {
            int at = static_cast<int>(foldedCode.size());
            foldedCode.push_back(instr);
            return at;
        }

        void drop(int producer) { dropped[producer] = true; }
    };
}
