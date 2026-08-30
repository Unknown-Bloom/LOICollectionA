#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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

        bool hasNativeConvention(const std::string& className) {
            ClassCall& classes = ClassCall::getInstance();
            if (!classes.isRegistered(className))
                return false;

            return hasSignature(classes.getMethodSignatures(className, std::string(lengthMethod)), {})
                && hasSignature(classes.getMethodSignatures(className, std::string(elementMethod)), { ParamType::INT });
        }

        const MethodDecl* findMethod(const ClassNode& cls, std::string_view name, size_t arity, const ClassLookup& lookup) {
            std::unordered_set<const ClassNode*> seen;
            const ClassNode* walk = &cls;
            while (walk && seen.insert(walk).second) {
                for (const auto& method : walk->methods) {
                    if (method.isConstructor || method.isStatic || method.name != name)
                        continue;
                    if (method.params.size() != arity)
                        continue;

                    return &method;
                }

                walk = walk->baseClassName.empty() ? nullptr : lookup(walk->baseClassName);
            }

            return nullptr;
        }

        bool takesIndex(const MethodDecl& method) {
            if (method.paramTypes.size() != 1)
                return false;

            TypeKind kind = method.paramTypes[0].kind;

            return kind == TypeKind::Int || kind == TypeKind::Unknown;
        }

        std::optional<IterableProtocol> scriptConvention(const ClassNode& cls, const ClassLookup& lookup) {
            if (!findMethod(cls, lengthMethod, 0, lookup))
                return std::nullopt;

            const MethodDecl* element = findMethod(cls, elementMethod, 1, lookup);
            if (!element || !takesIndex(*element))
                return std::nullopt;

            return IterableProtocol{ IterableShape::Convention, element->returnType, cls.name };
        }
    }

    std::optional<IterableProtocol> iterableProtocol(
        ASTNode::Type nodeType, const TypeInfo& valueType, const ClassLookup& lookup
    ) {
        if (nodeType == ASTNode::Type::Range)
            return IterableProtocol{ IterableShape::Counter, { TypeKind::Int } };

        if (valueType.kind == TypeKind::Unknown)
            return IterableProtocol{ IterableShape::Indexed, {} };

        if (auto it = iterableTraits().find(valueType.kind); it != iterableTraits().end())
            return IterableProtocol{ it->second.shape, it->second.elementType };

        if (valueType.kind != TypeKind::Object)
            return std::nullopt;

        if (hasNativeConvention(valueType.className))
            return IterableProtocol{ IterableShape::Convention, {}, valueType.className };

        const ClassNode* cls = lookup ? lookup(valueType.className) : nullptr;
        if (!cls)
            return std::nullopt;

        return scriptConvention(*cls, lookup);
    }
}
