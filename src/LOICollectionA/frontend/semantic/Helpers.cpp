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

    TypeInfo typeFromParam(ParamType param) {
        switch (param) {
            case ParamType::INT: return { TypeKind::Int };
            case ParamType::FLOAT: return { TypeKind::Float };
            case ParamType::STRING: return { TypeKind::String };
            case ParamType::BOOL: return { TypeKind::Bool };
            case ParamType::OBJECT: return { TypeKind::Object };
            case ParamType::FUNCTION: return { TypeKind::Function };
            case ParamType::ARRAY: return { TypeKind::Array };
        }

        return {};
    }

    bool typeMatchesParam(const TypeInfo& type, ParamType param) {
        if (type.kind == TypeKind::Unknown)
            return true;

        if (type.kind == TypeKind::Optional)
            return type.optionalInner && typeMatchesParam(*type.optionalInner, param);

        if (type.kind == TypeKind::Variant) {
            return std::ranges::any_of(type.variantOptions, [param](const TypeInfo& option) -> bool {
                return typeMatchesParam(option, param);
            });
        }

        TypeInfo expected = typeFromParam(param);
        if (expected.kind == TypeKind::Object)
            return type.kind == TypeKind::Object;

        return type == expected;
    }

    bool matchesNativeSignature(const CallbackTypeArgs& signature, const std::vector<TypeInfo>& argTypes) {
        if (signature.size() != argTypes.size())
            return false;

        for (size_t i = 0; i < argTypes.size(); ++i) {
            if (!typeMatchesParam(argTypes[i], signature[i]))
                return false;
        }

        return true;
    }

    TypeInfo arithmeticResult(const std::string& op, const TypeInfo& left, const TypeInfo& right) {
        auto numeric = [](const TypeInfo& t) { return t.kind == TypeKind::Int || t.kind == TypeKind::Float; };

        if (op == "+" && (left.kind == TypeKind::String || right.kind == TypeKind::String))
            return { TypeKind::String };

        if (!(numeric(left) && numeric(right)))
            return {};

        bool bothInt = left.kind == TypeKind::Int && right.kind == TypeKind::Int;
        if (bothInt && (op == "+" || op == "-" || op == "*" || op == "%"))
            return { TypeKind::Int };

        return { TypeKind::Float };
    }

    bool isNativeClass(const std::string& name) {
        return ClassCall::getInstance().isRegistered(name);
    }

        const std::unordered_map<std::string, TypeKind>& basicTypes() {
            static const std::unordered_map<std::string, TypeKind> table = {
                {"int", TypeKind::Int},
                {"float", TypeKind::Float},
                {"string", TypeKind::String},
                {"bool", TypeKind::Bool},
                {"void", TypeKind::Void},
            };
            return table;
        }

        bool isReservedTypeName(const std::string& name) {
            return basicTypes().contains(name) || name == "variant" || name == "optional";
        }

        bool isWhitelistedFormClass(const std::string& name) {
            return name == "CustomForm" || name == "MessageBox" ||
                   name == "PaginatedForm" || name == "ScriptForm";
        }

}
