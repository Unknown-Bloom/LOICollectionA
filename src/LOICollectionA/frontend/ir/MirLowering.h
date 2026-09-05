#pragma once

#include "LOICollectionA/frontend/ir/ByteCode.h"
#include "LOICollectionA/frontend/ir/Mir.h"

namespace LOICollection::frontend::ir {
    class MirLowering {
    public:
        static BytecodeChunk lower(const MirChunk& chunk);
    };
}
