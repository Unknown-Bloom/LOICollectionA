#pragma once

#include <cstddef>
#include <cstdint>
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
        Interface = 8,
        Enum = 9,
        EnumMember = 10,
        Component = 11,
        Module = 12,
        Parameter = 13
    };

    enum class CompletionKind : int {
        Method = 2,
        Function = 3,
        Field = 5,
        Variable = 6,
        Class = 7,
        Interface = 8,
        Module = 9,
        Property = 10,
        Enum = 13,
        Keyword = 14,
        EnumMember = 20,
        Constant = 21,
        Struct = 22
    };

    struct Symbol {
        std::string name;
        SymbolKind kind = SymbolKind::Variable;
        std::string detail;
        Range range{};
        std::string container;
        std::string uri;
        std::size_t offset = 0;
        std::size_t scopeStart = 0;
        std::size_t scopeEnd = 0;
        bool local = false;
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

    struct TextEdit {
        Range range{};
        std::string newText;
    };

    struct CodeAction {
        std::string title;
        std::string kind = "quickfix";
        std::string uri;
        std::vector<Diagnostic> diagnostics;
        std::vector<TextEdit> edits;
    };

    struct SymbolInformation {
        std::string name;
        SymbolKind kind = SymbolKind::Variable;
        std::string uri;
        Range range{};
        std::string containerName;
    };

    struct DocumentHighlight {
        Range range{};
        int kind = 1;
    };

    struct SignatureInformation {
        std::string label;
        std::vector<std::string> parameters;
    };

    struct SignatureHelp {
        std::vector<SignatureInformation> signatures;
        int activeSignature = 0;
        int activeParameter = 0;
    };

    struct WorkspaceEdit {
        std::string uri;
        std::vector<TextEdit> edits;
    };

    struct SemanticTokens {
        std::vector<std::uint32_t> data;
    };

    struct InlayHint {
        Position position{};
        std::string label;
        int kind = 1;
    };

    LOICOLLECTION_A_API void to_json(nlohmann::ordered_json& j, const Position& value);
    LOICOLLECTION_A_API void from_json(const nlohmann::ordered_json& j, Position& value);

    LOICOLLECTION_A_API void to_json(nlohmann::ordered_json& j, const Range& value);
    LOICOLLECTION_A_API void from_json(const nlohmann::ordered_json& j, Range& value);

    LOICOLLECTION_A_API void to_json(nlohmann::ordered_json& j, const Location& value);

    LOICOLLECTION_A_API void to_json(nlohmann::ordered_json& j, const Diagnostic& value);

    LOICOLLECTION_A_API void to_json(nlohmann::ordered_json& j, const CompletionItem& value);

    LOICOLLECTION_A_API void to_json(nlohmann::ordered_json& j, const Hover& value);

    LOICOLLECTION_A_API void to_json(nlohmann::ordered_json& j, const TextEdit& value);

    LOICOLLECTION_A_API void to_json(nlohmann::ordered_json& j, const CodeAction& value);

    LOICOLLECTION_A_API void to_json(nlohmann::ordered_json& j, const SymbolInformation& value);

    LOICOLLECTION_A_API void to_json(nlohmann::ordered_json& j, const DocumentHighlight& value);

    LOICOLLECTION_A_API void to_json(nlohmann::ordered_json& j, const SignatureHelp& value);

    LOICOLLECTION_A_API void to_json(nlohmann::ordered_json& j, const WorkspaceEdit& value);

    LOICOLLECTION_A_API void to_json(nlohmann::ordered_json& j, const SemanticTokens& value);

    LOICOLLECTION_A_API void to_json(nlohmann::ordered_json& j, const InlayHint& value);

    LOICOLLECTION_A_NDAPI nlohmann::ordered_json makeResponse(const nlohmann::ordered_json& id, nlohmann::ordered_json result);

    LOICOLLECTION_A_NDAPI nlohmann::ordered_json makeError(const nlohmann::ordered_json& id, int code, std::string message);

    LOICOLLECTION_A_NDAPI nlohmann::ordered_json makeNotification(std::string method, nlohmann::ordered_json params);

    LOICOLLECTION_A_NDAPI std::string encodeMessage(const nlohmann::ordered_json& message);

    LOICOLLECTION_A_API bool decodeMessage(std::string& buffer, nlohmann::ordered_json& message);
}
