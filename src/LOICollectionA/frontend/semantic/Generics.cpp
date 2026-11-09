#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "LOICollectionA/frontend/SemanticAnalyzer.h"

#include "LOICollectionA/frontend/semantic/Helpers.h"

namespace LOICollection::frontend {
    TypeInfo SemanticAnalyzer::selfType(const ClassNode& cls) const {
        TypeInfo type;
        type.kind = TypeKind::Object;
        type.className = cls.name;
        for (const auto& param : cls.typeParams) {
            TypeInfo argument;
            argument.kind = TypeKind::Generic;
            argument.typeVar = param.name;
            type.typeArgs.push_back(std::move(argument));
        }
        return type;
    }

    SemanticAnalyzer::TypeSubst SemanticAnalyzer::substitutionFor(
        const ClassNode& owner, const TypeInfo& receiver) const {
        TypeSubst subst;

        auto clsOpt = this->findClass(receiver.className);
        if (!clsOpt)
            return subst;

        ClassNode* current = &clsOpt->get();
        for (size_t index = 0; index < current->typeParams.size(); ++index) {
            const TypeInfo* argument = index < receiver.typeArgs.size() ? &receiver.typeArgs[index] : nullptr;
            if (argument && argument->kind != TypeKind::Unknown) {
                subst[current->typeParams[index].name] = *argument;
                continue;
            }

            TypeInfo placeholder;
            placeholder.kind = TypeKind::Generic;
            placeholder.typeVar = current->typeParams[index].name;
            subst[current->typeParams[index].name] = std::move(placeholder);
        }

        for (size_t step = 0; step <= this->classes.size() && current != &owner; ++step) {
            if (current->baseClassName.empty())
                break;

            auto baseOpt = this->findClass(current->baseClassName);
            if (!baseOpt)
                break;

            ClassNode& base = baseOpt->get();
            TypeInfo baseType;
            baseType.kind = TypeKind::Object;
            baseType.className = base.name;

            if (auto it = this->classBaseTypes.find(current->name); it != this->classBaseTypes.end())
                baseType = it->second;

            TypeSubst next;
            for (size_t index = 0; index < base.typeParams.size(); ++index) {
                const TypeInfo argument = index < baseType.typeArgs.size()
                    ? baseType.typeArgs[index]
                    : TypeInfo{ TypeKind::Unknown };
                next[base.typeParams[index].name] = this->substituteType(argument, subst);
            }

            subst = std::move(next);
            current = &base;
        }

        return subst;
    }

    bool SemanticAnalyzer::unifyType(
        const TypeInfo& pattern, const TypeInfo& actual, TypeSubst& subst) const {
        if (pattern.kind == TypeKind::Unknown || actual.kind == TypeKind::Unknown)
            return true;

        if (pattern.kind == TypeKind::Generic) {
            auto it = subst.find(pattern.typeVar);
            if (it != subst.end() && it->second.kind != TypeKind::Unknown)
                return sameTypeIgnoringOwnership(it->second, actual);

            subst[pattern.typeVar] = withoutOwnership(actual);
            return true;
        }

        if (pattern.kind == TypeKind::Optional) {
            if (actual.kind == TypeKind::Optional)
                return pattern.optionalInner && actual.optionalInner &&
                    this->unifyType(*pattern.optionalInner, *actual.optionalInner, subst);

            if (actual.kind == TypeKind::None)
                return true;

            return pattern.optionalInner && this->unifyType(*pattern.optionalInner, actual, subst);
        }

        if (pattern.kind == TypeKind::Variant) {
            if (actual.kind == TypeKind::Variant) {
                if (pattern.variantOptions.size() != actual.variantOptions.size())
                    return false;

                TypeSubst trial = subst;
                for (size_t index = 0; index < pattern.variantOptions.size(); ++index) {
                    if (!this->unifyType(pattern.variantOptions[index], actual.variantOptions[index], trial))
                        return false;
                }

                subst = std::move(trial);
                return true;
            }

            for (const TypeInfo& option : pattern.variantOptions) {
                TypeSubst trial = subst;
                if (this->unifyType(option, actual, trial)) {
                    subst = std::move(trial);
                    return true;
                }
            }

            return false;
        }

        if (pattern.kind == TypeKind::Object && actual.kind == TypeKind::Object &&
            pattern.className != actual.className) {
            if (!this->isDerived(actual.className, pattern.className))
                return false;

            auto clsOpt = this->findClass(pattern.className);
            if (!clsOpt)
                return false;

            const TypeSubst baseSubst = this->substitutionFor(clsOpt->get(), actual);
            if (pattern.typeArgs.size() != clsOpt->get().typeParams.size())
                return false;

            TypeSubst trial = subst;
            for (size_t index = 0; index < pattern.typeArgs.size(); ++index) {
                auto bound = baseSubst.find(clsOpt->get().typeParams[index].name);
                if (bound == baseSubst.end())
                    continue;

                if (!this->unifyType(pattern.typeArgs[index], bound->second, trial))
                    return false;
            }

            subst = std::move(trial);
            return true;
        }

        if (pattern.kind == TypeKind::Object || pattern.kind == TypeKind::Enum) {
            if (pattern.kind != actual.kind || pattern.className != actual.className)
                return false;

            if (pattern.typeArgs.size() != actual.typeArgs.size())
                return false;

            TypeSubst trial = subst;
            for (size_t index = 0; index < pattern.typeArgs.size(); ++index) {
                if (!this->unifyType(pattern.typeArgs[index], actual.typeArgs[index], trial))
                    return false;
            }

            subst = std::move(trial);
            return true;
        }

        if (pattern.kind == TypeKind::Array)
            return actual.kind == TypeKind::Array;

        if (pattern.kind == TypeKind::Trait)
            return this->satisfiesTrait(actual, pattern.className);

        return sameTypeIgnoringOwnership(pattern, actual);
    }

