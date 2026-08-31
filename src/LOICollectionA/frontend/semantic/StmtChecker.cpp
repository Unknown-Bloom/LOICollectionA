#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include "LOICollectionA/base/ScopeGuard.h"
#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/Iteration.h"
#include "LOICollectionA/frontend/SemanticAnalyzer.h"

#include "LOICollectionA/frontend/semantic/Helpers.h"


namespace LOICollection::frontend {

    void SemanticAnalyzer::checkTopLevel(ProgramNode& root) {
        MethodScope emptyScope;

        for (auto& part : root.parts) {
            switch (part->getType()) {
                case ASTNode::Type::Class:
                case ASTNode::Type::FunctionDef:
                case ASTNode::Type::Trait:
                case ASTNode::Type::Impl:
                case ASTNode::Type::Using:
                case ASTNode::Type::Import:
                case ASTNode::Type::Component:
                    continue;
                default:
                    checkStatement(*part, emptyScope);
            }
        }
    }

    void SemanticAnalyzer::checkClassBodies() {
        for (auto cls : classes)
            checkClassBody(cls.get());
    }

    void SemanticAnalyzer::checkFunctionBodies() {
        for (auto fn : functions)
            checkBody(std::nullopt, fn.get().decl);
    }

    void SemanticAnalyzer::checkClassBody(ClassNode& cls) {
        for (auto& method : cls.methods)
            checkBody(std::ref(cls), method);
    }

    void SemanticAnalyzer::checkBody(std::optional<std::reference_wrapper<ClassNode>> cls, MethodDecl& method) {
        MethodScope scope{ cls, std::ref(method) };

        bool pushedTypeParams = false;
        if (!method.typeParams.empty()) {
            pushedTypeParams = true;
            this->activeTypeParams.clear();
            this->activeTypeParamBounds.clear();
            for (const auto& tp : method.typeParams) {
                TypeInfo g;
                g.kind = TypeKind::Generic;
                g.typeVar = tp.name;
                this->activeTypeParams[tp.name] = g;
                this->activeTypeParamBounds[tp.name] = tp.bounds;
            }
        }

        if (method.body) {
            this->blockScopes.emplace_back();
            auto popScope = make_scope_guard([this] { this->blockScopes.pop_back(); });

            checkStatement(*method.body, scope);
        }

        if (pushedTypeParams) {
            this->activeTypeParams.clear();
            this->activeTypeParamBounds.clear();
        }

        if (method.hasReturnType && !method.isConstructor &&
            method.returnType.kind != TypeKind::Void && !method.hasReturnStatement) {
            diagnostics.addWarning(method.loc,
                "Missing return statement in function '" + method.name + "'");
        }
    }

