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
                if (!node.isColonColon) {
                    diagnostics.addError(node.loc,
                        "Static method '" + node.methodName + "' of class '" + clsOpt->get().name +
                        "' must be accessed with '::' (found '" + clsOpt->get().name + "." + node.methodName + "')");
                    return {};
                }

                std::vector<TypeInfo> argTypes = checkArgs();
                std::vector<TypeInfo> explicitTypes;
                for (const auto& typeArg : node.typeArgs)
                    explicitTypes.push_back(this->resolveTypeExpr(typeArg, node.loc, true));

                TypeInfo classType;
                if (scope.hasClass() && &scope.classRef() == &clsOpt->get()) {
                    classType = this->selfType(clsOpt->get());
                } else {
                    classType.kind = TypeKind::Object;
                    classType.className = clsOpt->get().name;
                }

                auto staticMethod = this->findStaticMethod(
                    clsOpt->get(), node.methodName, argTypes, scope, classType, explicitTypes
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
                node.paramTypes.clear();
                for (const auto& paramType : method.paramTypes)
                    node.paramTypes.push_back(this->substituteType(paramType, staticMethod->subst));

                for (size_t j = 0; j < argCount; ++j) {
                    if (node.paramTypes[j].kind == TypeKind::Optional &&
                        argTypes[j].kind == TypeKind::Optional) {
                        node.args[j]->preserveOptional = true;
                    }
                }

                return this->substituteType(method.returnType, staticMethod->subst);
            }

            if (isNativeClass(var.name)) {
                if (!node.isColonColon) {
                    diagnostics.addError(node.loc,
                        "Native class '" + var.name + "' members must be accessed with '::' (found '" +
                        var.name + "." + node.methodName + "')");
                    return {};
                }

                if (!node.typeArgs.empty())
                    diagnostics.addError(node.loc,
                        "Native class '" + var.name + "' does not accept type arguments");

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
            if (!node.typeArgs.empty())
                diagnostics.addError(node.loc,
                    "Method '" + node.methodName + "' does not accept type arguments");

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

        if (targetType.kind == TypeKind::Trait) {
            if (!node.typeArgs.empty())
                diagnostics.addError(node.loc,
                    "Method '" + node.methodName + "' does not accept type arguments");

            auto traitIt = this->traits.find(targetType.className);
            if (traitIt == this->traits.end()) {
                diagnostics.addError(node.loc, "Unknown trait: " + targetType.className);
                return {};
            }

            for (const auto& m : traitIt->second) {
                if (m.name != node.methodName || m.paramCount != argCount)
                    continue;

                TypeInfo ret;
                if (m.hasReturnType)
                    ret = this->resolveTypeExpr(m.returnTypeExpr, node.loc, false);
                node.dynamicDispatch = true;
                return ret;
            }

            diagnostics.addError(node.loc,
                "Trait '" + targetType.className + "' has no method '" + node.methodName +
                "' with " + std::to_string(argCount) + " argument(s)");
            return {};
        }

        if (targetType.kind == TypeKind::Generic) {
            if (!node.typeArgs.empty())
                diagnostics.addError(node.loc,
                    "Method '" + node.methodName + "' does not accept type arguments");

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
            ClassCall& classCall = ClassCall::getInstance();
            for (const auto& className : classCall.getClassNames()) {
                std::vector<CallbackTypeArgs> signatures =
                    classCall.getValueMethodSignatures(className, node.methodName);

                for (const auto& signature : signatures) {
                    if (matchesNativeSignature(signature, argTypes)) {
                        node.className = className;
                        node.methodOrdinal = -1;
                        return {};
                    }
                }
            }
        }

        if (targetType.kind == TypeKind::Unknown)
            return {};

        if (targetType.kind == TypeKind::Function) {
            if (node.methodName == "call") {
                return {};
            }

            diagnostics.addError(node.loc, "Method call target is not an object");
            return {};
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

        std::vector<TypeInfo> explicitTypes;
        for (const auto& typeArg : node.typeArgs)
            explicitTypes.push_back(this->resolveTypeExpr(typeArg, node.loc, true));

        struct Cand {
            std::reference_wrapper<ClassNode> owner;
            std::reference_wrapper<MethodDecl> method;
            TypeSubst subst;
        };

        std::vector<Cand> candidates;
        bool boundFailure = false;
        bool explicitMismatch = false;

        std::optional<std::reference_wrapper<ClassNode>> walk = std::ref(cls);
        for (size_t step = 0; step <= this->classes.size() && walk; ++step) {
            ClassNode& current = walk->get();
            const TypeSubst ownerSubst = this->substitutionFor(current, targetType);

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

                TypeSubst subst = ownerSubst;
                if (!explicitTypes.empty()) {
                    if (method.typeParams.size() != explicitTypes.size()) {
                        explicitMismatch = true;
                        continue;
                    }

                    for (size_t index = 0; index < method.typeParams.size(); ++index)
                        subst[method.typeParams[index].name] = explicitTypes[index];
                }

                if (!method.typeParams.empty() &&
                    !this->inferTypeArgs(method.typeParams, method.paramTypes, argTypes, subst))
                    continue;

                bool match = true;
                for (size_t j = 0; j < argCount; ++j) {
                    const TypeInfo param = this->substituteType(method.paramTypes[j], subst);
                    if (!this->isAssignableTo(param, argTypes[j])) {
                        match = false;
                        break;
                    }
                }

                if (!match)
                    continue;

                if (!this->typeParamBoundsSatisfied(method.typeParams, subst)) {
                    boundFailure = true;
                    continue;
                }

                candidates.push_back({ std::ref(current), std::ref(method), std::move(subst) });
            }

            if (current.baseClassName.empty())
                break;

            auto baseOpt = this->findClass(current.baseClassName);
            if (!baseOpt)
                break;

            walk = baseOpt;
        }

        if (candidates.empty()) {
            if (boundFailure) {
                diagnostics.addError(node.loc,
                    "Type bound not satisfied for generic method '" + node.methodName + "'");
            } else if (explicitMismatch) {
                diagnostics.addError(node.loc,
                    "No matching generic method '" + node.methodName + "' for " +
                    std::to_string(explicitTypes.size()) + " explicit type argument(s) in class '" +
                    cls.name + "'");
            } else {
                diagnostics.addError(node.loc,
                    "No matching method '" + node.methodName + "' with " +
                    std::to_string(argCount) + " argument(s) in class '" + cls.name + "'");
            }
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

        size_t best = 0;
        int bestDepth = depthOf(candidates[0].owner.get());
        size_t bestScore = knownParamCount(candidates[0].method.get());

        for (size_t i = 1; i < candidates.size(); ++i) {
            int depth = depthOf(candidates[i].owner.get());
            size_t score = knownParamCount(candidates[i].method.get());

            if (depth < bestDepth || (depth == bestDepth && score > bestScore)) {
                best = i;
                bestDepth = depth;
                bestScore = score;
            }
        }

        MethodDecl& method = candidates[best].method.get();

        node.className = cls.name;
        node.methodOrdinal = this->methodOrdinal(cls.name, this->methodSignature(method));
        node.paramTypes.clear();
        for (const auto& paramType : method.paramTypes)
            node.paramTypes.push_back(this->substituteType(paramType, candidates[best].subst));

        for (size_t j = 0; j < argCount; ++j) {
            if (node.paramTypes[j].kind == TypeKind::Optional &&
                argTypes[j].kind == TypeKind::Optional) {
                node.args[j]->preserveOptional = true;
            }
        }

        return this->substituteType(method.returnType, candidates[best].subst);
    }

    TypeInfo SemanticAnalyzer::checkFuncCall(FuncCallNode& node, MethodScope& scope) {
        size_t argCount = node.args.size();

        std::vector<TypeInfo> argTypes;
        argTypes.reserve(argCount);
        for (auto& arg : node.args)
            argTypes.push_back(checkExpr(*arg, scope));

        auto variantIt = this->enumVariant.find(node.name);
        if (variantIt != this->enumVariant.end()) {
            const EnumVariantInfo& info = variantIt->second;
            node.isEnumConstruction = true;
            node.paramTypes.clear();

            auto enumIt = this->enumNodes.find(info.enumName);
            const std::vector<TypeParam>* typeParams = enumIt == this->enumNodes.end()
                ? nullptr
                : &enumIt->second.get().typeParams;

            TypeSubst subst;
            std::vector<TypeInfo> explicitTypes;
            for (const auto& typeArg : node.typeArgs)
                explicitTypes.push_back(this->resolveTypeExpr(typeArg, node.loc, true));

            if (typeParams) {
                if (!explicitTypes.empty()) {
                    if (explicitTypes.size() != typeParams->size()) {
                        diagnostics.addError(node.loc,
                            typeParams->empty()
                                ? "Enum '" + info.enumName + "' does not accept type arguments"
                                : "Enum '" + info.enumName + "' expects " +
                                  std::to_string(typeParams->size()) +
                                  " type argument(s), got " + std::to_string(explicitTypes.size()));
                    } else {
                        for (size_t index = 0; index < typeParams->size(); ++index)
                            subst[(*typeParams)[index].name] = explicitTypes[index];
                    }
                }

                for (const auto& param : *typeParams)
                    subst.try_emplace(param.name, TypeInfo{ TypeKind::Unknown });

                static_cast<void>(this->inferTypeArgs(
                    *typeParams, info.payloadTypes, argTypes, subst));
                static_cast<void>(this->checkTypeParamBounds(
                    *typeParams, subst, node.loc, "enum '" + info.enumName + "'"));
            }

            for (const TypeInfo& payloadType : info.payloadTypes)
                node.paramTypes.push_back(this->substituteType(payloadType, subst));

            if (static_cast<int>(argCount) != info.arity) {
                diagnostics.addError(node.loc,
                    "Enum variant '" + node.name + "' expects " +
                    std::to_string(info.arity) + " argument(s), got " +
                    std::to_string(argCount));
            }

            for (size_t j = 0; j < argCount && j < node.paramTypes.size(); ++j) {
                if (!this->isAssignableTo(node.paramTypes[j], argTypes[j])) {
                    diagnostics.addError(node.loc,
                        "Type mismatch for enum variant '" + node.name +
                        "' payload slot " + std::to_string(j) + ": expected " +
                        typeToString(node.paramTypes[j]) + ", got " +
                        typeToString(argTypes[j]));
                }
            }

            TypeInfo result;
            result.kind = TypeKind::Enum;
            result.className = info.enumName;

            if (typeParams) {
                for (const auto& param : *typeParams) {
                    auto bound = subst.find(param.name);
                    result.typeArgs.push_back(bound == subst.end()
                        ? TypeInfo{ TypeKind::Unknown }
                        : bound->second);
                }
            }

            return result;
        }

        std::vector<TypeInfo> explicitTypes;
        for (const auto& typeArg : node.typeArgs)
            explicitTypes.push_back(this->resolveTypeExpr(typeArg, node.loc, true));

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
                        scope.classRef(), node.name, argTypes, scope,
                        this->selfType(scope.classRef()), explicitTypes)) {
                    MethodDecl& method = staticMethod->method.get();
                    node.isStaticCall = true;
                    node.staticClassName = scope.classRef().name;
                    node.methodOrdinal = this->staticMethodOrdinal(
                        scope.classRef().name, this->methodSignature(method)
                    );
                    node.paramTypes.clear();
                    for (const auto& paramType : method.paramTypes)
                        node.paramTypes.push_back(this->substituteType(paramType, staticMethod->subst));

                    for (size_t j = 0; j < argCount; ++j) {
                        if (node.paramTypes[j].kind == TypeKind::Optional &&
                            argTypes[j].kind == TypeKind::Optional) {
                            node.args[j]->preserveOptional = true;
                        }
                    }

                    return this->substituteType(method.returnType, staticMethod->subst);
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

                if (!explicitTypes.empty()) {
                    diagnostics.addError(node.loc,
                        "Value '" + node.name + "' does not accept type arguments");
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
            TypeSubst subst;
        };
        std::vector<Cand> candidates;
        bool boundFailure = false;
        bool explicitMismatch = false;

        for (size_t i = 0; i < it->second.size(); ++i) {
            const auto& decl = it->second[i].get().decl;
            if (decl.params.size() != argCount)
                continue;

            TypeSubst subst;
            if (!explicitTypes.empty()) {
                if (decl.typeParams.size() != explicitTypes.size()) {
                    explicitMismatch = true;
                    continue;
                }

                for (size_t index = 0; index < decl.typeParams.size(); ++index)
                    subst[decl.typeParams[index].name] = explicitTypes[index];
            }

            if (!decl.typeParams.empty() &&
                !this->inferTypeArgs(decl.typeParams, decl.paramTypes, argTypes, subst))
                continue;

            bool match = true;
            for (size_t j = 0; j < argCount; ++j) {
                if (!this->isAssignableTo(
                        this->substituteType(decl.paramTypes[j], subst), argTypes[j])) {
                    match = false;
                    break;
                }
            }

            if (!match)
                continue;

            if (!this->typeParamBoundsSatisfied(decl.typeParams, subst)) {
                boundFailure = true;
                continue;
            }

            candidates.push_back({ i, std::move(subst) });
        }

        if (candidates.empty()) {
            if (boundFailure) {
                diagnostics.addError(node.loc,
                    "Type bound not satisfied for generic function '" + node.name + "'");
            } else if (explicitMismatch) {
                diagnostics.addError(node.loc,
                    "No matching generic function '" + node.name + "' for " +
                    std::to_string(explicitTypes.size()) + " explicit type argument(s)");
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
        node.paramTypes.clear();
        for (const auto& paramType : decl.paramTypes)
            node.paramTypes.push_back(this->substituteType(paramType, candidates[best].subst));

        for (size_t j = 0; j < argCount; ++j) {
            if (node.paramTypes[j].kind == TypeKind::Optional &&
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
                if (!node.typeArgs.empty())
                    diagnostics.addError(node.loc,
                        "Native class '" + node.className + "' does not accept type arguments");

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

        auto ctorOpt = this->findConstructor(cls);
        MethodDecl* ctor = ctorOpt ? &ctorOpt->method.get() : nullptr;

        TypeInfo type;
        type.kind = TypeKind::Object;
        type.className = cls.name;
        TypeSubst subst;

        if (!node.typeArgs.empty()) {
            if (node.typeArgs.size() != cls.typeParams.size()) {
                diagnostics.addError(node.loc,
                    cls.typeParams.empty()
                        ? "Class '" + cls.name + "' does not accept type arguments"
                        : "Class '" + cls.name + "' expects " +
                          std::to_string(cls.typeParams.size()) +
                          " type argument(s), got " + std::to_string(node.typeArgs.size()));
                return type;
            }

            for (const auto& typeArg : node.typeArgs)
                type.typeArgs.push_back(this->resolveTypeExpr(typeArg, node.loc, true));
        }

        for (size_t i = 0; i < cls.typeParams.size(); ++i) {
            subst[cls.typeParams[i].name] = i < type.typeArgs.size()
                ? type.typeArgs[i]
                : TypeInfo{ TypeKind::Unknown };
        }

        if (type.typeArgs.empty() && !cls.typeParams.empty() && ctor &&
            ctor->params.size() == argCount) {
            for (size_t j = 0; j < argCount; ++j)
                static_cast<void>(this->unifyType(ctor->paramTypes[j], argTypes[j], subst));

            type.typeArgs.clear();
            for (const auto& param : cls.typeParams) {
                auto it = subst.find(param.name);
                type.typeArgs.push_back(it == subst.end()
                    ? TypeInfo{ TypeKind::Unknown }
                    : it->second);
            }
        }

        if (!cls.typeParams.empty()) {
            if (type.typeArgs.size() != cls.typeParams.size()) {
                diagnostics.addError(node.loc,
                    "Class '" + cls.name + "' must provide all type arguments");
                return type;
            }

            static_cast<void>(this->checkTypeParamBounds(
                cls.typeParams, subst, node.loc, "class '" + cls.name + "'"));
        }

        if (!ctor) {
            if (argCount != 0)
                diagnostics.addError(node.loc, "Class '" + cls.name + "' has no constructor");

            return type;
        }

        if (ctor->params.size() != argCount) {
            diagnostics.addError(node.loc,
                "Constructor of class '" + cls.name + "' expects " +
                std::to_string(ctor->params.size()) + " argument(s), got " +
                std::to_string(argCount));

            return type;
        }

        node.paramTypes.clear();
        for (size_t j = 0; j < argCount; ++j) {
            const TypeInfo param = this->substituteType(ctor->paramTypes[j], subst);
            node.paramTypes.push_back(param);

            if (!this->isAssignableTo(param, argTypes[j])) {
                diagnostics.addError(node.loc,
                    "Type mismatch for constructor parameter '" + ctor->params[j].name +
                    "': expected " + typeToString(param) +
                    ", got " + typeToString(argTypes[j]));
            }

            if (param.kind == TypeKind::Optional &&
                argTypes[j].kind == TypeKind::Optional) {
                node.args[j]->preserveOptional = true;
            }
        }

        return type;
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

        TypeInfo classType;
        if (auto baseIt = this->classBaseTypes.find(cls.name); baseIt != this->classBaseTypes.end())
            classType = baseIt->second;

        if (classType.kind != TypeKind::Object) {
            classType.kind = TypeKind::Object;
            classType.className = cls.baseClassName;
        }

        const TypeSubst subst = this->substitutionFor(ctorOwner, classType);
        node.paramTypes.clear();
        for (const auto& paramType : ctor.paramTypes)
            node.paramTypes.push_back(this->substituteType(paramType, subst));

        if (ctor.params.size() != argCount) {
            diagnostics.addError(node.loc,
                "Base constructor of class '" + cls.baseClassName + "' expects " +
                std::to_string(ctor.params.size()) + " argument(s), got " +
                std::to_string(argCount));
            return {};
        }

        for (size_t j = 0; j < argCount; ++j) {
            if (!this->isAssignableTo(node.paramTypes[j], argTypes[j])) {
                diagnostics.addError(node.loc,
                    "Type mismatch for base constructor parameter '" + ctor.params[j].name +
                    "': expected " + typeToString(node.paramTypes[j]) +
                    ", got " + typeToString(argTypes[j]));
            }

            if (node.paramTypes[j].kind == TypeKind::Optional &&
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

    TypeInfo SemanticAnalyzer::checkMatch(MatchNode& node, MethodScope& scope) {
        TypeInfo target = this->checkExpr(*node.target, scope);

        if (node.target->type.kind == TypeKind::Optional)
            node.target->preserveOptional = true;

        if (node.arms.empty()) {
            diagnostics.addError(node.loc, "match requires at least one arm");
            return {};
        }

        bool covered = false;
        bool coversTrue = false;
        bool coversFalse = false;
        std::unordered_set<std::string> enumVariantsCovered;

        for (auto& arm : node.arms) {
            if (arm->isCatchAll())
                covered = true;

            if (arm->pattern == MatchArm::Pattern::EnumVariant && !arm->guard)
                enumVariantsCovered.insert(arm->variantName);

            if (arm->pattern == MatchArm::Pattern::Literal && !arm->guard) {
                if (auto* flag = std::get_if<bool>(&arm->literal))
                    (*flag ? coversTrue : coversFalse) = true;
            }
        }

        if (target.kind == TypeKind::Bool && coversTrue && coversFalse)
            covered = true;

        if (target.kind == TypeKind::Enum && !covered && !enumVariantsCovered.empty()) {
            std::unordered_set<std::string> declaredVariants;
            for (const auto& [name, info] : this->enumVariant) {
                if (info.enumName == target.className)
                    declaredVariants.insert(name);
            }

            if (declaredVariants.size() == enumVariantsCovered.size())
                covered = true;
        }

        if (!covered)
            diagnostics.addError(node.loc,
                "match must be exhaustive; add a binding or '_' arm");

        for (auto& arm : node.arms) {
            std::vector<TypeInfo> binderTypes;

            switch (arm->pattern) {
                case MatchArm::Pattern::Literal: {
                    if (std::holds_alternative<std::monostate>(arm->literal)) {
                        if (target.kind != TypeKind::Unknown && target.kind != TypeKind::None &&
                            target.kind != TypeKind::Optional)
                            diagnostics.addError(arm->loc,
                                "'None' pattern can only match an optional value");
                        break;
                    }

                    if (target.kind == TypeKind::Unknown)
                        break;

                    TypeKind literalKind = std::holds_alternative<std::string>(arm->literal)
                        ? TypeKind::String
                        : std::holds_alternative<bool>(arm->literal)
                            ? TypeKind::Bool
                            : std::holds_alternative<float>(arm->literal)
                                ? TypeKind::Float
                                : TypeKind::Int;

                    bool numericCompatible = (literalKind == TypeKind::Int || literalKind == TypeKind::Float) &&
                        (target.kind == TypeKind::Int || target.kind == TypeKind::Float);

                    if (literalKind != target.kind && !numericCompatible)
                        diagnostics.addError(arm->loc,
                            "Pattern type mismatch: cannot match " + typeInfoToString(target) +
                            " against a literal of a different type");
                    break;
                }
                case MatchArm::Pattern::Type:
                    if (!this->findClass(arm->className) && !isNativeClass(arm->className))
                        diagnostics.addError(arm->loc, "Unknown class: " + arm->className);
                    break;
                case MatchArm::Pattern::EnumVariant: {
                    if (target.kind == TypeKind::Unknown)
                        break;

                    if (target.kind != TypeKind::Enum) {
                        diagnostics.addError(arm->loc,
                            "Enum variant pattern '" + arm->variantName +
                            "' can only match an enum value, got " + typeInfoToString(target));
                        break;
                    }

                    auto it = this->enumVariant.find(arm->variantName);
                    if (it == this->enumVariant.end()) {
                        diagnostics.addError(arm->loc, "Unknown enum variant: " + arm->variantName);
                        break;
                    }

                    const EnumVariantInfo& info = it->second;
                    if (info.enumName != target.className) {
                        diagnostics.addError(arm->loc,
                            "Enum variant '" + arm->variantName +
                            "' does not belong to enum '" + target.className + "'");
                        break;
                    }

                    if (static_cast<int>(arm->binders.size()) != info.arity) {
                        diagnostics.addError(arm->loc,
                            "Enum variant '" + arm->variantName + "' has " +
                            std::to_string(info.arity) + " payload slot(s), but the pattern binds " +
                            std::to_string(arm->binders.size()));
                    }

                    TypeSubst enumSubst;
                    if (auto enumIt = this->enumNodes.find(info.enumName);
                        enumIt != this->enumNodes.end()) {
                        const TypeInfo normalized = this->enumTargetType(info, target);
                        const auto& params = enumIt->second.get().typeParams;
                        for (size_t index = 0; index < params.size(); ++index) {
                            enumSubst[params[index].name] = index < normalized.typeArgs.size()
                                ? normalized.typeArgs[index]
                                : TypeInfo{ TypeKind::Unknown };
                        }
                    }

                    for (const TypeInfo& payloadType : info.payloadTypes)
                        binderTypes.push_back(this->substituteType(payloadType, enumSubst));

                    break;
                }
                case MatchArm::Pattern::Bind:
                case MatchArm::Pattern::Wildcard:
                    break;
            }

            this->blockScopes.emplace_back();
            auto popArmScope = make_scope_guard([this] { this->blockScopes.pop_back(); });

            if (!arm->binder.empty())
                this->blockScopes.back()[arm->binder] = arm->pattern == MatchArm::Pattern::Type
                    ? TypeInfo{ TypeKind::Object, arm->className }
                    : target;

            for (size_t i = 0; i < arm->binders.size(); ++i)
                this->blockScopes.back()[arm->binders[i]] =
                    i < binderTypes.size() ? binderTypes[i] : TypeInfo{ TypeKind::Unknown };

            if (arm->guard)
                this->checkExpr(*arm->guard, scope);

            if (arm->body)
                this->checkStatement(*arm->body, scope);
        }

        return {};
    }

    TypeInfo SemanticAnalyzer::checkLetPattern(LetPatternNode& node, MethodScope& scope) {
        if (!node.value)
            return { TypeKind::Bool };

        TypeInfo value = this->checkExpr(*node.value, scope);

        if (!node.isEnumPattern)
            return { TypeKind::Bool };

        if (value.kind != TypeKind::Enum && value.kind != TypeKind::Unknown) {
            this->diagnostics.addError(node.loc,
                "Enum variant pattern '" + node.variantName +
                "' can only match an enum value, got " + typeInfoToString(value));
            return { TypeKind::Bool };
        }

        if (value.kind == TypeKind::Unknown)
            return { TypeKind::Bool };

        auto it = this->enumVariant.find(node.variantName);
        if (it == this->enumVariant.end()) {
            this->diagnostics.addError(node.loc, "Unknown enum variant: " + node.variantName);
            return { TypeKind::Bool };
        }

        const EnumVariantInfo& info = it->second;
        if (info.enumName != value.className) {
            this->diagnostics.addError(node.loc,
                "Enum variant '" + node.variantName + "' does not belong to enum '" +
                value.className + "'");
            return { TypeKind::Bool };
        }

        if (static_cast<int>(node.binders.size()) != info.arity) {
            this->diagnostics.addError(node.loc,
                "Enum variant '" + node.variantName + "' has " +
                std::to_string(info.arity) + " payload slot(s), but the pattern binds " +
                std::to_string(node.binders.size()));
        }

        return { TypeKind::Bool };
    }

    void SemanticAnalyzer::bindLetPatternTypes(LetPatternNode& node) {
        const auto variantIt = node.isEnumPattern ? this->enumVariant.find(node.variantName) : this->enumVariant.end();

        TypeSubst enumSubst;
        if (variantIt != this->enumVariant.end()) {
            if (auto enumIt = this->enumNodes.find(variantIt->second.enumName);
                enumIt != this->enumNodes.end()) {
                const TypeInfo valueType = node.value ? node.value->type : TypeInfo{};
                const TypeInfo normalized = this->enumTargetType(variantIt->second, valueType);
                const auto& params = enumIt->second.get().typeParams;
                for (size_t index = 0; index < params.size(); ++index) {
                    enumSubst[params[index].name] = index < normalized.typeArgs.size()
                        ? normalized.typeArgs[index]
                        : TypeInfo{ TypeKind::Unknown };
                }
            }
        }

        for (size_t i = 0; i < node.binders.size(); ++i) {
            this->blockScopes.back()[node.binders[i]] =
                variantIt != this->enumVariant.end() && i < variantIt->second.payloadTypes.size()
                    ? this->substituteType(variantIt->second.payloadTypes[i], enumSubst)
                    : TypeInfo{ TypeKind::Unknown };
        }
    }

    std::optional<OperatorOverload> SemanticAnalyzer::resolveOperatorOverload(
        const std::string& op, const TypeInfo& receiver) {
        if (!operatorTraitFor(op))
            return std::nullopt;
        if (receiver.kind != TypeKind::Object)
            return std::nullopt;

        const std::string methodName = *operatorMethodFor(op);
        std::optional<std::reference_wrapper<ClassNode>> walk =
            receiver.className.empty() ? std::nullopt : this->findClass(receiver.className);
        if (!walk)
            return std::nullopt;

        bool unary = op == "neg";
        for (size_t step = 0; step <= this->classes.size() && walk; ++step) {
            ClassNode& cls = walk->get();
            for (auto& method : cls.methods) {
                if (method.isConstructor || method.isStatic || method.name != methodName)
                    continue;
                if (unary ? method.params.empty() : method.params.size() == 1) {
                    OperatorOverload result;
                    result.className = cls.name;
                    result.methodName = methodName;
                    result.methodOrdinal = this->methodOrdinal(cls.name, this->methodSignature(method));
                    result.returnType = method.returnType;
                    return result;
                }
            }

            if (cls.baseClassName.empty())
                break;

            auto baseOpt = this->findClass(cls.baseClassName);
            if (!baseOpt)
                break;
            walk = baseOpt;
        }

        return std::nullopt;
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
        return methodSignatureString(method);
    }

}
