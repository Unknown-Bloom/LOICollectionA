#pragma once

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/DiagnosticEngine.h"

#include "LOICollectionA/frontend/ir/hir/Hir.h"

namespace LOICollection::frontend::ir::hir {
    class HirVerifier {
    public:
        LOICOLLECTION_A_NDAPI static bool verify(const Program& program, DiagnosticEngine& diagnostics);
    };
}
