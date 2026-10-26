#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "LOICollectionA/frontend/AST.h"
#include "LOICollectionA/frontend/ComponentExpander.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/Lexer.h"
#include "LOICollectionA/frontend/Parser.h"
#include "LOICollectionA/frontend/SemanticAnalyzer.h"

#include "LOICollectionA/frontend/lsp/Analysis.h"

namespace LOICollection::frontend::lsp::detail {
    using BracketPair = std::pair<std::size_t, std::size_t>;

    struct BracketMap {
        std::vector<BracketPair> pairs;
    };

    struct Scope {
        std::size_t start = 0;
        std::size_t end = 0;
    };

    struct Collector {
        const std::string& text;
        const BracketMap& brackets;
        std::string uri;
        bool deep = true;
        std::vector<Symbol>& symbols;
        std::vector<InlayHint>& hints;
    };

    bool isNameStart(char c) {
        return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }

    bool isNameChar(char c) {
        return isNameStart(c) || (c >= '0' && c <= '9');
    }

    Range rangeOf(const SourceLocation& loc) {
        Range range;
        range.start.line = loc.line > 0 ? loc.line - 1 : 0;
        range.start.character = loc.column > 0 ? loc.column - 1 : 0;
        range.end.line = range.start.line;
        range.end.character = range.start.character + 1;

        return range;
    }

    Range rangeOf(const std::string& text, const SourceLocation& loc) {
        Range range = rangeOf(loc);
        std::size_t offset = std::min(loc.offset, text.size());

        if (offset >= text.size())
            return range;

        const char first = text[offset];

        if (isNameChar(first)) {
            std::size_t end = offset;
            while (end < text.size() && isNameChar(text[end]))
                ++end;

            range.end.character += end - offset - 1;
            return range;
        }

        if (first == '"' || first == '\'') {
            std::size_t end = offset + 1;
            while (end < text.size() && text[end] != first && text[end] != '\n') {
                if (text[end] == '\\' && end + 1 < text.size())
                    ++end;
                ++end;
            }

            if (end < text.size() && text[end] == first)
                ++end;

            range.end.character += end - offset - 1;
        }

        return range;
    }

    BracketMap bracketMap(const std::string& text) {
        BracketMap map;
        std::vector<std::size_t> stack;

        for (std::size_t i = 0; i < text.size(); ++i) {
            const char current = text[i];

            if (current == '/' && i + 1 < text.size() && text[i + 1] == '/') {
                while (i < text.size() && text[i] != '\n')
                    ++i;
                continue;
            }

            if (current == '/' && i + 1 < text.size() && text[i + 1] == '*') {
                i += 2;
                while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/'))
                    ++i;
                ++i;
                continue;
            }

            if (current == '"' || current == '\'') {
                ++i;
                while (i < text.size() && text[i] != current) {
                    if (text[i] == '\\')
                        ++i;
                    ++i;
                }
                continue;
            }

            if (current == '{' || current == '[') {
                stack.push_back(i);
                continue;
            }

            if (current == '}' || current == ']') {
                if (stack.empty())
                    continue;

                map.pairs.emplace_back(stack.back(), i);
                stack.pop_back();
            }
        }

        return map;
    }

    Scope whole(const std::string& text) {
        return Scope{ 0, text.size() };
    }

    Scope enclosingScope(const BracketMap& brackets, std::size_t offset, std::size_t end) {
        Scope best{ 0, end };

        for (const auto& [open, close] : brackets.pairs)
            if (open < offset && offset <= close && open >= best.start && close <= best.end)
                best = { open, close };

        return best;
    }

    Scope bodyScopeAfter(const BracketMap& brackets, const std::string& text, std::size_t offset, std::size_t end) {
        Scope best{ offset, end };
        bool found = false;

        for (const auto& [open, close] : brackets.pairs) {
            if (open < offset || text[open] != '{')
                continue;

            if (!found || open < best.start) {
                best = { open, close };
                found = true;
            }
        }

        return best;
    }

    std::size_t findNameFrom(const std::string& text, std::size_t from, const std::string& name) {
        if (name.empty())
            return from;

        std::size_t search = std::min(from, text.size());
        while (search + name.size() <= text.size()) {
            const auto found = text.find(name, search);
            if (found == std::string::npos)
                break;

            const bool leftClear = found == 0 || !isNameChar(text[found - 1]);
            const bool rightClear = found + name.size() >= text.size()
                || !isNameChar(text[found + name.size()]);

            if (leftClear && rightClear)
                return found;

            search = found + name.size();
        }

        return from;
    }

    std::size_t nameOffsetOf(const std::string& text, const SourceLocation& loc, const std::string& name) {
        const std::size_t start = std::min(loc.offset, text.size());
        const std::size_t found = findNameFrom(text, start, name);

        return found == start ? start : found;
    }

