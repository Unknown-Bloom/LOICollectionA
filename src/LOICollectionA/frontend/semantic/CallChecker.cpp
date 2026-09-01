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

    std::string SemanticAnalyzer::receiverVariable(MethodCallNode& node) const {
        if (node.isStaticCall || node.target->getType() != ASTNode::Type::Variable)
            return {};

        const std::string& name = static_cast<VariableNode&>(*node.target).name;
        if (this->classByName.contains(name) || isNativeClass(name))
            return {};

        return name;
    }

    void SemanticAnalyzer::reportReceiverCaptures(const std::string& name) {
        std::vector<ReceiverCapture> remaining;
        for (auto& capture : this->receiverCaptures) {
            if (capture.name != name) {
                remaining.push_back(capture);
                continue;
            }

            this->diagnostics.addError(capture.loc,
                "Callback captures its receiver '" + name +
                "'; the closure would retain the object and leak it, use 'this' instead");
        }

        this->receiverCaptures = std::move(remaining);
    }

    TypeInfo SemanticAnalyzer::checkMethodCall(MethodCallNode& node, MethodScope& scope) {
        size_t argCount = node.args.size();

        auto checkArgs = [&] {
            std::vector<TypeInfo> types;
            types.reserve(argCount);
            for (auto& arg : node.args)
                types.push_back(checkExpr(*arg, scope));
            return types;
        };

        if (node.target->getType() == ASTNode::Type::Variable) {
            auto& var = static_cast<VariableNode&>(*node.target);
            if (auto clsOpt = this->findClass(var.name)) {
                std::vector<TypeInfo> argTypes = checkArgs();

                auto staticMethod = this->findStaticMethod(
                    clsOpt->get(), node.methodName, argTypes, scope
                );
                if (!staticMethod) {
                    diagnostics.addError(node.loc,
                        "No matching static method '" + node.methodName + "' with " +
                        std::to_string(argCount) + " argument(s) in class '" +
                        clsOpt->get().name + "'");
                    return {};
                }

                MethodDecl& method = staticMethod->method.get();
                node.isStaticCall = true;
                node.staticClassName = clsOpt->get().name;
                node.methodOrdinal = this->staticMethodOrdinal(
                    clsOpt->get().name, this->methodSignature(method)
                );

                for (size_t j = 0; j < argCount; ++j) {
                    if (method.paramTypes[j].kind == TypeKind::Optional &&
                        argTypes[j].kind == TypeKind::Optional) {
                        node.args[j]->preserveOptional = true;
                    }
                }

                return method.returnType;
            }

            if (isNativeClass(var.name)) {
                std::vector<TypeInfo> argTypes = checkArgs();

                std::vector<CallbackTypeArgs> signatures =
                    ClassCall::getInstance().getStaticMethodSignatures(var.name, node.methodName);

                for (const auto& signature : signatures) {
                    if (matchesNativeSignature(signature, argTypes)) {
                        node.isStaticCall = true;
                        node.staticClassName = var.name;
                        return {};
                    }
                }

                diagnostics.addError(node.loc,
                    "No matching static method '" + node.methodName + "' with " +
                    std::to_string(argCount) + " argument(s) in native class '" +
                    var.name + "'");
                return {};
            }
        }

        TypeInfo targetType = checkExpr(*node.target, scope);

        while (targetType.kind == TypeKind::Optional)
            targetType = *targetType.optionalInner;

        std::string receiverVar;
        if (targetType.kind == TypeKind::Object)
            receiverVar = this->receiverVariable(node);

        std::vector<TypeInfo> argTypes;
        if (!receiverVar.empty()) {
            this->receiverBindings.push_back({ receiverVar, targetType.className });
            auto binding = make_scope_guard([this] { this->receiverBindings.pop_back(); });

            argTypes = checkArgs();
            this->reportReceiverCaptures(receiverVar);
        } else {
            argTypes = checkArgs();
        }

        if (targetType.kind == TypeKind::Array || targetType.kind == TypeKind::String) {
            const std::string& className = targetType.kind == TypeKind::Array ? "Array" : "String";
            std::vector<CallbackTypeArgs> signatures =
                ClassCall::getInstance().getValueMethodSignatures(className, node.methodName);

            for (const auto& signature : signatures) {
                if (matchesNativeSignature(signature, argTypes)) {
                    node.className = className;
                    node.methodOrdinal = -1;
                    return {};
                }
            }

            diagnostics.addError(node.loc,
                "No matching method '" + node.methodName + "' with " +
                std::to_string(argCount) + " argument(s) on " + className + " values");
            return {};
        }

        if (targetType.kind == TypeKind::Generic) {
            auto boundIt = this->activeTypeParamBounds.find(targetType.typeVar);
            if (boundIt != this->activeTypeParamBounds.end()) {
                for (const auto& traitName : boundIt->second) {
                    auto traitIt = this->traits.find(traitName);
                    if (traitIt == this->traits.end())
                        continue;

                    for (const auto& m : traitIt->second) {
                        if (m.name != node.methodName || m.paramCount != argCount)
                            continue;

                        TypeInfo ret;
                        if (m.hasReturnType)
                            ret = this->resolveTypeExpr(m.returnTypeExpr, node.loc, false);
                        node.dynamicDispatch = true;
                        return ret;
                    }
                }
            }

            diagnostics.addError(node.loc,
                "Type '" + targetType.typeVar + "' has no method '" + node.methodName + "'");
            return {};
        }

        if (targetType.kind == TypeKind::Unknown) {
            ClassCall& classes = ClassCall::getInstance();
            for (const auto& className : classes.getClassNames()) {
                std::vector<CallbackTypeArgs> signatures =
                    classes.getValueMethodSignatures(className, node.methodName);

                for (const auto& signature : signatures) {
                    if (matchesNativeSignature(signature, argTypes)) {
                        node.className = className;
                        node.methodOrdinal = -1;
                        return {};
                    }
                }
            }
        }

        if (targetType.kind != TypeKind::Object) {
            diagnostics.addError(node.loc, "Method call target is not an object");
            return {};
        }

        auto clsOpt = findClass(targetType.className);
        if (!clsOpt) {
            if (isNativeClass(targetType.className)) {
                std::vector<CallbackTypeArgs> signatures =
                    ClassCall::getInstance().getMethodSignatures(targetType.className, node.methodName);

                for (const auto& signature : signatures) {
                    if (matchesNativeSignature(signature, argTypes)) {
                        node.className = targetType.className;
                        node.methodOrdinal = -1;
                        return {};
                    }
                }

                diagnostics.addError(node.loc,
                    "No matching method '" + node.methodName + "' with " +
                    std::to_string(argCount) + " argument(s) in native class '" +
                    targetType.className + "'");
                return {};
            }

            diagnostics.addError(node.loc, "Unknown class: " + targetType.className);
            return {};
        }

        ClassNode& cls = clsOpt->get();

        std::vector<std::pair<std::reference_wrapper<ClassNode>, std::reference_wrapper<MethodDecl>>> candidates;
        std::optional<std::reference_wrapper<ClassNode>> walk = std::ref(cls);
        for (size_t step = 0; step <= this->classes.size() && walk; ++step) {
            ClassNode& current = walk->get();
            for (auto& method : current.methods) {
                if (method.isConstructor || method.name != node.methodName)
                    continue;
                if (method.isStatic)
                    continue;
                if (method.params.size() != argCount)
                    continue;
                if (method.isPrivate &&
                    !(scope.hasMethod() && &scope.classRef() == &current)) {
                    continue;
                }

                bool match = true;
                for (size_t j = 0; j < argCount; ++j) {
                    const TypeInfo& param = method.paramTypes[j];
                    if (!this->isAssignableTo(param, argTypes[j])) {
                        match = false;
                        break;
                    }
                }

                if (match)
                    candidates.emplace_back(std::ref(current), std::ref(method));
            }

            if (current.baseClassName.empty())
                break;

            auto baseOpt = this->findClass(current.baseClassName);
            if (!baseOpt)
                break;

            walk = baseOpt;
        }

        if (candidates.empty()) {
            diagnostics.addError(node.loc,
                "No matching method '" + node.methodName + "' with " +
                std::to_string(argCount) + " argument(s) in class '" + cls.name + "'");
            return {};
        }

        auto depthOf = [&](const ClassNode& owner) {
            int depth = 0;
            std::optional<std::reference_wrapper<ClassNode>> cur = std::ref(cls);
            for (size_t step = 0; step <= this->classes.size() && cur; ++step) {
                ClassNode& current = cur->get();
                if (&current == &owner)
                    return depth;

                depth++;

                if (current.baseClassName.empty())
                    return std::numeric_limits<int>::max();

                auto baseOpt = this->findClass(current.baseClassName);
                if (!baseOpt)
                    return std::numeric_limits<int>::max();

                cur = baseOpt;
            }

            return std::numeric_limits<int>::max();
        };

        auto best = candidates[0];
        int bestDepth = depthOf(best.first.get());
        size_t bestScore = knownParamCount(best.second.get());

        for (size_t i = 1; i < candidates.size(); ++i) {
            int depth = depthOf(candidates[i].first.get());
            size_t score = knownParamCount(candidates[i].second.get());

            if (depth < bestDepth || (depth == bestDepth && score > bestScore)) {
                best = candidates[i];
                bestDepth = depth;
                bestScore = score;
            }
        }

        MethodDecl& method = best.second.get();

        node.className = cls.name;
        node.methodOrdinal = this->methodOrdinal(cls.name, this->methodSignature(method));

        for (size_t j = 0; j < argCount; ++j) {
            if (method.paramTypes[j].kind == TypeKind::Optional &&
                argTypes[j].kind == TypeKind::Optional) {
                node.args[j]->preserveOptional = true;
            }
        }

        return method.returnType;
    }

    TypeInfo SemanticAnalyzer::checkFuncCall(FuncCallNode& node, MethodScope& scope) {
        size_t argCount = node.args.size();

        std::vector<TypeInfo> argTypes;
        argTypes.reserve(argCount);
        for (auto& arg : node.args)
            argTypes.push_back(checkExpr(*arg, scope));

        if (node.isFormReceiverCall && !this->formReceivers.empty()) {
            const std::string& receiverClass = this->formReceivers.back();
            std::vector<CallbackTypeArgs> signatures =
                ClassCall::getInstance().getMethodSignatures(receiverClass, node.name);

            for (const auto& signature : signatures) {
                if (matchesNativeSignature(signature, argTypes)) {
                    node.receiverClassName = receiverClass;
                    return {};
                }
            }

            node.isFormReceiverCall = false;
        }

        auto it = functionsByName.find(node.name);
        if (it == functionsByName.end() || it->second.empty()) {
            if (scope.hasMethod() && scope.methodRef().isStatic && scope.hasClass()) {
                if (auto staticMethod = this->findStaticMethod(
                        scope.classRef(), node.name, argTypes, scope)) {
                    MethodDecl& method = staticMethod->method.get();
                    node.isStaticCall = true;
                    node.staticClassName = scope.classRef().name;
                    node.methodOrdinal = this->staticMethodOrdinal(
                        scope.classRef().name, this->methodSignature(method)
                    );

                    for (size_t j = 0; j < argCount; ++j) {
                        if (method.paramTypes[j].kind == TypeKind::Optional &&
                            argTypes[j].kind == TypeKind::Optional) {
                            node.args[j]->preserveOptional = true;
                        }
                    }

                    return method.returnType;
                }
            }

            if (this->isNameDefined(node.name, scope)) {
                TypeInfo variableType = this->lookupName(node.name, scope);

                if (variableType.kind != TypeKind::Unknown &&
                    variableType.kind != TypeKind::Function) {
                    diagnostics.addError(node.loc,
                        "Value '" + node.name + "' is not callable");
                    return {};
                }

                node.isCallable = true;
                node.resolvedName = node.name;
                return {};
            }

            diagnostics.addError(node.loc,
                "No matching function '" + node.name + "' with " +
                std::to_string(argCount) + " argument(s)");
            return {};
        }

        struct Cand {
            size_t index;
            std::unordered_map<std::string, TypeInfo> subst;
        };
        std::vector<Cand> candidates;
        std::vector<std::string> boundFailures;

        for (size_t i = 0; i < it->second.size(); ++i) {
            const auto& decl = it->second[i].get().decl;
            if (decl.params.size() != argCount)
                continue;

            bool match = true;
            std::unordered_map<std::string, TypeInfo> subst;
            if (!decl.typeParams.empty()) {
                subst = this->inferTypeParams(decl, argTypes);
                for (size_t j = 0; j < argCount; ++j) {
                    if (!this->isAssignableTo(
                            this->substituteType(decl.paramTypes[j], subst), argTypes[j])) {
                        match = false;
                        break;
                    }
                }
                if (match && !this->boundsSatisfied(decl.typeParams, subst))
                    match = false;
            } else {
                for (size_t j = 0; j < argCount; ++j) {
                    if (!this->isAssignableTo(decl.paramTypes[j], argTypes[j])) {
                        match = false;
                        break;
                    }
                }
            }

            if (!match)
                continue;

            if (decl.typeParams.empty()) {
                candidates.push_back({ i, {} });
            } else if (this->boundsSatisfied(decl.typeParams, subst)) {
                candidates.push_back({ i, subst });
            } else {
                boundFailures.push_back(decl.name);
            }
        }

        if (candidates.empty()) {
            if (!boundFailures.empty()) {
                diagnostics.addError(node.loc,
                    "Type bound not satisfied for generic function '" + node.name + "'");
            } else {
                diagnostics.addError(node.loc,
                    "No matching function '" + node.name + "' with " +
                    std::to_string(argCount) + " argument(s)");
            }
            return {};
        }

        size_t best = 0;
        size_t bestScore = knownParamCount(it->second[candidates[0].index].get().decl);
        for (size_t i = 1; i < candidates.size(); ++i) {
            size_t score = knownParamCount(it->second[candidates[i].index].get().decl);
            if (score > bestScore) {
                best = i;
                bestScore = score;
            }
        }

        size_t bestIndex = candidates[best].index;
        MethodDecl& decl = it->second[bestIndex].get().decl;

        node.resolvedName = node.name;
        node.functionOrdinal = static_cast<int>(bestIndex);

        for (size_t j = 0; j < argCount; ++j) {
            if (decl.paramTypes[j].kind == TypeKind::Optional &&
                argTypes[j].kind == TypeKind::Optional) {
                node.args[j]->preserveOptional = true;
            }
        }

        return this->substituteType(decl.returnType, candidates[best].subst);
    }

    TypeInfo SemanticAnalyzer::checkNew(NewNode& node, MethodScope& scope) {
        size_t argCount = node.args.size();

        std::vector<TypeInfo> argTypes;
        argTypes.reserve(argCount);
        for (auto& arg : node.args)
            argTypes.push_back(checkExpr(*arg, scope));

        if (node.declarativeBlock)
            this->checkDeclarativeBlock(node, scope);

        auto clsOpt = findClass(node.className);
        if (!clsOpt) {
            if (isNativeClass(node.className)) {
                std::vector<CallbackTypeArgs> signatures =
                    ClassCall::getInstance().getConstructorSignatures(node.className);

                if (signatures.empty()) {
                    if (argCount != 0) {
                        diagnostics.addError(node.loc,
                            "Native class '" + node.className + "' has no constructor");
                    }

                    return { TypeKind::Object, node.className };
                }

                for (const auto& signature : signatures) {
                    if (matchesNativeSignature(signature, argTypes))
                        return { TypeKind::Object, node.className };
                }

                diagnostics.addError(node.loc,
                    "No matching constructor for native class '" + node.className + "' with " +
                    std::to_string(argCount) + " argument(s)");
                return { TypeKind::Object, node.className };
            }

            diagnostics.addError(node.loc, "Unknown class: " + node.className);
            return {};
        }

        ClassNode& cls = clsOpt->get();
        auto ctorRef = this->findConstructor(cls);

        if (!ctorRef) {
            if (argCount != 0) {
                diagnostics.addError(node.loc,
                    "Class '" + cls.name + "' has no constructor");
            }

            return { TypeKind::Object, cls.name };
        }

        MethodDecl& ctor = ctorRef->method.get();

        if (ctor.params.size() != argCount) {
            diagnostics.addError(node.loc,
                "Constructor of class '" + cls.name + "' expects " +
                std::to_string(ctor.params.size()) + " argument(s), got " +
                std::to_string(argCount));

            return { TypeKind::Object, cls.name };
        }

        for (size_t j = 0; j < argCount; ++j) {
            if (!this->isAssignableTo(ctor.paramTypes[j], argTypes[j])) {
                diagnostics.addError(node.loc,
                    "Type mismatch for constructor parameter '" + ctor.params[j].name +
                    "': expected " + typeToString(ctor.paramTypes[j]) +
                    ", got " + typeToString(argTypes[j]));
            }

            if (ctor.paramTypes[j].kind == TypeKind::Optional &&
                argTypes[j].kind == TypeKind::Optional) {
                node.args[j]->preserveOptional = true;
            }
        }

        return { TypeKind::Object, cls.name };
    }

    TypeInfo SemanticAnalyzer::checkSuperCall(SuperCallNode& node, MethodScope& scope) {
        if (!scope.hasMethod() || !scope.hasClass()) {
            diagnostics.addError(node.loc, "'super(...)' is only available inside class methods");
            return {};
        }

        MethodDecl& method = scope.methodRef();
        if (method.isStatic) {
            diagnostics.addError(node.loc, "'super(...)' is not available inside static methods");
            return {};
        }

        if (!method.isConstructor) {
            diagnostics.addError(node.loc, "'super(...)' is only available inside constructors");
            return {};
        }

        ClassNode& cls = scope.classRef();
        if (cls.baseClassName.empty()) {
            diagnostics.addError(node.loc, "Class '" + cls.name + "' has no base class");
            return {};
        }

        method.hasSuperCall = true;

        size_t argCount = node.args.size();
        std::vector<TypeInfo> argTypes;
        argTypes.reserve(argCount);
        for (auto& arg : node.args)
            argTypes.push_back(checkExpr(*arg, scope));

        auto baseOpt = this->findClass(cls.baseClassName);
        if (!baseOpt) {
            diagnostics.addError(node.loc, "Unknown base class: " + cls.baseClassName);
            return {};
        }

        auto ctorRef = this->findConstructor(baseOpt->get());

        if (!ctorRef) {
            if (argCount != 0) {
                diagnostics.addError(node.loc, "Base class has no constructor");
            }

            node.constructorIndex = -1;
            return {};
        }

        ClassNode& ctorOwner = ctorRef->owner.get();
        MethodDecl& ctor = ctorRef->method.get();
        node.className = ctorOwner.name;
        for (size_t i = 0; i < ctorOwner.methods.size(); ++i) {
            if (&ctorOwner.methods[i] == &ctor) {
                node.constructorIndex = static_cast<int>(i);
                break;
            }
        }

        if (ctor.params.size() != argCount) {
            diagnostics.addError(node.loc,
                "Base constructor of class '" + cls.baseClassName + "' expects " +
                std::to_string(ctor.params.size()) + " argument(s), got " +
                std::to_string(argCount));
            return {};
        }

        for (size_t j = 0; j < argCount; ++j) {
            if (!this->isAssignableTo(ctor.paramTypes[j], argTypes[j])) {
                diagnostics.addError(node.loc,
                    "Type mismatch for base constructor parameter '" + ctor.params[j].name +
                    "': expected " + typeToString(ctor.paramTypes[j]) +
                    ", got " + typeToString(argTypes[j]));
            }

            if (ctor.paramTypes[j].kind == TypeKind::Optional &&
                argTypes[j].kind == TypeKind::Optional) {
                node.args[j]->preserveOptional = true;
            }
        }

        return {};
    }

    TypeInfo SemanticAnalyzer::checkInstanceOf(InstanceOfNode& node, MethodScope& scope) {
        checkExpr(*node.target, scope);

        if (!this->findClass(node.className) && !isNativeClass(node.className)) {
            diagnostics.addError(node.loc, "Unknown class: " + node.className);
        }

        return { TypeKind::Bool };
    }

    size_t SemanticAnalyzer::knownParamCount(const MethodDecl& method) const {
        size_t count = 0;
        for (const auto& type : method.paramTypes) {
            if (type.kind != TypeKind::Unknown)
                count++;
        }
        return count;
    }

    std::string SemanticAnalyzer::methodSignature(const MethodDecl& method) const {
        std::string signature = method.name + "(";
        for (size_t i = 0; i < method.paramTypes.size(); ++i) {
            if (i != 0)
                signature += ",";

            signature += this->typeToString(method.paramTypes[i]);
        }
        signature += ")";
        return signature;
    }

}
