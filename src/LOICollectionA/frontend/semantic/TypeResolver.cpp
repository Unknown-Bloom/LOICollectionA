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
                it->second.kind != TypeKind::None)
                return it->second;
            return type;
        }

        TypeInfo result = type;
        if (type.optionalInner)
            result.optionalInner = std::make_shared<TypeInfo>(
                substituteType(*type.optionalInner, subst));
        for (size_t i = 0; i < result.variantOptions.size(); ++i)
            result.variantOptions[i] = substituteType(result.variantOptions[i], subst);
        return result;
    }

    std::unordered_map<std::string, TypeInfo> SemanticAnalyzer::inferTypeParams(
        const MethodDecl& decl, const std::vector<TypeInfo>& argTypes) const {
        std::unordered_map<std::string, TypeInfo> subst;
        for (const auto& tp : decl.typeParams)
            subst[tp.name] = {};

        for (size_t j = 0; j < argTypes.size() && j < decl.paramTypes.size(); ++j) {
            const TypeInfo& param = decl.paramTypes[j];
            if (param.kind != TypeKind::Generic)
                continue;

            auto it = subst.find(param.typeVar);
            if (it == subst.end())
                continue;

            if (it->second.kind == TypeKind::Unknown)
                it->second = argTypes[j];
            else if (it->second != argTypes[j])
                it->second = {};
        }

        return subst;
    }

    bool SemanticAnalyzer::satisfiesTrait(const TypeInfo& type, const std::string& traitName) const {
        auto traitIt = this->traits.find(traitName);
        if (traitIt == this->traits.end())
            return false;

        if (type.kind != TypeKind::Object)
            return false;

        auto clsOpt = this->findClass(type.className);
        if (!clsOpt)
            return false;

        ClassNode& cls = clsOpt->get();
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

        return true;
    }

    bool SemanticAnalyzer::boundsSatisfied(
        const std::vector<TypeParam>& typeParams,
        const std::unordered_map<std::string, TypeInfo>& subst) const {
        for (const auto& tp : typeParams) {
            if (tp.bounds.empty())
                continue;

            auto it = subst.find(tp.name);
            if (it == subst.end() || it->second.kind == TypeKind::Unknown)
                continue;

            for (const auto& bound : tp.bounds) {
                if (!this->satisfiesTrait(it->second, bound))
                    return false;
            }
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
        if (target == from)
            return true;

        if (target.kind == TypeKind::Object && from.kind == TypeKind::Object)
            return this->isDerived(from.className, target.className);

        return false;
    }

}
