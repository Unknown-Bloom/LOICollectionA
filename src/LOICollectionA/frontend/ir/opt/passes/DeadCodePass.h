#pragma once

#include "LOICollectionA/frontend/ir/Mir.h"

#include "LOICollectionA/frontend/ir/opt/OptContext.h"

namespace LOICollection::frontend::ir::opt {
    class DeadCodePass {
    public:
        void run(MirChunk& chunk, OptContext& ctx, bool eliminate);
    };
}
