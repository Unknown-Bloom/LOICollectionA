#pragma once

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/AST.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"

#include "LOICollectionA/frontend/ir/Lir.h"
#include "LOICollectionA/frontend/ir/hir/Hir.h"
#include "LOICollectionA/frontend/ir/mir/Mir.h"

namespace LOICollection::frontend::ir {
    class Compiler {
    public:
        LOICOLLECTION_A_API Compiler(DiagnosticEngine& diag);

        LOICOLLECTION_A_NDAPI MirChunk compile(ASTNode& root);
        LOICOLLECTION_A_NDAPI hir::Program buildHir(ASTNode& root);
        LOICOLLECTION_A_NDAPI mir::Module buildMir(const hir::Program& program);
        LOICOLLECTION_A_NDAPI Chunk lowerLir(const hir::Program& program, const mir::Module& module);

    private:
        DiagnosticEngine& diagnostics;
    };
}
