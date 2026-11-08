#pragma once

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/DiagnosticEngine.h"

#include "LOICollectionA/frontend/ir/mir/Mir.h"

namespace LOICollection::frontend::ir::mir {
    class MirVerifier {
    public:
        LOICOLLECTION_A_NDAPI static bool verify(const Module& module, DiagnosticEngine& diagnostics);
    };
}
