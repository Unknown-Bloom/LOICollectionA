#pragma once

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/DiagnosticEngine.h"

#include "LOICollectionA/frontend/ir/Lir.h"

namespace LOICollection::frontend::ir {
    class LirVerifier {
    public:
        LOICOLLECTION_A_NDAPI static bool verify(const Chunk& chunk, DiagnosticEngine& diagnostics);
    };
}
