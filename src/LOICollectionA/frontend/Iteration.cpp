#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include "LOICollectionA/frontend/Callback.h"

#include "LOICollectionA/frontend/Iteration.h"

namespace LOICollection::frontend {
    namespace {
        struct IterableTraits {
            IterableShape shape = IterableShape::Indexed;
            TypeInfo      elementType{};
        };

        const std::unordered_map<TypeKind, IterableTraits>& iterableTraits() {
            static const std::unordered_map<TypeKind, IterableTraits> table = {
                { TypeKind::Array,  { IterableShape::Indexed, {} }                   },
                { TypeKind::String, { IterableShape::Indexed, { TypeKind::String } } },
            };

            return table;
        }

        bool hasSignature(const std::vector<CallbackTypeArgs>& signatures, const CallbackTypeArgs& expected) {
            return std::ranges::any_of(signatures, [&expected](const CallbackTypeArgs& signature) -> bool { return signature == expected; });
        }

        bool providesConvention(const std::string& className) {
            ClassCall& classes = ClassCall::getInstance();
            if (!classes.isRegistered(className))
                return false;

            return hasSignature(classes.getMethodSignatures(className, std::string(lengthMethod)), {})
                && hasSignature(classes.getMethodSignatures(className, std::string(elementMethod)), { ParamType::INT });
        }
    }

    std::optional<IterableProtocol> iterableProtocol(ASTNode::Type nodeType, const TypeInfo& valueType) {
        if (nodeType == ASTNode::Type::Range)
            return IterableProtocol{ IterableShape::Counter, { TypeKind::Int } };

        if (valueType.kind == TypeKind::Unknown)
            return IterableProtocol{ IterableShape::Indexed, {} };

        if (auto it = iterableTraits().find(valueType.kind); it != iterableTraits().end())
            return IterableProtocol{ it->second.shape, it->second.elementType };

        if (valueType.kind == TypeKind::Object && providesConvention(valueType.className))
            return IterableProtocol{ IterableShape::Convention, {}, valueType.className };

        return std::nullopt;
    }
}