    void SemanticAnalyzer::checkStatement(ASTNode& node, MethodScope& scope) {
        switch (node.getType()) {
            case ASTNode::Type::Class:
            case ASTNode::Type::FunctionDef:
            case ASTNode::Type::Using:
            case ASTNode::Type::Import:
                return;
            case ASTNode::Type::Return:
                checkReturn(static_cast<ReturnNode&>(node), scope);
                return;
            case ASTNode::Type::While: {
                auto& whileNode = static_cast<WhileNode&>(node);
                ++scope.loopDepth;
                auto loops = make_scope_guard([&scope] { --scope.loopDepth; });

                if (whileNode.condition)
                    checkExpr(*whileNode.condition, scope);
                if (whileNode.body)
                    checkStatement(*whileNode.body, scope);
                return;
            }
            case ASTNode::Type::For: {
                auto& forNode = static_cast<ForNode&>(node);
                ++scope.loopDepth;
                auto loops = make_scope_guard([&scope] { --scope.loopDepth; });

                if (forNode.init)
                    checkExpr(*forNode.init, scope);
                if (forNode.condition)
                    checkExpr(*forNode.condition, scope);
                if (forNode.step)
                    checkExpr(*forNode.step, scope);
                if (forNode.body)
                    checkStatement(*forNode.body, scope);
                return;
            }
            case ASTNode::Type::ForIn: {
                auto& forIn = static_cast<ForInNode&>(node);

                ++scope.loopDepth;
                auto loops = make_scope_guard([&scope] { --scope.loopDepth; });

                TypeInfo iterableType = checkExpr(*forIn.iterable, scope);

                auto protocol = iterableProtocol(forIn.iterable->getType(), iterableType, this->classLookup());
                if (!protocol) {
                    this->diagnostics.addError(forIn.loc,
                        "for-in iterable must be an array, a string or a class providing '" +
                        std::string(lengthMethod) + "()' and '" + std::string(elementMethod) +
                        "(int)', got " + typeInfoToString(iterableType));
                }

                auto declareLoopVar = [this](const std::string& name, TypeInfo type) {
                    if (this->blockScopes.empty())
                        this->globalTypes[name] = std::move(type);
                    else
                        this->blockScopes.back()[name] = std::move(type);
                };

                if (forIn.hasIndexVar)
                    declareLoopVar(forIn.indexVar, TypeInfo{ TypeKind::Int });
                declareLoopVar(forIn.elementVar,
                    protocol ? protocol->elementType : TypeInfo{});

                if (forIn.body)
                    checkStatement(*forIn.body, scope);
                return;
            }
            case ASTNode::Type::Break:
                if (scope.loopDepth == 0)
                    this->diagnostics.addError(static_cast<BreakNode&>(node).loc,
                        "'break' can only be used inside a loop");
                return;
            case ASTNode::Type::Continue:
                if (scope.loopDepth == 0)
                    this->diagnostics.addError(static_cast<ContinueNode&>(node).loc,
                        "'continue' can only be used inside a loop");
                return;
            case ASTNode::Type::Block:
                for (auto& part : static_cast<BlockNode&>(node).parts)
                    checkStatement(*part, scope);
                return;
            default: {
                TypeInfo type = checkExpr(static_cast<ExprNode&>(node), scope);
                if (type.kind == TypeKind::None &&
                    node.getType() != ASTNode::Type::Assignment) {
                    this->diagnostics.addError({0, 0, 0},
                        "'None' is only allowed when assigning to an optional value");
                }
                return;
            }
        }
    }

    void SemanticAnalyzer::checkDeclarativeBlock(NewNode& node, MethodScope& scope) {
        if (!isWhitelistedFormClass(node.className) || !isNativeClass(node.className)) {
            diagnostics.addError(node.loc,
                "Declarative UI blocks are only allowed on form classes "
                "(CustomForm, MessageBox, PaginatedForm, ScriptForm), got '" + node.className + "'");
            return;
        }

        this->blockScopes.emplace_back();
        this->formReceivers.push_back(node.className);

        if (!node.receiverName.empty())
            this->blockScopes.back()[node.receiverName] = { TypeKind::Object, node.className };

        for (auto& part : node.declarativeBlock->parts)
            checkStatement(*part, scope);

        this->formReceivers.pop_back();
        this->blockScopes.pop_back();
    }

    TypeInfo SemanticAnalyzer::checkReturn(ReturnNode& node, MethodScope& scope) {
        if (!scope.hasMethod()) {
            diagnostics.addError(node.loc, "'return' is only available inside functions or class methods");
            return {};
        }

        MethodDecl& method = scope.methodRef();
        method.hasReturnStatement = true;

        TypeInfo valueType;
        if (node.value) {
            valueType = checkExpr(*node.value, scope);

            if (method.returnType.kind == TypeKind::Void) {
                diagnostics.addError(node.loc,
                    "Void function '" + method.name + "' cannot return a value");
                return valueType;
            }

            if (method.hasReturnType &&
                method.returnType.kind != TypeKind::Unknown &&
                valueType.kind != TypeKind::Unknown &&
                !this->isAssignableTo(method.returnType, valueType)) {
                diagnostics.addError(node.loc,
                    "Return type mismatch in function '" + method.name +
                    "': expected " + typeToString(method.returnType) +
                    ", got " + typeToString(valueType));
            }

            if (method.returnType.kind == TypeKind::Optional &&
                valueType.kind == TypeKind::Optional) {
                node.value->preserveOptional = true;
            }
        }

        return valueType;
    }

