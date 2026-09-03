#pragma once

#include "LOICollectionA/frontend/ir/ByteCode.h"

namespace LOICollection::frontend::ir::opt {
    struct StackEffect {
        int pops = 0;
        int pushes = 0;
        int peeks = 0;

        int net() const { return pushes - pops; }

        int reach() const { return pops + peeks; }
    };

    bool stackEffectOf(const Instruction& instr, const BytecodeChunk& chunk, StackEffect& out);
}
