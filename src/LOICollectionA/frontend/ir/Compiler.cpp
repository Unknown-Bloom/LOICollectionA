#include <utility>

#include "LOICollectionA/frontend/ir/Compiler.h"

#include "LOICollectionA/frontend/ir/LirLowering.h"
#include "LOICollectionA/frontend/ir/LirVerifier.h"
#include "LOICollectionA/frontend/ir/hir/HirBuilder.h"
#include "LOICollectionA/frontend/ir/hir/HirVerifier.h"
#include "LOICollectionA/frontend/ir/mir/MirBuilder.h"
#include "LOICollectionA/frontend/ir/mir/MirPass.h"
#include "LOICollectionA/frontend/ir/mir/MirVerifier.h"

namespace LOICollection::frontend::ir {
    Compiler::Compiler(DiagnosticEngine& diag) : diagnostics(diag) {}

    Chunk Compiler::compile(ASTNode& root) {
        hir::Program program = this->buildHir(root);
        if (this->diagnostics.hasErrors())
            return {};

        mir::Module module = this->buildMir(program);
        if (this->diagnostics.hasErrors())
            return {};

        return this->lowerLir(program, module);
    }

    hir::Program Compiler::buildHir(ASTNode& root) {
        hir::HirBuilder builder(this->diagnostics);
        hir::Program program = builder.build(root);

        if (this->diagnostics.hasErrors())
            return program;

        static_cast<void>(hir::HirVerifier::verify(program, this->diagnostics));
        return program;
    }

    mir::Module Compiler::buildMir(const hir::Program& program) {
        mir::MirBuilder builder(program, this->diagnostics);
        mir::Module module = builder.build();

        if (this->diagnostics.hasErrors())
            return module;

        if (!mir::MirVerifier::verify(module, this->diagnostics))
            return module;

        mir::MirPass::run(module);
        return module;
    }

    Chunk Compiler::lowerLir(const hir::Program& program, const mir::Module& module) {
        LirLowering lowering(program, module, this->diagnostics);
        Chunk chunk = lowering.lower();

        if (this->diagnostics.hasErrors())
            return chunk;

        static_cast<void>(LirVerifier::verify(chunk, this->diagnostics));
        return chunk;
    }
}