    TypeInfo SemanticAnalyzer::checkLambda(LambdaNode& node, MethodScope& scope) {
        MethodDecl& decl = node.decl;
        decl.paramTypes.resize(decl.params.size());

        for (size_t i = 0; i < decl.params.size(); ++i) {
            if (decl.params[i].hasType)
                decl.paramTypes[i] = this->resolveTypeExpr(
                    decl.params[i].typeExpr, decl.loc, true);
        }

        if (decl.hasReturnType)
            decl.returnType = this->resolveTypeExpr(
                decl.returnTypeExpr, decl.loc, true);

        MethodScope lambdaScope;
        if (scope.hasClass())
            lambdaScope.cls = scope.cls;
        lambdaScope.method = std::ref(decl);

        ++this->closureDepth;
        auto closure = make_scope_guard([this] { --this->closureDepth; });

        if (decl.body)
            checkStatement(*decl.body, lambdaScope);

        if (decl.hasReturnType &&
            decl.returnType.kind != TypeKind::Void && !decl.hasReturnStatement) {
            diagnostics.addWarning(decl.loc,
                "Missing return statement in anonymous function");
        }

        return { TypeKind::Function };
    }

    TypeInfo SemanticAnalyzer::lookupName(const std::string& name, MethodScope& scope) {
        for (auto it = this->blockScopes.rbegin(); it != this->blockScopes.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end())
                return found->second;
        }

        if (scope.hasMethod()) {
            MethodDecl& method = scope.methodRef();
            for (size_t i = 0; i < method.params.size(); ++i) {
                if (method.params[i].name == name)
                    return method.paramTypes[i];
            }

            if (scope.hasClass() && !scope.methodRef().isStatic) {
                if (auto field = findField(scope.classRef(), name)) {
                    if (field->member.get().isPrivate && &scope.classRef() != &field->owner.get())
                        return {};

                    return field->member.get().type;
                }
            }

            if (scope.hasClass()) {
                if (auto staticField = findStaticField(scope.classRef(), name)) {
                    if (staticField->member.get().isPrivate && &scope.classRef() != &staticField->owner.get())
                        return {};

                    return staticField->member.get().type;
                }
            }
        }

        if (auto declaredIt = declaredGlobals.find(name);
            declaredIt != declaredGlobals.end()) {
            return declaredIt->second;
        }

        auto it = globalTypes.find(name);
        if (it != globalTypes.end())
            return it->second;

        return {};
    }

    bool SemanticAnalyzer::isNameDefined(const std::string& name, MethodScope& scope) const {
        for (const auto& blockScope : this->blockScopes) {
            if (blockScope.contains(name))
                return true;
        }

        if (scope.hasMethod()) {
            const MethodDecl& method = scope.methodRef();
            for (const auto& param : method.params) {
                if (param.name == name)
                    return true;
            }

            if (scope.hasClass()) {
                if (!scope.methodRef().isStatic && findField(scope.classRef(), name))
                    return true;

                if (findStaticField(scope.classRef(), name))
                    return true;
            }
        }

        return globalTypes.find(name) != globalTypes.end() ||
               declaredGlobals.find(name) != declaredGlobals.end();
    }

    void SemanticAnalyzer::unify(TypeInfo& target, const TypeInfo& from, SourceLocation loc, const std::string& what) {
        if (from.kind == TypeKind::Unknown || from.kind == TypeKind::Void)
            return;

        if (target.kind == TypeKind::Unknown) {
            target = from;
            return;
        }

        if (from.kind == TypeKind::None && target.kind != TypeKind::Optional) {
            diagnostics.addError(loc,
                "'None' is only allowed when assigning to an optional value (" + what + ")");
            return;
        }

        if (!this->isAssignableTo(target, from)) {
            std::string hint = from.kind == TypeKind::Optional && target.kind != TypeKind::Optional
                ? " (consider using '\?\?' to provide a default value)"
                : "";

            diagnostics.addError(loc,
                "Type mismatch for " + what + ": expected " +
                typeToString(target) + ", got " + typeToString(from) + hint);
        }
    }

}