    std::string typeExprName(const TypeExpr& expr) {
        if (expr.name.empty())
            return {};

        std::string result = expr.dyn ? "dyn " + expr.name : expr.name;
        if (expr.args.empty())
            return result;

        result += "<";
        for (std::size_t i = 0; i < expr.args.size(); ++i) {
            if (i != 0)
                result += ", ";
            result += typeExprName(expr.args[i]);
        }

        return result + ">";
    }

    std::string typeName(const TypeExpr& expr, const TypeInfo& fallback) {
        if (expr.name.empty()) {
            const std::string inferred = typeInfoToString(fallback);

            return inferred == "unknown" ? std::string{} : inferred;
        }

        return typeExprName(expr);
    }

    std::string hintName(const TypeInfo& type) {
        std::string name = typeInfoToString(type);
        if (name.starts_with("class "))
            name.erase(0, 6);

        if (name.empty() || name == "unknown" || name == "void")
            return {};

        return name;
    }

    Position positionOfText(const std::string& text, std::size_t offset) {
        offset = std::min(offset, text.size());

        Position position;
        for (std::size_t i = 0; i < offset; ++i) {
            if (text[i] == '\n') {
                ++position.line;
                position.character = 0;
            } else {
                ++position.character;
            }
        }

        return position;
    }

