#pragma once

#include "LOICollectionA/frontend/ir/ByteCode.h"

#include "LOICollectionA/frontend/ir/opt/OptContext.h"

namespace LOICollection::frontend::ir::opt {
    class DeadCodePass {
    public:
        void run(BytecodeChunk& chunk, OptContext& ctx, bool eliminate);
    };
}