    bool SemanticAnalyzer::inferTypeArgs(
        const std::vector<TypeParam>& typeParams,
        const std::vector<TypeInfo>& patterns,
        const std::vector<TypeInfo>& actuals,
        TypeSubst& subst) const {
        for (const auto& param : typeParams)
            subst.try_emplace(param.name, TypeInfo{ TypeKind::Unknown });

        const size_t count = std::min(patterns.size(), actuals.size());
        for (size_t index = 0; index < count; ++index) {
            if (!this->unifyType(patterns[index], actuals[index], subst))
                return false;
        }

        return true;
    }

    bool SemanticAnalyzer::typeParamBoundsSatisfied(
        const std::vector<TypeParam>& typeParams, const TypeSubst& subst) const {
        for (const auto& param : typeParams) {
            if (param.bounds.empty())
                continue;

            auto it = subst.find(param.name);
            if (it == subst.end() || it->second.kind == TypeKind::Unknown)
                continue;

            for (const std::string& bound : param.bounds) {
                if (!this->satisfiesTrait(it->second, bound))
                    return false;
            }
        }

        return true;
    }

    bool SemanticAnalyzer::checkTypeParamBounds(
        const std::vector<TypeParam>& typeParams,
        const TypeSubst& subst,
        SourceLocation loc,
        const std::string& what) const {
        bool satisfied = true;

        for (const auto& param : typeParams) {
            if (param.bounds.empty())
                continue;

            auto it = subst.find(param.name);
            if (it == subst.end() || it->second.kind == TypeKind::Unknown)
                continue;

            for (const std::string& bound : param.bounds) {
                if (this->satisfiesTrait(it->second, bound))
                    continue;

                this->diagnostics.addError(loc,
                    "Type bound '" + bound + "' not satisfied for type argument '" +
                    this->typeToString(it->second) + "' of type parameter '" + param.name +
                    "' (" + what + ")");
                satisfied = false;
            }
        }

        return satisfied;
    }

    bool SemanticAnalyzer::typeParamBoundSatisfied(
        const std::string& param, const std::string& traitName) const {
        auto it = this->activeTypeParamBounds.find(param);
        if (it == this->activeTypeParamBounds.end())
            return false;

        return std::ranges::find(it->second, traitName) != it->second.end();
    }

    bool SemanticAnalyzer::typeArgumentsCompatible(const TypeInfo& target, const TypeInfo& from) const {
        std::function<bool(const TypeInfo&, const TypeInfo&)> compatible =
            [&compatible](const TypeInfo& left, const TypeInfo& right) -> bool {
                if (left.kind == TypeKind::Unknown || right.kind == TypeKind::Unknown)
                    return true;

                if (left.kind == TypeKind::Generic || right.kind == TypeKind::Generic)
                    return true;

                if (sameTypeIgnoringOwnership(left, right))
                    return true;

                if (left.kind == TypeKind::Optional && right.kind == TypeKind::Optional)
                    return (!left.optionalInner && !right.optionalInner) ||
                        (left.optionalInner && right.optionalInner &&
                         compatible(*left.optionalInner, *right.optionalInner));

                if (left.kind == TypeKind::Variant && right.kind == TypeKind::Variant) {
                    if (left.variantOptions.size() != right.variantOptions.size())
                        return false;

                    for (size_t index = 0; index < left.variantOptions.size(); ++index) {
                        if (!compatible(left.variantOptions[index], right.variantOptions[index]))
                            return false;
                    }

                    return true;
                }

                if ((left.kind == TypeKind::Object && right.kind == TypeKind::Object) ||
                    (left.kind == TypeKind::Enum && right.kind == TypeKind::Enum)) {
                    if (left.className != right.className ||
                        left.typeArgs.size() != right.typeArgs.size())
                        return false;

                    for (size_t index = 0; index < left.typeArgs.size(); ++index) {
                        if (!compatible(left.typeArgs[index], right.typeArgs[index]))
                            return false;
                    }

                    return true;
                }

                return false;
            };

        return compatible(target, from);
    }

