#pragma once

#include <vector>

#include "LOICollectionA/frontend/ir/Mir.h"

namespace LOICollection::frontend::ir::opt {
    // Common subexpression elimination on the register-based MIR.
    //
    // Each value-producing instruction carries an explicit destination
    // register, so we can number values by the registers that hold them. When a
    // later instruction would recompute a value already live in another
    // register, it is replaced by a `MOVE` into that register. Tracking is reset
    // at jump targets and after any instruction whose side effects could change
    // a previously computed value.
    class CSEPass {
    public:
        explicit CSEPass(MirChunk& chunk)
        : mChunk(chunk) {}

        size_t run();

    private:
        MirChunk& mChunk;
    };
}
