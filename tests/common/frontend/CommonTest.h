#pragma once

#include <string>
#include <memory>
#include <stdexcept>

#include "LOICollectionA/frontend/ComponentExpander.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/Lexer.h"
#include "LOICollectionA/frontend/Parser.h"
#include "LOICollectionA/frontend/SemanticAnalyzer.h"
#include "LOICollectionA/frontend/ir/Compiler.h"
#include "LOICollectionA/frontend/ir/MirLowering.h"
#include "LOICollectionA/frontend/ir/Optimizer.h"
#include "LOICollectionA/frontend/ir/VM.h"

namespace LOICollection::frontend {
    inline std::shared_ptr<ir::BytecodeChunk> compile(const std::string& input, DiagnosticEngine& diagnostics) {
        Lexer lexer(input, diagnostics);
        Parser parser(lexer, diagnostics);

        auto ast = parser.parse();
        if (diagnostics.hasErrors())
            return nullptr;

        if (ast && ast->getType() == ASTNode::Type::Program) {
            if (!ComponentExpander::expand(static_cast<ProgramNode&>(*ast), diagnostics))
                return nullptr;
            if (diagnostics.hasErrors())
                return nullptr;

            SemanticAnalyzer analyzer(diagnostics);
            analyzer.analyze(static_cast<ProgramNode&>(*ast));
            if (diagnostics.hasErrors())
                return nullptr;
        }

        ir::Compiler compiler(diagnostics);
        auto mir = std::make_shared<ir::MirChunk>(compiler.compile(*ast));
        if (diagnostics.hasErrors())
            return nullptr;

        ir::Optimizer optimizer;
        optimizer.optimize(*mir);

        return std::make_shared<ir::BytecodeChunk>(ir::MirLowering::lower(*mir));
    }

    inline std::string eval(const std::string& input, const Context& ctx = {}) {
        DiagnosticEngine diagnostics;

        Lexer lexer(input, diagnostics);
        Parser parser(lexer, diagnostics);

        auto ast = parser.parse();
        if (diagnostics.hasErrors())
            throw std::runtime_error(diagnostics.getErrorMessage());

        if (ast->getType() == ASTNode::Type::Program) {
            if (!ComponentExpander::expand(static_cast<ProgramNode&>(*ast), diagnostics))
                throw std::runtime_error(diagnostics.getErrorMessage());
        }

        if (ast->getType() == ASTNode::Type::Program) {
            SemanticAnalyzer analyzer(diagnostics);
            analyzer.analyze(static_cast<ProgramNode&>(*ast));
        }

        if (diagnostics.hasErrors())
            throw std::runtime_error(diagnostics.getErrorMessage());

        ir::Compiler compiler(diagnostics);
        ir::VM vm(diagnostics);

        auto mir = std::make_shared<ir::MirChunk>(compiler.compile(*ast));
        if (diagnostics.hasErrors())
            throw std::runtime_error(diagnostics.getErrorMessage());

        ir::Optimizer optimizer;
        [[maybe_unused]] auto optimizeStats = optimizer.optimize(*mir);

        auto bytecode = std::make_shared<ir::BytecodeChunk>(ir::MirLowering::lower(*mir));
        auto result = vm.run(bytecode, ctx);
        if (diagnostics.hasErrors())
            throw std::runtime_error(diagnostics.getErrorMessage());

        return ir::VM::valueToString(result);
    }
}