    TypeInfo SemanticAnalyzer::resolveClassType(
        ClassNode& cls, const std::vector<TypeExpr>& args, SourceLocation loc, bool reportError) {
        TypeInfo type;
        type.kind = TypeKind::Object;
        type.className = cls.name;

        for (const auto& argument : args)
            type.typeArgs.push_back(this->resolveTypeExpr(argument, loc, reportError));

        if (args.size() != cls.typeParams.size()) {
            if (reportError) {
                if (cls.typeParams.empty()) {
                    this->diagnostics.addError(loc,
                        "Class '" + cls.name + "' does not accept type arguments");
                } else {
                    this->diagnostics.addError(loc,
                        "Class '" + cls.name + "' expects " +
                        std::to_string(cls.typeParams.size()) +
                        " type argument(s), got " + std::to_string(args.size()));
                }
            }

            return type;
        }

        TypeSubst subst;
        for (size_t index = 0; index < cls.typeParams.size(); ++index)
            subst[cls.typeParams[index].name] = type.typeArgs[index];

        if (reportError)
            static_cast<void>(this->checkTypeParamBounds(cls.typeParams, subst, loc,
                "class '" + cls.name + "'"));

        return type;
    }

    TypeInfo SemanticAnalyzer::resolveEnumType(
        EnumDeclNode& node, const std::vector<TypeExpr>& args, SourceLocation loc, bool reportError) {
        TypeInfo type;
        type.kind = TypeKind::Enum;
        type.className = node.name;

        for (const auto& argument : args)
            type.typeArgs.push_back(this->resolveTypeExpr(argument, loc, reportError));

        if (args.size() != node.typeParams.size()) {
            if (reportError) {
                if (node.typeParams.empty()) {
                    this->diagnostics.addError(loc,
                        "Enum '" + node.name + "' does not accept type arguments");
                } else {
                    this->diagnostics.addError(loc,
                        "Enum '" + node.name + "' expects " +
                        std::to_string(node.typeParams.size()) +
                        " type argument(s), got " + std::to_string(args.size()));
                }
            }

            return type;
        }

        TypeSubst subst;
        for (size_t index = 0; index < node.typeParams.size(); ++index)
            subst[node.typeParams[index].name] = type.typeArgs[index];

        if (reportError)
            static_cast<void>(this->checkTypeParamBounds(node.typeParams, subst, loc,
                "enum '" + node.name + "'"));

        return type;
    }

    void SemanticAnalyzer::validateTypeParamBounds(
        const std::vector<TypeParam>& typeParams, SourceLocation loc) {
        for (const auto& param : typeParams) {
            for (const std::string& bound : param.bounds) {
                if (!this->traits.contains(bound)) {
                    this->diagnostics.addError(loc,
                        "Unknown trait bound '" + bound + "' for type parameter '" + param.name + "'");
                }
            }
        }
    }

    void SemanticAnalyzer::pushTypeParams(const std::vector<TypeParam>& typeParams) {
        for (const auto& param : typeParams) {
            TypeInfo argument;
            argument.kind = TypeKind::Generic;
            argument.typeVar = param.name;
            this->activeTypeParams[param.name] = std::move(argument);
            this->activeTypeParamBounds[param.name] = param.bounds;
        }
    }

    TypeInfo SemanticAnalyzer::enumTargetType(const EnumVariantInfo& info, const TypeInfo& value) const {
        if (value.kind != TypeKind::Enum || value.className != info.enumName)
            return value;

        auto nodeIt = this->enumNodes.find(info.enumName);
        if (nodeIt == this->enumNodes.end())
            return value;

        TypeInfo result = value;
        const auto& params = nodeIt->second.get().typeParams;
        while (result.typeArgs.size() < params.size())
            result.typeArgs.push_back(TypeInfo{ TypeKind::Unknown });

        return result;
    }
}
