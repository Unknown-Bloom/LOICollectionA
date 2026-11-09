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

    TypeInfo SemanticAnalyzer::typeOfValue(const ValueNode::ValueType& value) const {
        switch (value.index()) {
            case 0: return { TypeKind::Int };
            case 1: return { TypeKind::Float };
            case 2: return { TypeKind::String };
            case 3: return { TypeKind::Bool };
            case 4: return { TypeKind::Object };
            case 5: return { TypeKind::Function };
            case 6: return { TypeKind::Array };
            case 7: return { TypeKind::None };
            default: return {};
        }
    }

    TypeInfo SemanticAnalyzer::typeFromName(const std::string& name, SourceLocation loc, bool reportError) const {
        if (auto it = this->activeTypeParams.find(name); it != this->activeTypeParams.end())
            return it->second;

        if (auto it = basicTypes().find(name); it != basicTypes().end())
            return { it->second };

        if (auto cls = findClass(name))
            return { TypeKind::Object, cls->get().name };

        if (isNativeClass(name))
            return { TypeKind::Object, name };

        if (this->enumByName.contains(name))
            return { TypeKind::Enum, name };

        if (reportError)
            diagnostics.addError(loc, "Unknown type: " + name);

        return {};
    }

    std::string SemanticAnalyzer::typeToString(const TypeInfo& type) const {
        return typeInfoToString(type);
    }

    bool SemanticAnalyzer::isNumeric(const TypeInfo& type) const {
        return type.kind == TypeKind::Int || type.kind == TypeKind::Float;
    }

    TypeInfo SemanticAnalyzer::substituteType(
        const TypeInfo& type, const std::unordered_map<std::string, TypeInfo>& subst) const {
        if (type.kind == TypeKind::Generic) {
            auto it = subst.find(type.typeVar);
            if (it != subst.end() && it->second.kind != TypeKind::Unknown &&
                it->second.kind != TypeKind::None) {
                TypeInfo resolved = it->second;
                if (type.ownership != Ownership::Value && isReferenceType(resolved))
                    resolved.ownership = type.ownership;
                return resolved;
            }
            return type;
        }

        TypeInfo result = type;
        if (type.optionalInner)
            result.optionalInner = std::make_shared<TypeInfo>(
                substituteType(*type.optionalInner, subst));
        for (size_t i = 0; i < result.variantOptions.size(); ++i)
            result.variantOptions[i] = substituteType(result.variantOptions[i], subst);
        for (auto& arg : result.typeArgs)
            arg = substituteType(arg, subst);
        return result;
    }

    bool SemanticAnalyzer::satisfiesTrait(const TypeInfo& type, const std::string& traitName) const {
        if (type.kind == TypeKind::Generic)
            return this->typeParamBoundSatisfied(type.typeVar, traitName);

        if (type.kind == TypeKind::Trait)
            return type.className == traitName;

        static const std::unordered_map<std::string, std::vector<TypeKind>> builtinTraits = {
            { "Add", { TypeKind::Int, TypeKind::Float, TypeKind::String } },
            { "Sub", { TypeKind::Int, TypeKind::Float } },
            { "Mul", { TypeKind::Int, TypeKind::Float } },
            { "Div", { TypeKind::Int, TypeKind::Float } },
            { "Mod", { TypeKind::Int, TypeKind::Float } },
            { "Pow", { TypeKind::Int, TypeKind::Float } },
            { "Neg", { TypeKind::Int, TypeKind::Float } },
            { "Eq", { TypeKind::Int, TypeKind::Float, TypeKind::String, TypeKind::Bool } },
            { "Ne", { TypeKind::Int, TypeKind::Float, TypeKind::String, TypeKind::Bool } },
            { "Lt", { TypeKind::Int, TypeKind::Float, TypeKind::String } },
            { "Le", { TypeKind::Int, TypeKind::Float, TypeKind::String } },
            { "Gt", { TypeKind::Int, TypeKind::Float, TypeKind::String } },
            { "Ge", { TypeKind::Int, TypeKind::Float, TypeKind::String } },
        };

        if (auto builtin = builtinTraits.find(traitName); builtin != builtinTraits.end()) {
            if (std::ranges::find(builtin->second, type.kind) != builtin->second.end())
                return true;
        }

        auto traitIt = this->traits.find(traitName);
        if (traitIt == this->traits.end())
            return false;

        if (type.kind != TypeKind::Object)
            return false;

        auto clsOpt = this->findClass(type.className);
        if (!clsOpt)
            return false;

        std::optional<std::reference_wrapper<ClassNode>> walk = clsOpt;
        for (size_t step = 0; step <= this->classes.size() && walk; ++step) {
            ClassNode& cls = walk->get();
            for (const auto& reqMethod : traitIt->second) {
                bool found = false;
                for (const auto& m : cls.methods) {
                    if (m.name != reqMethod.name || m.params.size() != reqMethod.paramCount)
                        continue;
                    found = true;
                    break;
                }
                if (!found)
                    return false;
            }

            if (cls.baseClassName.empty())
                break;

            walk = this->findClass(cls.baseClassName);
        }

        return true;
    }

    bool SemanticAnalyzer::isAssignableTo(const TypeInfo& target, const TypeInfo& from) const {
        if (target.kind == TypeKind::Unknown ||
            from.kind == TypeKind::Unknown ||
            from.kind == TypeKind::Void) {
            return true;
        }

        if (from.kind == TypeKind::None)
            return target.kind == TypeKind::Optional;

        if (effectiveOwnership(target) != effectiveOwnership(from))
            return false;

        if (from.kind == TypeKind::Optional) {
            if (target.kind == TypeKind::Optional)
                return this->isAssignableTo(*target.optionalInner, *from.optionalInner);

            if (target.kind == TypeKind::Variant) {
                for (const auto& option : target.variantOptions) {
                    if (this->isAssignableTo(option, from))
                        return true;
                }
            }

            return false;
        }

        if (target.kind == TypeKind::None)
            return false;

        if (target.kind == TypeKind::Optional)
            return this->isAssignableTo(*target.optionalInner, from);

        if (target.kind == TypeKind::Variant) {
            return std::ranges::any_of(target.variantOptions, [this, from](const TypeInfo& option) -> bool {
                return this->isAssignableTo(option, from);
            });
        }

        return this->isTypeCompatible(target, from);
    }

    bool SemanticAnalyzer::isTypeCompatible(const TypeInfo& target, const TypeInfo& from) const {
        if (sameTypeIgnoringOwnership(target, from))
            return true;

        if (target.kind == TypeKind::Generic)
            return from.kind == TypeKind::Generic && target.typeVar == from.typeVar;

        if (from.kind == TypeKind::Generic)
            return true;

        if (target.kind == TypeKind::Trait) {
            if (from.kind == TypeKind::Trait)
                return target.className == from.className;
            if (from.kind == TypeKind::Object || from.kind == TypeKind::Generic)
                return this->satisfiesTrait(from, target.className);
            return false;
        }

        if (target.kind == TypeKind::Object && from.kind == TypeKind::Object) {
            if (target.className == from.className)
                return this->typeArgumentsCompatible(target, from);

            if (!this->isDerived(from.className, target.className))
                return false;

            auto clsOpt = this->findClass(target.className);
            if (!clsOpt)
                return false;

            const TypeSubst subst = this->substitutionFor(clsOpt->get(), from);
            TypeInfo mapped;
            mapped.kind = TypeKind::Object;
            mapped.className = target.className;

            for (const auto& param : clsOpt->get().typeParams) {
                auto it = subst.find(param.name);
                mapped.typeArgs.push_back(it == subst.end() ? TypeInfo{ TypeKind::Unknown } : it->second);
            }

            return this->typeArgumentsCompatible(target, mapped);
        }

        if (target.kind == TypeKind::Enum && from.kind == TypeKind::Enum)
            return target.className == from.className && this->typeArgumentsCompatible(target, from);

        if (from.kind == TypeKind::Function)
            return this->declaresCallableMethod(target);

        return false;
    }

    bool SemanticAnalyzer::declaresCallableMethod(const TypeInfo& type) const {
        if (type.kind == TypeKind::Object) {
            std::unordered_set<std::string> seen;
            std::string name = type.className;

            while (!name.empty() && !seen.contains(name)) {
                seen.insert(name);

                auto cls = this->findClass(name);
                if (!cls)
                    return false;

                for (const auto& method : cls->get().methods) {
                    if (method.name == "call")
                        return true;
                }

                name = cls->get().baseClassName;
            }

            return false;
        }

        if (type.kind == TypeKind::Trait) {
            auto it = this->traits.find(type.className);
            if (it == this->traits.end())
                return false;

            return std::ranges::any_of(it->second,
                [](const TraitMethod& method) { return method.name == "call"; });
        }

        return false;
    }

}
