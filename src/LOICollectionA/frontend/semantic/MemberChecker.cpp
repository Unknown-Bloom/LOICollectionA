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

    TypeInfo SemanticAnalyzer::checkMemberAccess(MemberAccessNode& node, MethodScope& scope) {
        if (node.target->getType() == ASTNode::Type::Variable) {
            auto& var = static_cast<VariableNode&>(*node.target);
            if (auto clsOpt = this->findClass(var.name)) {
                if (!node.isColonColon) {
                    const bool isStaticMember =
                        static_cast<bool>(this->findStaticField(clsOpt->get(), node.memberName)) ||
                        static_cast<bool>(this->findStaticMethod(
                            clsOpt->get(), node.memberName, std::vector<TypeInfo>{}, scope));
                    diagnostics.addError(node.loc,
                        isStaticMember
                            ? "Static member '" + node.memberName + "' of class '" + clsOpt->get().name +
                              "' must be accessed with '::' (found '" + clsOpt->get().name + "." + node.memberName + "')"
                            : "Class '" + clsOpt->get().name + "' has no static member '" + node.memberName + "'");
                    return {};
                }

                auto field = this->findStaticField(clsOpt->get(), node.memberName);
                if (!field) {
                    diagnostics.addError(node.loc,
                        "Class '" + clsOpt->get().name + "' has no static member '" +
                        node.memberName + "'");
                    return {};
                }

                ClassMember& m = field->member.get();
                if (m.isPrivate &&
                    !(scope.hasMethod() && &scope.classRef() == &field->owner.get())) {
                    diagnostics.addError(node.loc,
                        "Cannot access private member '" + m.name + "' of class '" +
                        field->owner.get().name + "'");
                }

                TypeInfo classType;
                if (scope.hasClass() && &scope.classRef() == &clsOpt->get()) {
                    classType = this->selfType(clsOpt->get());
                } else {
                    classType = this->resolveClassType(
                        clsOpt->get(), node.typeArgs, node.loc, true);
                }

                const TypeSubst subst = this->substitutionFor(field->owner.get(), classType);
                node.isStaticAccess = true;
                node.staticClassName = field->owner.get().name;
                return this->substituteType(m.type, subst);
            }

            if (isNativeClass(var.name)) {
                if (!node.isColonColon) {
                    diagnostics.addError(node.loc,
                        "Native class '" + var.name + "' members must be accessed with '::' (found '" +
                        var.name + "." + node.memberName + "')");
                    return {};
                }

                if (!node.typeArgs.empty())
                    diagnostics.addError(node.loc,
                        "Native class '" + var.name + "' does not accept type arguments");

                if (ClassCall::getInstance().hasStaticField(var.name, node.memberName)) {
                    node.isStaticAccess = true;
                    node.staticClassName = var.name;
                    return {};
                }

                diagnostics.addError(node.loc,
                    "Native class '" + var.name + "' has no static member '" +
                    node.memberName + "'");
                return {};
            }
        }

        TypeInfo targetType = checkExpr(*node.target, scope);

        bool safeMaybeNone = false;
        if (node.isSafe) {
            
            node.target->preserveOptional = true;
            safeMaybeNone = targetType.kind == TypeKind::Optional ||
                            targetType.kind == TypeKind::Unknown;
        }

        TypeInfo result = this->checkMemberAccessImpl(node, scope, targetType);

        if (!node.isSafe || !safeMaybeNone)
            return result;

        if (result.kind == TypeKind::Unknown || result.kind == TypeKind::None)
            return {};

        node.preserveOptional = true;
        TypeInfo optional;
        optional.kind = TypeKind::Optional;
        optional.optionalInner = std::make_shared<TypeInfo>(result);
        return optional;
    }

    TypeInfo SemanticAnalyzer::checkMemberAccessImpl(MemberAccessNode& node, MethodScope& scope, TypeInfo targetType) {
        if (targetType.kind == TypeKind::Unknown)
            return {};

        if (targetType.kind == TypeKind::Variant || targetType.kind == TypeKind::Optional) {
            static const std::unordered_map<std::string, MemberAccessNode::MemberKind> conventionMembers = {
                { "type", MemberAccessNode::MemberKind::TypeOf },
                { "value", MemberAccessNode::MemberKind::Value },
                { "has_value", MemberAccessNode::MemberKind::HasValue },
            };

            auto it = conventionMembers.find(node.memberName);
            if (it != conventionMembers.end()) {
                node.target->preserveOptional = true;

                switch (it->second) {
                    case MemberAccessNode::MemberKind::TypeOf:
                        node.memberKind = it->second;
                        return { TypeKind::String };

                    case MemberAccessNode::MemberKind::Value: {
                        node.memberKind = it->second;
                        if (targetType.kind == TypeKind::Optional)
                            return *targetType.optionalInner;

                        TypeInfo result;
                        for (const auto& option : targetType.variantOptions) {
                            if (result.kind == TypeKind::Unknown)
                                result = option;
                            else if (!(result == option))
                                return {};
                        }
                        return result;
                    }

                    case MemberAccessNode::MemberKind::HasValue:
                        if (targetType.kind != TypeKind::Optional) {
                            this->diagnostics.addError(node.loc,
                                "'.has_value' is only available on optional values");
                            return {};
                        }
                        node.memberKind = it->second;
                        return { TypeKind::Bool };

                    default: break;
                }
            }

            if (targetType.kind == TypeKind::Variant) {
                this->diagnostics.addError(node.loc,
                    "Variant has no member '" + node.memberName + "'");
                return {};
            }
        }

        while (targetType.kind == TypeKind::Optional)
            targetType = *targetType.optionalInner;

        if (targetType.kind == TypeKind::Array) {
            diagnostics.addError(node.loc, "Array has no member '" + node.memberName + "'");
            return {};
        }

        if (targetType.kind == TypeKind::String) {
            diagnostics.addError(node.loc, "String has no member '" + node.memberName + "'");
            return {};
        }

        if (targetType.kind != TypeKind::Object) {
            diagnostics.addError(node.loc, "Cannot access member of a non-object value");
            return {};
        }

        auto clsOpt = findClass(targetType.className);
        if (!clsOpt) {
            if (isNativeClass(targetType.className)) {
                if (!ClassCall::getInstance().hasField(targetType.className, node.memberName)) {
                    diagnostics.addError(node.loc,
                        "Class '" + targetType.className + "' has no member '" + node.memberName + "'");
                }

                return {};
            }

            diagnostics.addError(node.loc, "Unknown class: " + targetType.className);
            return {};
        }

        ClassNode& cls = clsOpt->get();
        if (auto field = findField(cls, node.memberName)) {
            ClassMember& m = field->member.get();
            if (m.isPrivate && !(scope.hasMethod() && &scope.classRef() == &field->owner.get())) {
                diagnostics.addError(node.loc,
                    "Cannot access private member '" + m.name + "' of class '" + field->owner.get().name + "'");
            }

            const TypeSubst subst = this->substitutionFor(field->owner.get(), targetType);
            return this->substituteType(m.type, subst);
        }

        diagnostics.addError(node.loc,
            "Class '" + cls.name + "' has no member '" + node.memberName + "'");
        return {};
    }

    int SemanticAnalyzer::methodOrdinal(const std::string& className, const std::string& signature) const {
        auto classIt = this->classMethodOrdinals.find(className);
        if (classIt == this->classMethodOrdinals.end())
            return -1;

        auto sigIt = classIt->second.find(signature);
        if (sigIt == classIt->second.end())
            return -1;

        return sigIt->second;
    }

    bool SemanticAnalyzer::isDerived(const std::string& derivedName, const std::string& baseName) const {
        if (derivedName == baseName)
            return true;

        std::string current = derivedName;
        for (size_t step = 0; step <= this->classes.size() && !current.empty(); ++step) {
            auto clsOpt = this->findClass(current);
            if (!clsOpt)
                return false;

            ClassNode& cls = clsOpt->get();
            if (cls.baseClassName == baseName)
                return true;

            current = cls.baseClassName;
        }

        return false;
    }

    std::optional<SemanticAnalyzer::FieldRef> SemanticAnalyzer::findField(ClassNode& cls, const std::string& name) const {
        std::optional<std::reference_wrapper<ClassNode>> current = std::ref(cls);
        for (size_t step = 0; step <= this->classes.size() && current; ++step) {
            ClassNode& clsRef = current->get();
            for (auto& member : clsRef.members) {
                if (!member.isStatic && member.name == name) {
                    return FieldRef{ std::ref(member), std::ref(clsRef) };
                }
            }

            if (clsRef.baseClassName.empty())
                break;

            auto baseOpt = this->findClass(clsRef.baseClassName);
            if (!baseOpt)
                break;

            current = baseOpt;
        }

        return std::nullopt;
    }

    std::optional<SemanticAnalyzer::ConstructorRef> SemanticAnalyzer::findConstructor(ClassNode& cls) const {
        std::optional<std::reference_wrapper<ClassNode>> current = std::ref(cls);
        for (size_t step = 0; step <= this->classes.size() && current; ++step) {
            ClassNode& clsRef = current->get();
            for (auto& method : clsRef.methods) {
                if (method.isConstructor) {
                    return ConstructorRef{ std::ref(method), std::ref(clsRef) };
                }
            }

            if (clsRef.baseClassName.empty())
                break;

            auto baseOpt = this->findClass(clsRef.baseClassName);
            if (!baseOpt)
                break;

            current = baseOpt;
        }

        return std::nullopt;
    }

    std::optional<SemanticAnalyzer::FieldRef> SemanticAnalyzer::findStaticField(ClassNode& cls, const std::string& name) const {
        std::optional<std::reference_wrapper<ClassNode>> current = std::ref(cls);
        for (size_t step = 0; step <= this->classes.size() && current; ++step) {
            ClassNode& clsRef = current->get();
            for (auto& member : clsRef.members) {
                if (member.isStatic && member.name == name) {
                    return FieldRef{ std::ref(member), std::ref(clsRef) };
                }
            }

            if (clsRef.baseClassName.empty())
                break;

            auto baseOpt = this->findClass(clsRef.baseClassName);
            if (!baseOpt)
                break;

            current = baseOpt;
        }

        return std::nullopt;
    }

    std::optional<SemanticAnalyzer::StaticMethodRef> SemanticAnalyzer::findStaticMethod(
        ClassNode& cls, const std::string& name, const std::vector<TypeInfo>& argTypes,
        const MethodScope& scope, const TypeInfo& classType,
        const std::vector<TypeInfo>& methodTypeArgs
    ) const {
        struct Cand {
            std::reference_wrapper<ClassNode> owner;
            std::reference_wrapper<MethodDecl> method;
            TypeSubst subst;
        };

        TypeInfo receiver = classType;
        if (receiver.kind != TypeKind::Object || receiver.className.empty()) {
            receiver.kind = TypeKind::Object;
            receiver.className = cls.name;
        }

        std::vector<Cand> candidates;
        std::optional<std::reference_wrapper<ClassNode>> walk = std::ref(cls);

        for (size_t step = 0; step <= this->classes.size() && walk; ++step) {
            ClassNode& current = walk->get();
            const TypeSubst ownerSubst = this->substitutionFor(current, receiver);

            for (auto& method : current.methods) {
                if (method.isConstructor || !method.isStatic || method.name != name)
                    continue;
                if (method.params.size() != argTypes.size())
                    continue;
                if (method.isPrivate &&
                    !(scope.hasClass() && scope.hasMethod() && &scope.classRef() == &current)) {
                    continue;
                }

                TypeSubst subst = ownerSubst;
                if (!methodTypeArgs.empty()) {
                    if (method.typeParams.size() != methodTypeArgs.size())
                        continue;

                    for (size_t index = 0; index < method.typeParams.size(); ++index)
                        subst[method.typeParams[index].name] = methodTypeArgs[index];
                }

                if (!method.typeParams.empty() &&
                    !this->inferTypeArgs(method.typeParams, method.paramTypes, argTypes, subst))
                    continue;

                bool match = true;
                for (size_t j = 0; j < argTypes.size(); ++j) {
                    const TypeInfo param = this->substituteType(method.paramTypes[j], subst);
                    if (!this->isAssignableTo(param, argTypes[j])) {
                        match = false;
                        break;
                    }
                }

                if (!match || !this->typeParamBoundsSatisfied(method.typeParams, subst))
                    continue;

                candidates.push_back({ std::ref(current), std::ref(method), std::move(subst) });
            }

            if (current.baseClassName.empty())
                break;

            auto baseOpt = this->findClass(current.baseClassName);
            if (!baseOpt)
                break;

            walk = baseOpt;
        }

        if (candidates.empty())
            return std::nullopt;

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

        return StaticMethodRef{ candidates[best].method, candidates[best].owner,
            std::move(candidates[best].subst) };
    }

    int SemanticAnalyzer::staticMethodOrdinal(const std::string& className, const std::string& signature) const {
        auto classIt = this->classStaticMethodOrdinals.find(className);
        if (classIt == this->classStaticMethodOrdinals.end())
            return -1;

        auto sigIt = classIt->second.find(signature);
        if (sigIt == classIt->second.end())
            return -1;

        return sigIt->second;
    }

}
