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

    TypeInfo SemanticAnalyzer::checkExpr(ExprNode& node, MethodScope& scope) {
        node.preserveOptional = false;
        TypeInfo result = this->checkExprImpl(node, scope);
        node.type = result;
        return result;
    }

    TypeInfo SemanticAnalyzer::checkExprImpl(ExprNode& node, MethodScope& scope) {
        switch (node.getType()) {
            case ASTNode::Type::Assignment:
                return checkAssignment(static_cast<AssignmentNode&>(node), scope);

            case ASTNode::Type::CompoundAssign: {
                auto& assign = static_cast<CompoundAssignNode&>(node);
                TypeInfo target = checkExpr(*assign.target, scope);
                TypeInfo value = checkExpr(*assign.value, scope);
                return arithmeticResult(assign.op, target, value);
            }

            case ASTNode::Type::Coalesce: {
                auto& coalesce = static_cast<CoalesceNode&>(node);
                TypeInfo left = checkExpr(*coalesce.left, scope);
                TypeInfo right = checkExpr(*coalesce.right, scope);

                
                coalesce.left->preserveOptional = true;

                while (left.kind == TypeKind::Optional)
                    left = *left.optionalInner;

                if (left.kind == TypeKind::Unknown || right.kind == TypeKind::Unknown)
                    return {};
                if (left.kind == right.kind)
                    return left;

                return {};
            }

            case ASTNode::Type::Range: {
                auto& range = static_cast<RangeNode&>(node);
                TypeInfo start = checkExpr(*range.start, scope);
                TypeInfo end = checkExpr(*range.end, scope);

                if (start.kind != TypeKind::Unknown && start.kind != TypeKind::Int)
                    this->diagnostics.addError(range.loc, "Range start must be an int");
                if (end.kind != TypeKind::Unknown && end.kind != TypeKind::Int)
                    this->diagnostics.addError(range.loc, "Range end must be an int");

                return { TypeKind::Int };
            }

            case ASTNode::Type::Value:
                return typeOfValue(static_cast<ValueNode&>(node).value);

            case ASTNode::Type::Variable: {
                auto& var = static_cast<VariableNode&>(node);
                TypeInfo type = lookupName(var.name, scope);

                if (this->closureDepth > 0) {
                    for (const auto& binding : this->receiverBindings) {
                        if (binding.name != var.name)
                            continue;

                        this->receiverCaptures.push_back({ var.name, var.loc });
                        break;
                    }
                }

                if (scope.hasClass()) {
                    bool isParam = false;
                    if (scope.hasMethod()) {
                        for (const auto& param : scope.methodRef().params) {
                            if (param.name == var.name) {
                                isParam = true;
                                break;
                            }
                        }
                    }

                    if (isParam)
                        return type;

                    if (!scope.hasMethod() || !scope.methodRef().isStatic) {
                        if (auto field = findField(scope.classRef(), var.name)) {
                            if (field->member.get().isPrivate && &scope.classRef() != &field->owner.get()) {
                                diagnostics.addError(var.loc,
                                    "Cannot access private member '" + var.name + "'");
                            }

                            return type;
                        }
                    }

                    if (auto staticField = findStaticField(scope.classRef(), var.name)) {
                        if (staticField->member.get().isPrivate && &scope.classRef() != &staticField->owner.get()) {
                            diagnostics.addError(var.loc,
                                "Cannot access private member '" + var.name + "'");
                        }

                        var.isStaticField = true;
                        var.staticClassName = staticField->owner.get().name;
                        return type;
                    }
                }

                return type;
            }

            case ASTNode::Type::This: {
                auto& self = static_cast<ThisNode&>(node);
                if (scope.hasMethod() && scope.hasClass()) {
                    if (scope.methodRef().isStatic) {
                        diagnostics.addError(self.loc, "'this' is not available inside static methods");
                        return {};
                    }

                    return { TypeKind::Object, scope.classRef().name };
                }

                if (!this->formReceivers.empty())
                    return { TypeKind::Object, this->formReceivers.back() };

                if (this->closureDepth > 0 && !this->receiverBindings.empty())
                    return { TypeKind::Object, this->receiverBindings.back().className };

                diagnostics.addError(self.loc, "'this' is only available inside class methods");
                return {};
            }

            case ASTNode::Type::Super: {
                auto& superNode = static_cast<SuperNode&>(node);
                if (!scope.hasMethod() || !scope.hasClass()) {
                    diagnostics.addError(superNode.loc, "'super' is only available inside class methods");
                    return {};
                }

                if (scope.methodRef().isStatic) {
                    diagnostics.addError(superNode.loc, "'super' is not available inside static methods");
                    return {};
                }

                ClassNode& cls = scope.classRef();
                if (cls.baseClassName.empty()) {
                    diagnostics.addError(superNode.loc, "Class '" + cls.name + "' has no base class");
                    return {};
                }

                return { TypeKind::Object, cls.baseClassName };
            }

            case ASTNode::Type::SuperCall:
                return checkSuperCall(static_cast<SuperCallNode&>(node), scope);

            case ASTNode::Type::InstanceOf:
                return checkInstanceOf(static_cast<InstanceOfNode&>(node), scope);

            case ASTNode::Type::New:
                return checkNew(static_cast<NewNode&>(node), scope);

            case ASTNode::Type::MemberAccess:
                return checkMemberAccess(static_cast<MemberAccessNode&>(node), scope);

            case ASTNode::Type::MethodCall:
                return checkMethodCall(static_cast<MethodCallNode&>(node), scope);

            case ASTNode::Type::FuncCall:
                return checkFuncCall(static_cast<FuncCallNode&>(node), scope);

            case ASTNode::Type::Lambda:
                return checkLambda(static_cast<LambdaNode&>(node), scope);

            case ASTNode::Type::Array: {
                auto& array = static_cast<ArrayNode&>(node);
                for (auto& element : array.elements)
                    checkExpr(*element, scope);

                return { TypeKind::Array };
            }

            case ASTNode::Type::Index: {
                auto& indexNode = static_cast<IndexAccessNode&>(node);
                TypeInfo targetType = checkExpr(*indexNode.target, scope);
                TypeInfo indexType = checkExpr(*indexNode.index, scope);

                if (targetType.kind == TypeKind::Unknown)
                    return {};

                if (targetType.kind != TypeKind::Array) {
                    diagnostics.addError(indexNode.loc, "Cannot index a non-array value");
                    return {};
                }

                if (indexType.kind != TypeKind::Unknown && indexType.kind != TypeKind::Int) {
                    diagnostics.addError(indexNode.loc, "Array index must be an int");
                }

                return {};
            }

            case ASTNode::Type::If: {
                auto& ifNode = static_cast<IfNode&>(node);
                if (ifNode.condition)
                    checkExpr(*ifNode.condition, scope);
                if (ifNode.trueBranch)
                    checkStatement(*ifNode.trueBranch, scope);
                if (ifNode.falseBranch)
                    checkStatement(*ifNode.falseBranch, scope);

                return {};
            }

            case ASTNode::Type::Arithmetic: {
                auto& arith = static_cast<ArithmeticNode&>(node);
                TypeInfo left = checkExpr(*arith.left, scope);
                TypeInfo right = checkExpr(*arith.right, scope);
                return arithmeticResult(arith.op, left, right);
            }

            case ASTNode::Type::Compare: {
                auto& cmp = static_cast<CompareNode&>(node);
                checkExpr(*cmp.left, scope);
                checkExpr(*cmp.right, scope);
                return { TypeKind::Bool };
            }

            case ASTNode::Type::Logical: {
                auto& logical = static_cast<LogicalNode&>(node);
                checkExpr(*logical.left, scope);
                checkExpr(*logical.right, scope);
                return { TypeKind::Bool };
            }

            case ASTNode::Type::Unary: {
                auto& unary = static_cast<UnaryNode&>(node);
                TypeInfo operand = checkExpr(*unary.operand, scope);
                return unary.op == "!" ? TypeInfo{ TypeKind::Bool } : operand;
            }

            case ASTNode::Type::Function:
                for (auto& arg : static_cast<FunctionNode&>(node).args)
                    checkExpr(*arg, scope);
                return {};

            case ASTNode::Type::Macro:
                for (auto& arg : static_cast<MacroNode&>(node).args)
                    checkExpr(*arg, scope);
                return {};

            default:
                return {};
        }
    }

    TypeInfo SemanticAnalyzer::checkAssignment(AssignmentNode& node, MethodScope& scope) {
        TypeInfo rhs;
        if (node.value)
            rhs = checkExpr(*node.value, scope);

        switch (node.target->getType()) {
            case ASTNode::Type::Variable: {
                auto& var = static_cast<VariableNode&>(*node.target);
                if (scope.hasMethod()) {
                    MethodDecl& method = scope.methodRef();
                    for (size_t i = 0; i < method.params.size(); ++i) {
                        if (method.params[i].name == var.name) {
                            if (method.params[i].hasType) {
                                unify(method.paramTypes[i], rhs, node.loc, "parameter '" + var.name + "'");
                                if (method.paramTypes[i].kind == TypeKind::Optional &&
                                    rhs.kind == TypeKind::Optional && node.value) {
                                    node.value->preserveOptional = true;
                                }
                            }
                            return rhs;
                        }
                    }

                    if (scope.hasClass() && !scope.methodRef().isStatic) {
                        if (auto field = findField(scope.classRef(), var.name)) {
                            ClassMember& member = field->member.get();
                            if (member.isPrivate && &scope.classRef() != &field->owner.get()) {
                                diagnostics.addError(node.loc,
                                    "Cannot access private member '" + member.name + "'");
                            }

                            if (member.type.kind != TypeKind::Unknown) {
                                unify(member.type, rhs, node.loc, "member '" + var.name + "'");
                                if (member.type.kind == TypeKind::Optional &&
                                    rhs.kind == TypeKind::Optional && node.value) {
                                    node.value->preserveOptional = true;
                                }
                            }

                            if (scope.methodRef().isConstructor)
                                this->constructorAssignedMembers[scope.classRef().name].insert(var.name);
                            return rhs;
                        }
                    }

                    if (scope.hasClass()) {
                        if (auto staticField = findStaticField(scope.classRef(), var.name)) {
                            ClassMember& member = staticField->member.get();
                            if (member.isPrivate && &scope.classRef() != &staticField->owner.get()) {
                                diagnostics.addError(node.loc,
                                    "Cannot access private member '" + member.name + "'");
                            }

                            if (member.type.kind != TypeKind::Unknown) {
                                unify(member.type, rhs, node.loc, "static member '" + var.name + "'");
                                if (member.type.kind == TypeKind::Optional &&
                                    rhs.kind == TypeKind::Optional && node.value) {
                                    node.value->preserveOptional = true;
                                }
                            }

                            var.isStaticField = true;
                            var.staticClassName = staticField->owner.get().name;
                            return rhs;
                        }
                    }
                }

                if (node.isDeclaration && !this->blockScopes.empty()) {
                    auto& locals = this->blockScopes.back();
                    if (locals.contains(var.name)) {
                        this->diagnostics.addError(node.loc,
                            "Variable '" + var.name + "' is already declared in this scope");
                        return rhs;
                    }

                    if (node.hasDeclaredType) {
                        if (!node.value) {
                            this->diagnostics.addError(node.loc,
                                "Typed declaration of '" + var.name + "' requires an initializer");
                            return rhs;
                        }

                        TypeInfo declared = this->resolveTypeExpr(node.declaredType, node.loc, true);
                        this->unify(declared, rhs, node.loc, "variable '" + var.name + "'");
                        if (declared.kind == TypeKind::Optional &&
                            rhs.kind == TypeKind::Optional && node.value) {
                            node.value->preserveOptional = true;
                        }
                        locals[var.name] = declared;
                        return rhs;
                    }

                    locals[var.name] = rhs;
                    return rhs;
                }

                if (node.isDeclaration &&
                    (this->declaredGlobals.contains(var.name) ||
                     this->globalTypes.contains(var.name))) {
                    this->diagnostics.addError(node.loc,
                        "Variable '" + var.name + "' is already declared in this scope");
                    return rhs;
                }

                if (!this->blockScopes.empty()) {
                    for (auto it = this->blockScopes.rbegin(); it != this->blockScopes.rend(); ++it) {
                        auto found = it->find(var.name);
                        if (found != it->end()) {
                            if (rhs.kind != TypeKind::Unknown && rhs.kind != TypeKind::None)
                                found->second = rhs;
                            return rhs;
                        }
                    }

                    if (!this->isNameDefined(var.name, scope)) {
                        this->requireLet(node, var);
                        this->blockScopes.back()[var.name] = rhs;
                        return rhs;
                    }
                }

                if (node.hasDeclaredType) {
                    TypeInfo declared = this->resolveTypeExpr(node.declaredType, node.loc, true);

                    if (auto existing = this->declaredGlobals.find(var.name);
                        existing != this->declaredGlobals.end()) {
                        if (!(existing->second == declared)) {
                            this->diagnostics.addError(node.loc,
                                "Conflicting type declaration for variable '" + var.name + "'");
                        }
                    } else if (this->globalTypes.contains(var.name)) {
                        this->diagnostics.addError(node.loc,
                            "Variable '" + var.name + "' was already defined without an explicit type");
                    } else {
                        this->requireLet(node, var);
                        this->declaredGlobals[var.name] = declared;
                    }

                    if (!node.value) {
                        this->diagnostics.addError(node.loc,
                            "Typed declaration of '" + var.name + "' requires an initializer");
                        return rhs;
                    }

                    auto declaredIt = this->declaredGlobals.find(var.name);
                    if (declaredIt == this->declaredGlobals.end())
                        return rhs;

                    TypeInfo& target = declaredIt->second;
                    this->unify(target, rhs, node.loc, "variable '" + var.name + "'");
                    if (target.kind == TypeKind::Optional &&
                        rhs.kind == TypeKind::Optional && node.value) {
                        node.value->preserveOptional = true;
                    }
                    return rhs;
                }

                if (this->globalTypes.contains(var.name)) {
                    auto& existing = this->globalTypes[var.name];
                    this->unify(existing, rhs, node.loc, "variable '" + var.name + "'");
                    if (existing.kind == TypeKind::Optional &&
                        rhs.kind == TypeKind::Optional && node.value) {
                        node.value->preserveOptional = true;
                    }
                    return rhs;
                }

                if (this->declaredGlobals.contains(var.name)) {
                    auto& existing = this->declaredGlobals[var.name];
                    this->unify(existing, rhs, node.loc, "variable '" + var.name + "'");
                    if (existing.kind == TypeKind::Optional &&
                        rhs.kind == TypeKind::Optional && node.value) {
                        node.value->preserveOptional = true;
                    }
                    return rhs;
                }

                if (rhs.kind == TypeKind::None) {
                    this->requireLet(node, var);
                    globalTypes[var.name] = TypeInfo{};
                    return rhs;
                }

                this->requireLet(node, var);

                if (rhs.kind != TypeKind::Unknown)
                    globalTypes[var.name] = rhs;
                else
                    globalTypes.try_emplace(var.name, rhs);

                return rhs;
            }

            case ASTNode::Type::MemberAccess: {
                auto& member = static_cast<MemberAccessNode&>(*node.target);

                if (member.target->getType() == ASTNode::Type::Variable) {
                    auto& var = static_cast<VariableNode&>(*member.target);
                    if (auto clsOpt = this->findClass(var.name)) {
                        auto field = this->findStaticField(clsOpt->get(), member.memberName);
                        if (!field) {
                            diagnostics.addError(node.loc,
                                "Class '" + clsOpt->get().name + "' has no static member '" +
                                member.memberName + "'");
                            return rhs;
                        }

                        ClassMember& m = field->member.get();
                        if (m.isPrivate &&
                            !(scope.hasMethod() && &scope.classRef() == &field->owner.get())) {
                            diagnostics.addError(node.loc,
                                "Cannot access private member '" + m.name + "'");
                        }

                        if (m.type.kind != TypeKind::Unknown) {
                            unify(m.type, rhs, node.loc, "static member '" + m.name + "'");
                            if (m.type.kind == TypeKind::Optional &&
                                rhs.kind == TypeKind::Optional && node.value) {
                                node.value->preserveOptional = true;
                            }
                        }

                        member.isStaticAccess = true;
                        member.staticClassName = field->owner.get().name;
                        return rhs;
                    }

                    if (isNativeClass(var.name)) {
                        if (ClassCall::getInstance().hasStaticField(var.name, member.memberName)) {
                            member.isStaticAccess = true;
                            member.staticClassName = var.name;
                            return rhs;
                        }

                        diagnostics.addError(node.loc,
                            "Native class '" + var.name + "' has no static member '" +
                            member.memberName + "'");
                        return rhs;
                    }
                }

                TypeInfo targetType = checkExpr(*member.target, scope);

                if (targetType.kind == TypeKind::Unknown)
                    return rhs;

                if (targetType.kind == TypeKind::Optional)
                    targetType = *targetType.optionalInner;

                if (targetType.kind != TypeKind::Object) {
                    diagnostics.addError(node.loc, "Cannot access member of a non-object value");
                    return rhs;
                }

                auto clsOpt = findClass(targetType.className);
                if (!clsOpt) {
                    if (isNativeClass(targetType.className)) {
                        if (ClassCall::getInstance().hasField(targetType.className, member.memberName)) {
                            return rhs;
                        }

                        diagnostics.addError(node.loc,
                            "Class '" + targetType.className + "' has no member '" + member.memberName + "'");
                        return rhs;
                    }

                    diagnostics.addError(node.loc, "Unknown class: " + targetType.className);
                    return rhs;
                }

                ClassNode& cls = clsOpt->get();
                if (auto field = findField(cls, member.memberName)) {
                    ClassMember& m = field->member.get();
                    if (m.isPrivate && !(scope.hasMethod() && &scope.classRef() == &field->owner.get())) {
                        diagnostics.addError(node.loc, "Cannot access private member '" + m.name + "'");
                    }

                    if (m.type.kind != TypeKind::Unknown) {
                        unify(m.type, rhs, node.loc, "member '" + m.name + "'");
                        if (m.type.kind == TypeKind::Optional &&
                            rhs.kind == TypeKind::Optional && node.value) {
                            node.value->preserveOptional = true;
                        }
                    }

                    if (scope.hasMethod() && scope.methodRef().isConstructor &&
                        member.target->getType() == ASTNode::Type::This) {
                        this->constructorAssignedMembers[scope.classRef().name].insert(member.memberName);
                    }
                    return rhs;
                }

                diagnostics.addError(node.loc,
                    "Class '" + cls.name + "' has no member '" + member.memberName + "'");
                return rhs;
            }

            case ASTNode::Type::Index: {
                auto& indexNode = static_cast<IndexAccessNode&>(*node.target);
                TypeInfo targetType = checkExpr(*indexNode.target, scope);
                TypeInfo indexType = checkExpr(*indexNode.index, scope);

                if (targetType.kind == TypeKind::Unknown)
                    return rhs;

                if (targetType.kind != TypeKind::Array) {
                    diagnostics.addError(node.loc, "Cannot assign to an index of a non-array value");
                    return rhs;
                }

                if (indexType.kind != TypeKind::Unknown && indexType.kind != TypeKind::Int) {
                    diagnostics.addError(node.loc, "Array index must be an int");
                }

                return rhs;
            }

            default:
                diagnostics.addError(node.loc, "Invalid assignment target");
                return rhs;
        }
    }

    void SemanticAnalyzer::requireLet(AssignmentNode& node, const VariableNode& var) {
        if (node.isDeclaration)
            return;

        this->diagnostics.addError(node.loc,
            "Variable '" + var.name + "' is not declared; write 'let " + var.name + " = ...' to introduce it");
    }

}
