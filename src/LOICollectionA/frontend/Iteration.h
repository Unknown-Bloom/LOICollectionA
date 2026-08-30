#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "LOICollectionA/frontend/AST.h"

namespace LOICollection::frontend {
    inline constexpr std::string_view lengthMethod  = "length";
    inline constexpr std::string_view elementMethod = "element";

    enum class IterableShape {
        Counter,
        Indexed,
        Convention
    };

    struct IterableProtocol {
        IterableShape shape = IterableShape::Indexed;
        TypeInfo      elementType{};
        std::string   className{};
    };

    using ClassLookup = std::function<ClassNode* (const std::string&)>;

    [[nodiscard]] std::optional<IterableProtocol> iterableProtocol(
        ASTNode::Type nodeType,
        const TypeInfo& valueType,
        const ClassLookup& lookup = {});
}
