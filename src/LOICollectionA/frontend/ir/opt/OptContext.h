#pragma once

#include <vector>

#include "LOICollectionA/frontend/ir/Mir.h"

#include "LOICollectionA/frontend/ir/Optimizer.h"

namespace LOICollection::frontend::ir::opt {
    // Scratch state shared by the optimizer passes while transforming a chunk.
    // The passes build `foldedCode` from the original `code`, recording how
    // each emitted instruction maps back to its source (`newToOld`/`oldToNew`)
    // and which emitted instructions are dead (`dropped`). DeadCodePass
    // compacts the result and remaps jump offsets.
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
