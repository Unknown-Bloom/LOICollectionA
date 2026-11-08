#pragma once

#include "LOICollectionA/frontend/ir/Lir.h"

#include "LOICollectionA/frontend/ir/opt/OptContext.h"

namespace LOICollection::frontend::ir::opt {
    class DeadCodePass {
    public:
        void run(Chunk& chunk, OptContext& ctx, bool eliminate);
    };
}
