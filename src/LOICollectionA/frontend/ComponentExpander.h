#pragma once

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/AST.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"

namespace LOICollection::frontend {
    class ComponentExpander {
    public:
        LOICOLLECTION_A_NDAPI static bool expand(
            ProgramNode& program,
            DiagnosticEngine& diagnostics
        );

    private:
        ComponentExpander() = default;
    };
}
