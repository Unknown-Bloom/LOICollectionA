#pragma once

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/ir/mir/Mir.h"

namespace LOICollection::frontend::ir::mir {
    class MirPass {
    public:
        LOICOLLECTION_A_API static void removeUnreachable(Module& module);
        LOICOLLECTION_A_API static void run(Module& module);
    };
}
