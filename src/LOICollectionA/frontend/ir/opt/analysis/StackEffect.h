#pragma once

#include "LOICollectionA/frontend/ir/Mir.h"

namespace LOICollection::frontend::ir::opt {
    struct StackEffect {
        int pops = 0;
        int pushes = 0;
        int peeks = 0;

        int net() const { return pushes - pops; }

        int reach() const { return pops + peeks; }
    };

    bool stackEffectOf(const MirInstr& instr, const MirChunk& chunk, StackEffect& out);
}
