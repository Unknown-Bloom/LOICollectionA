#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "LOICollectionA/base/Macro.h"

namespace LOICollection::frontend::lsp {
    struct Position {
        std::size_t line = 0;
        std::size_t character = 0;
    };

    struct Range {
        Position start{};
        Position end{};
    };

    struct Location {
        std::string uri;
        Range range{};
    };

    enum class Severity : int {
        Error = 1,
        Warning = 2,
        Information = 3,
        Hint = 4
    };

    struct Diagnostic {
        Range range{};
        Severity severity = Severity::Error;
        std::string message;
    };

    enum class SymbolKind : int {
        Function = 0,
        Method = 1,
        StaticMethod = 2,
        Field = 3,
        StaticField = 4,
        Variable = 5,
        Constant = 6,
        Class = 7,
        Interface = 8
    };

    enum class CompletionKind : int {
        Method = 2,
        Function = 3,
        Field = 5,
        Variable = 6,
        Class = 7,
        Interface = 8,
        Keyword = 14,
        Constant = 21
    };

    struct Symbol {
        std::string name;
        SymbolKind kind = SymbolKind::Variable;
        std::string detail;
        Range range{};
        std::string container;
    };

    struct CompletionItem {
        std::string label;
        CompletionKind kind = CompletionKind::Variable;
        std::string detail;
    };

    struct Hover {
        std::string contents;
        std::optional<Range> range;
    };

    LOICOLLECTION_A_API void to_json(nlohmann::ordered_json& j, const Position& value);
    LOICOLLECTION_A_API void from_json(const nlohmann::ordered_json& j, Position& value);

    LOICOLLECTION_A_API void to_json(nlohmann::ordered_json& j, const Range& value);
    LOICOLLECTION_A_API void from_json(const nlohmann::ordered_json& j, Range& value);

    LOICOLLECTION_A_API void to_json(nlohmann::ordered_json& j, const Location& value);

    LOICOLLECTION_A_API void to_json(nlohmann::ordered_json& j, const Diagnostic& value);

    LOICOLLECTION_A_API void to_json(nlohmann::ordered_json& j, const CompletionItem& value);

    LOICOLLECTION_A_API void to_json(nlohmann::ordered_json& j, const Hover& value);

    LOICOLLECTION_A_NDAPI nlohmann::ordered_json makeResponse(const nlohmann::ordered_json& id, nlohmann::ordered_json result);

    LOICOLLECTION_A_NDAPI nlohmann::ordered_json makeError(const nlohmann::ordered_json& id, int code, std::string message);

    LOICOLLECTION_A_NDAPI nlohmann::ordered_json makeNotification(std::string method, nlohmann::ordered_json params);

    LOICOLLECTION_A_NDAPI std::string encodeMessage(const nlohmann::ordered_json& message);

    LOICOLLECTION_A_API bool decodeMessage(std::string& buffer, nlohmann::ordered_json& message);
}