    std::string signatureOf(const std::string& name, const MethodDecl& decl) {
        std::string result = name + "(";

        for (std::size_t i = 0; i < decl.params.size(); ++i) {
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

    std::string paramTypeOf(const MethodDecl& decl, std::size_t index) {
        const bool hasType = index < decl.params.size() &&
            (decl.params[index].hasType || !decl.params[index].typeExpr.name.empty());
        if (hasType)
            return typeExprName(decl.params[index].typeExpr);

        if (index < decl.paramTypes.size()) {
            const std::string type = typeInfoToString(decl.paramTypes[index]);
            if (type != "unknown")
                return type;
        }

        return {};
    }

    Symbol makeSymbol(const std::string& text, std::string name, SymbolKind kind,
        std::string detail, const SourceLocation& loc, std::string container,
        Scope scope, bool local, const std::string& uri) {
        Symbol symbol;
        symbol.name = std::move(name);
        symbol.kind = kind;
        symbol.detail = std::move(detail);
        symbol.range = rangeOf(loc);
        symbol.container = std::move(container);
        symbol.uri = uri;
        symbol.offset = nameOffsetOf(text, loc, symbol.name);
        symbol.scopeStart = scope.start;
        symbol.scopeEnd = scope.end;
        symbol.local = local;

        return symbol;
    }

    void addHint(std::vector<InlayHint>& hints, const std::string& text, std::size_t offset, std::string label) {
        if (offset == 0 || label.empty())
            return;

        InlayHint hint;
        hint.position = positionOfText(text, offset);
        hint.label = std::move(label);
        hints.push_back(std::move(hint));
    }

    SourceLocation locationAt(const std::string& text, std::size_t offset) {
        offset = std::min(offset, text.size());

        SourceLocation loc;
        loc.line = 1;
        loc.column = 1;
        loc.offset = offset;

        for (std::size_t i = 0; i < offset; ++i) {
            if (text[i] == '\n') {
                ++loc.line;
                loc.column = 1;
            } else {
                ++loc.column;
            }
        }

        return loc;
    }

    SourceLocation locOf(const ASTNode& node) {
        switch (node.getType()) {
            case ASTNode::Type::Block:
            case ASTNode::Type::Program: {
                const auto& sequence = static_cast<const SequenceNode&>(node);
                if (!sequence.parts.empty() && sequence.parts.front())
                    return locOf(*sequence.parts.front());
                return {};
            }
            case ASTNode::Type::Value: return static_cast<const ValueNode&>(node).loc;
            case ASTNode::Type::Compare: return static_cast<const CompareNode&>(node).loc;
            case ASTNode::Type::Logical: return static_cast<const LogicalNode&>(node).loc;
            case ASTNode::Type::If: return static_cast<const IfNode&>(node).loc;
            case ASTNode::Type::Arithmetic: return static_cast<const ArithmeticNode&>(node).loc;
            case ASTNode::Type::Unary: return static_cast<const UnaryNode&>(node).loc;
            case ASTNode::Type::Function: return static_cast<const FunctionNode&>(node).loc;
            case ASTNode::Type::Macro: return static_cast<const MacroNode&>(node).loc;
            case ASTNode::Type::Variable: return static_cast<const VariableNode&>(node).loc;
            case ASTNode::Type::Assignment: return static_cast<const AssignmentNode&>(node).loc;
            case ASTNode::Type::Class: return static_cast<const ClassNode&>(node).loc;
            case ASTNode::Type::New: return static_cast<const NewNode&>(node).loc;
            case ASTNode::Type::MemberAccess: return static_cast<const MemberAccessNode&>(node).loc;
            case ASTNode::Type::MethodCall: return static_cast<const MethodCallNode&>(node).loc;
            case ASTNode::Type::This: return static_cast<const ThisNode&>(node).loc;
            case ASTNode::Type::Super: return static_cast<const SuperNode&>(node).loc;
            case ASTNode::Type::SuperCall: return static_cast<const SuperCallNode&>(node).loc;
            case ASTNode::Type::InstanceOf: return static_cast<const InstanceOfNode&>(node).loc;
            case ASTNode::Type::FunctionDef: return static_cast<const FunctionDefNode&>(node).loc;
            case ASTNode::Type::FuncCall: return static_cast<const FuncCallNode&>(node).loc;
            case ASTNode::Type::Lambda: return static_cast<const LambdaNode&>(node).loc;
            case ASTNode::Type::Array: return static_cast<const ArrayNode&>(node).loc;
            case ASTNode::Type::Index: return static_cast<const IndexAccessNode&>(node).loc;
            case ASTNode::Type::While: return static_cast<const WhileNode&>(node).loc;
            case ASTNode::Type::For: return static_cast<const ForNode&>(node).loc;
            case ASTNode::Type::Break: return static_cast<const BreakNode&>(node).loc;
            case ASTNode::Type::Continue: return static_cast<const ContinueNode&>(node).loc;
            case ASTNode::Type::CompoundAssign: return static_cast<const CompoundAssignNode&>(node).loc;
            case ASTNode::Type::ForIn: return static_cast<const ForInNode&>(node).loc;
            case ASTNode::Type::Range: return static_cast<const RangeNode&>(node).loc;
            case ASTNode::Type::Coalesce: return static_cast<const CoalesceNode&>(node).loc;
            case ASTNode::Type::Try: return static_cast<const TryNode&>(node).loc;
            case ASTNode::Type::Import: return static_cast<const ImportNode&>(node).loc;
            case ASTNode::Type::Component: return static_cast<const ComponentNode&>(node).loc;
            case ASTNode::Type::Trait: return static_cast<const TraitNode&>(node).loc;
            case ASTNode::Type::Impl: return static_cast<const ImplNode&>(node).loc;
            case ASTNode::Type::Match: return static_cast<const MatchNode&>(node).loc;
            case ASTNode::Type::Panic: return static_cast<const PanicNode&>(node).loc;
            case ASTNode::Type::Enum: return static_cast<const EnumDeclNode&>(node).loc;
            case ASTNode::Type::LetPattern: return static_cast<const LetPatternNode&>(node).loc;
            case ASTNode::Type::Using: return static_cast<const UsingNode&>(node).loc;
            case ASTNode::Type::Return: return static_cast<const ReturnNode&>(node).loc;
            case ASTNode::Type::Expr: break;
        }

        return {};
    }

    Scope bodyScope(const BracketMap& brackets, const std::string& text, const ASTNode* body,
        const SourceLocation& fallback) {
        if (body) {
            const SourceLocation loc = locOf(*body);
            if (loc.offset > 0)
                return enclosingScope(brackets, loc.offset, text.size());
        }

        return bodyScopeAfter(brackets, text, fallback.offset, text.size());
    }

    void collectStatement(ASTNode& node, Collector& collector);

    void collectExpression(ExprNode& node, Collector& collector);

    void collectParams(const MethodDecl& decl, Collector& collector, const std::string& container,
        Scope scope) {
        if (!collector.deep)
            return;

        std::size_t cursor = decl.loc.offset;

        for (std::size_t i = 0; i < decl.params.size(); ++i) {
            const auto& param = decl.params[i];
            const std::size_t offset = findNameFrom(collector.text, cursor, param.name);
            cursor = offset + param.name.size();

            const SourceLocation loc = locationAt(collector.text, offset);

            const std::string type = paramTypeOf(decl, i);
            const std::string detail = type.empty() ? param.name : param.name + ": " + type;

            collector.symbols.push_back(makeSymbol(collector.text, param.name, SymbolKind::Parameter,
                detail, loc, container, scope, true, collector.uri));

            if (!param.hasType && param.typeExpr.name.empty() && i < decl.paramTypes.size()) {
                const std::string hint = hintName(decl.paramTypes[i]);
                if (!hint.empty())
                    addHint(collector.hints, collector.text, offset + param.name.size(), ": " + hint);
            }
        }
    }

    void collectMethodBody(const MethodDecl& decl, Collector& collector, const std::string& container) {
        const Scope scope = decl.body
            ? bodyScope(collector.brackets, collector.text, decl.body.get(), decl.loc)
            : enclosingScope(collector.brackets, decl.loc.offset, collector.text.size());

        collectParams(decl, collector, container, scope);

        if (collector.deep && decl.body)
            collectStatement(*decl.body, collector);
    }

    void collectClass(ClassNode& cls, Collector& collector) {
        const Scope scope = whole(collector.text);

        collector.symbols.push_back(makeSymbol(collector.text, cls.name, SymbolKind::Class,
            "class " + cls.name, cls.loc, "", scope, false, collector.uri));

        for (const auto& member : cls.members) {
            const std::size_t offset = nameOffsetOf(collector.text, member.loc, member.name);
            const Scope memberScope = enclosingScope(collector.brackets, offset, collector.text.size());

            collector.symbols.push_back(makeSymbol(collector.text, member.name,
                member.isStatic ? SymbolKind::StaticField : SymbolKind::Field,
                typeName(member.typeExpr, member.type), member.loc, cls.name, memberScope,
                false, collector.uri));
        }

        for (const auto& method : cls.methods) {
            const std::size_t offset = nameOffsetOf(collector.text, method.loc, method.name);
            const Scope methodScope = enclosingScope(collector.brackets, offset, collector.text.size());

            collector.symbols.push_back(makeSymbol(collector.text, method.name,
                method.isStatic ? SymbolKind::StaticMethod : SymbolKind::Method,
                signatureOf(method.name, method), method.loc, cls.name, methodScope,
                false, collector.uri));

            collectMethodBody(method, collector, cls.name);
        }
    }

    void collectTrait(TraitNode& trait, Collector& collector) {
        collector.symbols.push_back(makeSymbol(collector.text, trait.name, SymbolKind::Interface,
            "trait " + trait.name, trait.loc, "", whole(collector.text), false, collector.uri));

        for (const auto& method : trait.methods) {
            const std::size_t offset = nameOffsetOf(collector.text, method.loc, method.name);
            const Scope methodScope = enclosingScope(collector.brackets, offset, collector.text.size());

            collector.symbols.push_back(makeSymbol(collector.text, method.name, SymbolKind::Method,
                signatureOf(method.name, method), method.loc, trait.name, methodScope,
                false, collector.uri));

            collectMethodBody(method, collector, trait.name);
        }
    }

    void collectImpl(ImplNode& impl, Collector& collector) {
        for (const auto& method : impl.methods) {
            const std::size_t offset = nameOffsetOf(collector.text, method.loc, method.name);
            const Scope methodScope = enclosingScope(collector.brackets, offset, collector.text.size());

            collector.symbols.push_back(makeSymbol(collector.text, method.name,
                method.isStatic ? SymbolKind::StaticMethod : SymbolKind::Method,
                signatureOf(method.name, method), method.loc, impl.target.name, methodScope,
                false, collector.uri));

            collectMethodBody(method, collector, impl.target.name);
        }

        for (const auto& constant : impl.consts) {
            const std::size_t offset = nameOffsetOf(collector.text, constant.loc, constant.name);
            const Scope constantScope = enclosingScope(collector.brackets, offset, collector.text.size());

            collector.symbols.push_back(makeSymbol(collector.text, constant.name, SymbolKind::Constant,
                typeName(constant.typeExpr, constant.type), constant.loc, impl.target.name,
                constantScope, false, collector.uri));
        }
    }

    void collectEnum(EnumDeclNode& decl, Collector& collector) {
        const Scope scope = whole(collector.text);

        collector.symbols.push_back(makeSymbol(collector.text, decl.name, SymbolKind::Enum,
            "enum " + decl.name, decl.loc, "", scope, false, collector.uri));

        for (const auto& variant : decl.variants) {
            std::string detail = variant.name + "(";
            for (std::size_t i = 0; i < variant.fieldTypes.size(); ++i) {
                if (i != 0)
                    detail += ", ";
                detail += typeExprName(variant.fieldTypes[i]);
            }
            detail += ")";

            collector.symbols.push_back(makeSymbol(collector.text, variant.name, SymbolKind::EnumMember,
                std::move(detail), variant.loc, decl.name, scope, false, collector.uri));
        }
    }

    void collectComponent(ComponentNode& component, Collector& collector) {
        std::string detail = "component " + component.name + "(";
        for (std::size_t i = 0; i < component.params.size(); ++i) {
            if (i != 0)
                detail += ", ";
            detail += component.params[i];
        }
        detail += ")";

        collector.symbols.push_back(makeSymbol(collector.text, component.name, SymbolKind::Component,
            std::move(detail), component.loc, "", whole(collector.text), false, collector.uri));

        const Scope scope = bodyScope(collector.brackets, collector.text, component.body.get(), component.loc);
        std::size_t cursor = component.loc.offset;

        for (const auto& param : component.params) {
            const std::size_t offset = findNameFrom(collector.text, cursor, param);
            cursor = offset + param.size();

            const SourceLocation loc = locationAt(collector.text, offset);

            collector.symbols.push_back(makeSymbol(collector.text, param, SymbolKind::Parameter,
                param, loc, component.name, scope, true, collector.uri));
        }

        if (collector.deep && component.body)
            collectStatement(*component.body, collector);
    }

    void collectFunctionDef(FunctionDefNode& def, Collector& collector) {
        collector.symbols.push_back(makeSymbol(collector.text, def.name, SymbolKind::Function,
            signatureOf(def.name, def.decl), def.loc, "", whole(collector.text), false, collector.uri));

        collectMethodBody(def.decl, collector, "");
    }

    void collectUsing(UsingNode& node, Collector& collector) {
        const std::string type = typeExprName(node.type);

        collector.symbols.push_back(makeSymbol(collector.text, node.name, SymbolKind::Class,
            type.empty() ? "using " + node.name : "using " + type, node.loc, "",
            whole(collector.text), false, collector.uri));
    }

    void collectImport(ImportNode& node, Collector& collector) {
        collector.symbols.push_back(makeSymbol(collector.text, node.path, SymbolKind::Module,
            "import \"" + node.path + "\"", node.loc, "", whole(collector.text), false, collector.uri));
    }

    void collectDeclaration(AssignmentNode& assign, Collector& collector) {
        if (!assign.isDeclaration || !assign.target)
            return;

        const auto* variable = dynamic_cast<const VariableNode*>(assign.target.get());
        if (!variable)
            return;

        const std::size_t offset = nameOffsetOf(collector.text, variable->loc, variable->name);
        const Scope scope = enclosingScope(collector.brackets, offset, collector.text.size());
        const bool local = scope.start != 0 || scope.end != collector.text.size();

        TypeInfo inferred = assign.target->type;
        if (inferred.kind == TypeKind::Unknown && assign.value)
            inferred = assign.value->type;

        std::string detail = typeName(assign.declaredType, inferred);
        if (detail.empty() && assign.value && assign.value->getType() == ASTNode::Type::New)
            detail = "class " + static_cast<NewNode&>(*assign.value).className;

        if (detail.empty() && variable->name.starts_with('.'))
            return;

        collector.symbols.push_back(makeSymbol(collector.text, variable->name, SymbolKind::Variable,
            std::move(detail), assign.loc, "", scope, local, collector.uri));

        if (!assign.hasDeclaredType && collector.deep && !variable->name.starts_with('.')) {
            const std::string hint = hintName(inferred);
            if (!hint.empty())
                addHint(collector.hints, collector.text, offset + variable->name.size(), ": " + hint);
        }
    }

    void collectForIn(ForInNode& forIn, Collector& collector) {
        const Scope scope = bodyScope(collector.brackets, collector.text, forIn.body.get(), forIn.loc);
        std::size_t cursor = forIn.loc.offset;

        if (forIn.hasIndexVar && !forIn.indexVar.empty()) {
            const std::size_t offset = findNameFrom(collector.text, cursor, forIn.indexVar);
            cursor = offset + forIn.indexVar.size();

            const SourceLocation loc = locationAt(collector.text, offset);

            collector.symbols.push_back(makeSymbol(collector.text, forIn.indexVar, SymbolKind::Variable,
                "", loc, "", scope, true, collector.uri));
        }

        if (!forIn.elementVar.empty()) {
            const std::size_t offset = findNameFrom(collector.text, cursor, forIn.elementVar);
            const SourceLocation loc = locationAt(collector.text, offset);

            collector.symbols.push_back(makeSymbol(collector.text, forIn.elementVar, SymbolKind::Variable,
                "", loc, "", scope, true, collector.uri));
        }

        if (collector.deep) {
            if (forIn.iterable)
                collectExpression(*forIn.iterable, collector);
            if (forIn.body)
                collectStatement(*forIn.body, collector);
        }
    }

    void collectMatch(MatchNode& match, Collector& collector) {
        if (collector.deep && match.target)
            collectExpression(*match.target, collector);

        for (auto& arm : match.arms) {
            if (!arm)
                continue;

            const Scope scope = bodyScope(collector.brackets, collector.text, arm->body.get(), arm->loc);
            std::size_t cursor = arm->loc.offset;

            if (!arm->binder.empty()) {
                const std::size_t offset = findNameFrom(collector.text, cursor, arm->binder);
                cursor = offset + arm->binder.size();

                const SourceLocation loc = locationAt(collector.text, offset);

                collector.symbols.push_back(makeSymbol(collector.text, arm->binder,
                    SymbolKind::Variable, "", loc, "", scope, true, collector.uri));
            }

            for (const auto& binder : arm->binders) {
                const std::size_t offset = findNameFrom(collector.text, cursor, binder);
                cursor = offset + binder.size();

                const SourceLocation loc = locationAt(collector.text, offset);

                collector.symbols.push_back(makeSymbol(collector.text, binder,
                    SymbolKind::Variable, "", loc, "", scope, true, collector.uri));
            }

            if (collector.deep) {
                if (arm->guard)
                    collectExpression(*arm->guard, collector);
                if (arm->body)
                    collectStatement(*arm->body, collector);
            }
        }
    }

    void collectLetPattern(LetPatternNode& node, Collector& collector) {
        if (collector.deep && node.value)
            collectExpression(*node.value, collector);

        const Scope scope = bodyScopeAfter(collector.brackets, collector.text,
            node.loc.offset, collector.text.size());
        std::size_t cursor = node.loc.offset;

        for (const auto& binder : node.binders) {
            const std::size_t offset = findNameFrom(collector.text, cursor, binder);
            cursor = offset + binder.size();

            const SourceLocation loc = locationAt(collector.text, offset);

            collector.symbols.push_back(makeSymbol(collector.text, binder, SymbolKind::Variable,
                "", loc, "", scope, true, collector.uri));
        }
    }

    void collectLambda(LambdaNode& lambda, Collector& collector) {
        const Scope scope = bodyScope(collector.brackets, collector.text, lambda.decl.body.get(), lambda.decl.loc);
        collectParams(lambda.decl, collector, "", scope);

        if (collector.deep && lambda.decl.body)
            collectStatement(*lambda.decl.body, collector);
    }

    void collectExpression(ExprNode& node, Collector& collector) {
        switch (node.getType()) {
            case ASTNode::Type::Assignment:
                collectDeclaration(static_cast<AssignmentNode&>(node), collector);
                if (collector.deep) {
                    auto& assign = static_cast<AssignmentNode&>(node);
                    if (assign.target)
                        collectExpression(*assign.target, collector);
                    if (assign.value)
                        collectExpression(*assign.value, collector);
                }
                return;
            case ASTNode::Type::CompoundAssign: {
                if (!collector.deep)
                    return;

                auto& assign = static_cast<CompoundAssignNode&>(node);
                if (assign.target)
                    collectExpression(*assign.target, collector);
                if (assign.value)
                    collectExpression(*assign.value, collector);
                return;
            }
            case ASTNode::Type::Compare: {
                if (!collector.deep)
                    return;

                auto& compare = static_cast<CompareNode&>(node);
                collectExpression(*compare.left, collector);
                collectExpression(*compare.right, collector);
                return;
            }
            case ASTNode::Type::Logical: {
                if (!collector.deep)
                    return;

                auto& logical = static_cast<LogicalNode&>(node);
                collectExpression(*logical.left, collector);
                collectExpression(*logical.right, collector);
                return;
            }
            case ASTNode::Type::Arithmetic: {
                if (!collector.deep)
                    return;

                auto& arith = static_cast<ArithmeticNode&>(node);
                collectExpression(*arith.left, collector);
                collectExpression(*arith.right, collector);
                return;
            }
            case ASTNode::Type::Unary: {
                if (!collector.deep)
                    return;

                collectExpression(*static_cast<UnaryNode&>(node).operand, collector);
                return;
            }
            case ASTNode::Type::Range: {
                if (!collector.deep)
                    return;

                auto& range = static_cast<RangeNode&>(node);
                collectExpression(*range.start, collector);
                collectExpression(*range.end, collector);
                return;
            }
            case ASTNode::Type::Coalesce: {
                if (!collector.deep)
                    return;

                auto& coalesce = static_cast<CoalesceNode&>(node);
                collectExpression(*coalesce.left, collector);
                collectExpression(*coalesce.right, collector);
                return;
            }
            case ASTNode::Type::Try:
                if (collector.deep)
                    collectExpression(*static_cast<TryNode&>(node).operand, collector);
                return;
            case ASTNode::Type::If: {
                if (!collector.deep)
                    return;

                auto& ifNode = static_cast<IfNode&>(node);
                if (ifNode.condition)
                    collectExpression(*ifNode.condition, collector);
                if (ifNode.trueBranch)
                    collectStatement(*ifNode.trueBranch, collector);
                if (ifNode.falseBranch)
                    collectStatement(*ifNode.falseBranch, collector);
                return;
            }
            case ASTNode::Type::While: {
                if (!collector.deep)
                    return;

                auto& whileNode = static_cast<WhileNode&>(node);
                if (whileNode.condition)
                    collectExpression(*whileNode.condition, collector);
                if (whileNode.body)
                    collectStatement(*whileNode.body, collector);
                return;
            }
            case ASTNode::Type::For: {
                if (!collector.deep)
                    return;

                auto& forNode = static_cast<ForNode&>(node);
                if (forNode.init)
                    collectExpression(*forNode.init, collector);
                if (forNode.condition)
                    collectExpression(*forNode.condition, collector);
                if (forNode.step)
                    collectExpression(*forNode.step, collector);
                if (forNode.body)
                    collectStatement(*forNode.body, collector);
                return;
            }
            case ASTNode::Type::ForIn:
                collectForIn(static_cast<ForInNode&>(node), collector);
                return;
            case ASTNode::Type::Match:
                collectMatch(static_cast<MatchNode&>(node), collector);
                return;
            case ASTNode::Type::LetPattern:
                collectLetPattern(static_cast<LetPatternNode&>(node), collector);
                return;
            case ASTNode::Type::Function: {
                if (!collector.deep)
                    return;

                for (auto& arg : static_cast<FunctionNode&>(node).args)
                    collectExpression(*arg, collector);
                return;
            }
            case ASTNode::Type::Macro: {
                if (!collector.deep)
                    return;

                for (auto& arg : static_cast<MacroNode&>(node).args)
                    collectExpression(*arg, collector);
                return;
            }
            case ASTNode::Type::FuncCall: {
                if (!collector.deep)
                    return;

                for (auto& arg : static_cast<FuncCallNode&>(node).args)
                    collectExpression(*arg, collector);
                return;
            }
            case ASTNode::Type::MethodCall: {
                if (!collector.deep)
                    return;

                auto& call = static_cast<MethodCallNode&>(node);
                collectExpression(*call.target, collector);
                for (auto& arg : call.args)
                    collectExpression(*arg, collector);
                return;
            }
            case ASTNode::Type::MemberAccess:
                if (collector.deep)
                    collectExpression(*static_cast<MemberAccessNode&>(node).target, collector);
                return;
            case ASTNode::Type::Index: {
                if (!collector.deep)
                    return;

                auto& index = static_cast<IndexAccessNode&>(node);
                collectExpression(*index.target, collector);
                collectExpression(*index.index, collector);
                return;
            }
            case ASTNode::Type::Array: {
                if (!collector.deep)
                    return;

                for (auto& element : static_cast<ArrayNode&>(node).elements)
                    collectExpression(*element, collector);
                return;
            }
            case ASTNode::Type::New: {
                if (!collector.deep)
                    return;

                auto& newExpr = static_cast<NewNode&>(node);
                for (auto& arg : newExpr.args)
                    collectExpression(*arg, collector);
                if (newExpr.declarativeBlock)
                    collectStatement(*newExpr.declarativeBlock, collector);
                return;
            }
            case ASTNode::Type::SuperCall: {
                if (!collector.deep)
                    return;

                for (auto& arg : static_cast<SuperCallNode&>(node).args)
                    collectExpression(*arg, collector);
                return;
            }
            case ASTNode::Type::InstanceOf:
                if (collector.deep)
                    collectExpression(*static_cast<InstanceOfNode&>(node).target, collector);
                return;
            case ASTNode::Type::Lambda:
                collectLambda(static_cast<LambdaNode&>(node), collector);
                return;
            default:
                return;
        }
    }

    void collectStatement(ASTNode& node, Collector& collector) {
        switch (node.getType()) {
            case ASTNode::Type::Block:
            case ASTNode::Type::Program: {
                for (auto& part : static_cast<SequenceNode&>(node).parts)
                    if (part)
                        collectStatement(*part, collector);
                return;
            }
            case ASTNode::Type::Class:
                collectClass(static_cast<ClassNode&>(node), collector);
                return;
            case ASTNode::Type::Trait:
                collectTrait(static_cast<TraitNode&>(node), collector);
                return;
            case ASTNode::Type::Impl:
                collectImpl(static_cast<ImplNode&>(node), collector);
                return;
            case ASTNode::Type::Enum:
                collectEnum(static_cast<EnumDeclNode&>(node), collector);
                return;
            case ASTNode::Type::Component:
                collectComponent(static_cast<ComponentNode&>(node), collector);
                return;
            case ASTNode::Type::FunctionDef:
                collectFunctionDef(static_cast<FunctionDefNode&>(node), collector);
                return;
            case ASTNode::Type::Using:
                collectUsing(static_cast<UsingNode&>(node), collector);
                return;
            case ASTNode::Type::Import:
                collectImport(static_cast<ImportNode&>(node), collector);
                return;
            case ASTNode::Type::Return:
                if (collector.deep) {
                    auto& ret = static_cast<ReturnNode&>(node);
                    if (ret.value)
                        collectExpression(*ret.value, collector);
                }
                return;
            case ASTNode::Type::Panic:
                if (collector.deep) {
                    auto& panic = static_cast<PanicNode&>(node);
                    if (panic.message)
                        collectExpression(*panic.message, collector);
                }
                return;
            case ASTNode::Type::Break:
            case ASTNode::Type::Continue:
                return;
            default:
                if (collector.deep)
                    collectExpression(static_cast<ExprNode&>(node), collector);
                return;
        }
    }

    bool isWanted(const Symbol& symbol, bool all, const std::unordered_set<std::string>& wanted,
        const std::unordered_set<std::string>& enumNames) {
        if (all)
            return true;

        if (wanted.contains(symbol.name))
            return true;

        return symbol.kind == SymbolKind::EnumMember && enumNames.contains(symbol.container);
    }

    void collectImportedSymbols(const ResolvedImport& resolved, bool all,
        const std::unordered_set<std::string>& wanted, std::vector<Symbol>& out,
        const ImportResolver& resolver, std::vector<std::string>& stack, std::size_t depth) {
        if (depth > 8)
            return;

        if (std::find(stack.begin(), stack.end(), resolved.uri) != stack.end())
            return;

        stack.push_back(resolved.uri);

        DiagnosticEngine engine;
        Lexer lexer(resolved.text, engine);
        Parser parser(lexer, engine);
        auto ast = parser.parse();

        if (!ast || ast->getType() != ASTNode::Type::Program) {
            stack.pop_back();
            return;
        }

        auto& program = static_cast<ProgramNode&>(*ast);
        const BracketMap brackets = bracketMap(resolved.text);

        std::vector<Symbol> declared;
        std::vector<InlayHint> hints;
        Collector collector{ resolved.text, brackets, resolved.uri, false, declared, hints };

        for (auto& part : program.parts)
            if (part)
                collectStatement(*part, collector);

        std::unordered_set<std::string> enumNames;
        for (const auto& symbol : declared)
            if (symbol.kind == SymbolKind::Enum && (all || wanted.contains(symbol.name)))
                enumNames.insert(symbol.name);

        for (auto& symbol : declared) {
            if (symbol.kind == SymbolKind::Variable || symbol.kind == SymbolKind::Parameter ||
                symbol.kind == SymbolKind::Module)
                continue;

            if (isWanted(symbol, all, wanted, enumNames))
                out.push_back(std::move(symbol));
        }

        for (auto& part : program.parts) {
            if (!part || part->getType() != ASTNode::Type::Import)
                continue;

            auto& import = static_cast<ImportNode&>(*part);
            const auto child = resolver(import.path);
            if (!child)
                continue;

            const bool childAll = !import.hasSymbols;
            const std::unordered_set<std::string> childWanted(
                import.symbols.begin(), import.symbols.end());

            collectImportedSymbols(*child, childAll, childWanted,
                out, resolver, stack, depth + 1);
        }

        stack.pop_back();
    }
}

namespace LOICollection::frontend::lsp {
    using namespace detail;

    DocumentAnalysis analyze(const std::string& text, const ImportResolver& resolver) {
        DocumentAnalysis result;

        DiagnosticEngine engine;
        Lexer lexer(text, engine);
        Parser parser(lexer, engine);

        auto ast = parser.parse();

        ProgramNode* program = nullptr;
        std::vector<Symbol> components;
        std::vector<InlayHint> componentHints;
        const BracketMap brackets = bracketMap(text);

        if (ast && ast->getType() == ASTNode::Type::Program) {
            program = static_cast<ProgramNode*>(ast.get());

            Collector componentCollector{ text, brackets, "", true, components, componentHints };
            for (auto& part : program->parts)
                if (part && part->getType() == ASTNode::Type::Component)
                    collectStatement(*part, componentCollector);

            if (ComponentExpander::expand(*program, engine)) {
                SemanticAnalyzer analyzer(engine);
                analyzer.analyze(*program);
            }
        }

        for (const auto& diagnostic : engine.all()) {
            Diagnostic converted;
            converted.range = rangeOf(text, diagnostic.loc);
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

        if (program) {
            Collector collector{ text, brackets, "", true, result.symbols, result.hints };
            collectStatement(*program, collector);

            result.symbols.insert(result.symbols.end(),
                std::make_move_iterator(components.begin()),
                std::make_move_iterator(components.end()));
            result.hints.insert(result.hints.end(),
                std::make_move_iterator(componentHints.begin()),
                std::make_move_iterator(componentHints.end()));

            if (resolver) {
                std::vector<std::string> stack;

                for (auto& part : program->parts) {
                    if (!part || part->getType() != ASTNode::Type::Import)
                        continue;

                    auto& import = static_cast<ImportNode&>(*part);
                    const auto resolved = resolver(import.path);
                    if (!resolved)
                        continue;

                    const bool all = !import.hasSymbols;
                    const std::unordered_set<std::string> wanted(
                        import.symbols.begin(), import.symbols.end());

                    collectImportedSymbols(*resolved, all, wanted, result.imported,
                        resolver, stack, 0);
                }
            }
        }

        return result;
    }
}
