#include <string>
#include <vector>

#include "LOICollectionA/frontend/AST.h"
#include "LOICollectionA/frontend/ComponentExpander.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/Lexer.h"
#include "LOICollectionA/frontend/Parser.h"
#include "LOICollectionA/frontend/SemanticAnalyzer.h"

#include "LOICollectionA/frontend/lsp/Analysis.h"

namespace LOICollection::frontend::lsp {
    namespace {
        Range rangeOf(const SourceLocation& loc) {
            Range range;
            range.start.line = loc.line > 0 ? loc.line - 1 : 0;
            range.start.character = loc.column > 0 ? loc.column - 1 : 0;
            range.end.line = range.start.line;
            range.end.character = range.start.character + 1;

            return range;
        }

        std::string typeName(const TypeExpr& expr, const TypeInfo& fallback) {
            if (expr.name.empty()) {
                const std::string inferred = typeInfoToString(fallback);

                return inferred == "unknown" ? std::string{} : inferred;
            }

            if (expr.args.empty())
                return expr.name;

            std::string result = expr.name + "<";
            for (size_t i = 0; i < expr.args.size(); ++i) {
                if (i != 0)
                    result += ", ";
                result += expr.args[i].name;
            }

            return result + ">";
        }

        std::string signatureOf(const std::string& name, const MethodDecl& decl) {
            std::string result = name + "(";

            for (size_t i = 0; i < decl.params.size(); ++i) {
                if (i != 0)
                    result += ", ";

                const std::string type = typeName(decl.params[i].typeExpr,
                    i < decl.paramTypes.size() ? decl.paramTypes[i] : TypeInfo{});

                result += decl.params[i].name;
                if (!type.empty())
                    result += ": " + type;
            }

            result += ")";

            const std::string returnType = typeName(decl.returnTypeExpr, decl.returnType);
            if (!returnType.empty())
                result += " -> " + returnType;

            return result;
        }

        void pushSymbol(std::vector<Symbol>& out, std::string name, SymbolKind kind,
            std::string detail, const SourceLocation& loc, std::string container) {
            out.push_back({
                std::move(name), kind, std::move(detail), rangeOf(loc), std::move(container)
            });
        }

        void collectClass(ClassNode& cls, std::vector<Symbol>& out) {
            pushSymbol(out, cls.name, SymbolKind::Class, "class " + cls.name, cls.loc, "");

            for (const auto& member : cls.members)
                pushSymbol(out, member.name,
                    member.isStatic ? SymbolKind::StaticField : SymbolKind::Field,
                    typeName(member.typeExpr, member.type), member.loc, cls.name);

            for (const auto& method : cls.methods)
                pushSymbol(out, method.name,
                    method.isStatic ? SymbolKind::StaticMethod : SymbolKind::Method,
                    signatureOf(method.name, method), method.loc, cls.name);
        }

        void collectSequence(SequenceNode& node, std::vector<Symbol>& out) {
            for (const auto& part : node.parts) {
                if (!part)
                    continue;

                switch (part->getType()) {
                    case ASTNode::Type::Class:
                        collectClass(static_cast<ClassNode&>(*part), out);
                        break;
                    case ASTNode::Type::Block:
                        collectSequence(static_cast<BlockNode&>(*part), out);
                        break;
                    case ASTNode::Type::FunctionDef: {
                        auto& def = static_cast<FunctionDefNode&>(*part);
                        pushSymbol(out, def.name, SymbolKind::Function,
                            signatureOf(def.name, def.decl), def.loc, "");
                        break;
                    }
                    case ASTNode::Type::Trait: {
                        auto& trait = static_cast<TraitNode&>(*part);
                        pushSymbol(out, trait.name, SymbolKind::Interface,
                            "trait " + trait.name, trait.loc, "");

                        for (const auto& method : trait.methods)
                            pushSymbol(out, method.name, SymbolKind::Method,
                                signatureOf(method.name, method), method.loc, trait.name);
                        break;
                    }
                    case ASTNode::Type::Impl: {
                        auto& impl = static_cast<ImplNode&>(*part);

                        for (const auto& method : impl.methods)
                            pushSymbol(out, method.name,
                                method.isStatic ? SymbolKind::StaticMethod : SymbolKind::Method,
                                signatureOf(method.name, method), method.loc, impl.target.name);

                        for (const auto& constant : impl.consts)
                            pushSymbol(out, constant.name, SymbolKind::Constant,
                                typeName(constant.typeExpr, constant.type), constant.loc,
                                impl.target.name);
                        break;
                    }
                    case ASTNode::Type::Assignment: {
                        auto& assign = static_cast<AssignmentNode&>(*part);
                        if (!assign.isDeclaration || !assign.target)
                            break;

                        const auto* variable = dynamic_cast<const VariableNode*>(assign.target.get());
                        if (!variable)
                            break;

                        pushSymbol(out, variable->name, SymbolKind::Variable,
                            typeName(assign.declaredType, assign.target->type), assign.loc, "");
                        break;
                    }
                    default:
                        break;
                }
            }
        }
    }

    DocumentAnalysis analyze(const std::string& text) {
        DocumentAnalysis result;

        DiagnosticEngine engine;
        Lexer lexer(text, engine);
        Parser parser(lexer, engine);

        auto ast = parser.parse();

        ProgramNode* program = nullptr;
        if (ast && ast->getType() == ASTNode::Type::Program) {
            program = static_cast<ProgramNode*>(ast.get());

            if (ComponentExpander::expand(*program, engine)) {
                SemanticAnalyzer analyzer(engine);
                analyzer.analyze(*program);
            }
        }

        for (const auto& diagnostic : engine.all()) {
            Diagnostic converted;
            converted.range = rangeOf(diagnostic.loc);
            converted.message = diagnostic.message;

            switch (diagnostic.level) {
                case DiagnosticLevel::Error:
                    converted.severity = Severity::Error;
                    break;
                case DiagnosticLevel::Warning:
                    converted.severity = Severity::Warning;
                    break;
                default:
                    converted.severity = Severity::Information;
                    break;
            }

            result.diagnostics.push_back(std::move(converted));
        }

        if (program)
            collectSequence(*program, result.symbols);

        return result;
    }
}
