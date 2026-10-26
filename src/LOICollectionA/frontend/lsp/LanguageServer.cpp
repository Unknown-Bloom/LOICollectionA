#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/Lexer.h"

#include "LOICollectionA/frontend/lsp/LanguageServer.h"

namespace LOICollection::frontend::lsp::detail {
    constexpr std::string_view kKeywords[] = {
        "if", "class", "func", "new", "this", "super", "return",
        "public", "private", "extends", "instanceof", "static", "using",
        "None", "while", "for", "in", "break", "continue",
        "import", "component", "let", "const", "trait", "impl",
        "enum", "match", "panic", "dyn",
        "true", "false"
    };

    constexpr std::string_view kTypes[] = {
        "int", "float", "string", "bool", "void", "optional", "variant", "array"
    };

    constexpr std::string_view kFallbackForms[] = {
        "CustomForm", "MessageBox", "PaginatedForm", "ScriptForm"
    };

    constexpr std::string_view kSemanticTokenTypes[] = {
        "namespace", "type", "class", "enum", "interface", "struct", "typeParameter",
        "parameter", "variable", "property", "enumMember", "event", "function", "method",
        "macro", "keyword", "modifier", "comment", "string", "number", "regexp", "operator"
    };

    enum SemanticType : int {
        SemanticNamespace,
        SemanticType,
        SemanticClass,
        SemanticEnum,
        SemanticInterface,
        SemanticStruct,
        SemanticTypeParameter,
        SemanticParameter,
        SemanticVariable,
        SemanticProperty,
        SemanticEnumMember,
        SemanticEvent,
        SemanticFunction,
        SemanticMethod,
        SemanticMacro,
        SemanticKeyword,
        SemanticModifier,
        SemanticComment,
        SemanticString,
        SemanticNumber,
        SemanticRegexp,
        SemanticOperator
    };

    struct Receiver {
        std::string name;
        std::string separator;
    };

    struct TokenInfo {
        std::size_t line = 0;
        std::size_t character = 0;
        std::size_t length = 0;
        int type = 0;
    };

    bool isIdentifierStart(char c) {
        return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }

    bool isIdentifierChar(char c) {
        return isIdentifierStart(c) || (c >= '0' && c <= '9');
    }

    std::size_t offsetOf(const std::string& text, const Position& position) {
        std::size_t offset = 0;

        for (std::size_t line = 0; line < position.line && offset < text.size(); ++offset)
            if (text[offset] == '\n')
                ++line;

        return std::min(offset + position.character, text.size());
    }

    std::size_t wordStartAt(const std::string& text, std::size_t offset) {
        offset = std::min(offset, text.size());

        if (offset > 0 && isIdentifierChar(text[offset - 1]))
            while (offset > 0 && isIdentifierChar(text[offset - 1]))
                --offset;

        return offset;
    }

    std::string wordAt(const std::string& text, std::size_t offset) {
        if (offset > text.size())
            offset = text.size();

        std::size_t start = offset;
        std::size_t end = offset;

        if (start > 0 && isIdentifierChar(text[start - 1]))
            while (start > 0 && isIdentifierChar(text[start - 1]))
                --start;
        else if (end >= text.size() || !isIdentifierChar(text[end]))
            return {};

        while (end < text.size() && isIdentifierChar(text[end]))
            ++end;

        return text.substr(start, end - start);
    }

    Receiver receiverBefore(const std::string& text, std::size_t offset) {
        if (offset > text.size())
            offset = text.size();

        std::size_t end = offset;
        while (end > 0 && isIdentifierChar(text[end - 1]))
            --end;

        if (end >= 2 && text[end - 1] == ':' && text[end - 2] == ':') {
            const std::size_t nameEnd = end - 2;
            std::size_t start = nameEnd;
            while (start > 0 && isIdentifierChar(text[start - 1]))
                --start;

            return { text.substr(start, nameEnd - start), "::" };
        }

        if (end == 0 || text[end - 1] != '.')
            return {};

        std::size_t nameEnd = end - 1;
        std::string separator = ".";
        if (nameEnd > 0 && text[nameEnd - 1] == '?') {
            --nameEnd;
            separator = "?.";
        }

        std::size_t start = nameEnd;
        while (start > 0 && isIdentifierChar(text[start - 1]))
            --start;

        return { text.substr(start, nameEnd - start), separator };
    }

    std::string enclosingClass(const std::string& text, std::size_t offset) {
        constexpr std::string_view kClass = "class";

        std::string result;
        std::size_t search = 0;

        while (search < offset) {
            const auto found = text.find(kClass, search);
            if (found == std::string::npos || found >= offset)
                break;

            if (found > 0 && isIdentifierChar(text[found - 1])) {
                search = found + kClass.size();
                continue;
            }

            const auto nameStart = text.find_first_not_of(" \t\r\n", found + kClass.size());
            if (nameStart == std::string::npos || nameStart >= offset)
                break;

            if (!isIdentifierStart(text[nameStart])) {
                search = nameStart + 1;
                continue;
            }

            auto nameEnd = nameStart;
            while (nameEnd < text.size() && isIdentifierChar(text[nameEnd]))
                ++nameEnd;

            result = text.substr(nameStart, nameEnd - nameStart);
            search = nameEnd;
        }

        return result;
    }

    const Symbol* findSymbolAt(const std::vector<Symbol>& symbols, const std::string& name,
        std::size_t offset) {
        const Symbol* best = nullptr;

        for (const auto& symbol : symbols) {
            if (symbol.name != name)
                continue;

            if (offset < symbol.scopeStart || offset > symbol.scopeEnd)
                continue;

            if (!best || symbol.scopeStart > best->scopeStart ||
                (symbol.scopeStart == best->scopeStart && symbol.offset <= offset &&
                 (best->offset > offset || symbol.offset > best->offset)))
                best = &symbol;
        }

        return best;
    }

    const Symbol* findImportedSymbol(const std::vector<Symbol>& symbols, const std::string& name) {
        for (const auto& symbol : symbols)
            if (symbol.name == name)
                return &symbol;

        return nullptr;
    }

    const Symbol* findAnySymbol(const DocumentAnalysis& analysis, const std::string& name,
        std::size_t offset) {
        if (const Symbol* symbol = findSymbolAt(analysis.symbols, name, offset))
            return symbol;

        return findImportedSymbol(analysis.imported, name);
    }

    const Symbol* findMember(const DocumentAnalysis& analysis, const std::string& className,
        const std::string& name) {
        for (const auto& symbol : analysis.symbols)
            if (symbol.container == className && symbol.name == name)
                return &symbol;

        for (const auto& symbol : analysis.imported)
            if (symbol.container == className && symbol.name == name)
                return &symbol;

        return nullptr;
    }

    std::string classNameOf(const Symbol& symbol) {
        switch (symbol.kind) {
            case SymbolKind::Variable:
            case SymbolKind::Field:
            case SymbolKind::StaticField:
            case SymbolKind::Constant:
            case SymbolKind::Parameter:
                break;
            default:
                return symbol.name;
        }

        std::string type = symbol.detail;
        if (symbol.kind == SymbolKind::Parameter) {
            const auto colon = type.find(": ");
            if (colon != std::string::npos)
                type.erase(0, colon + 2);
        }

        if (type.starts_with("class "))
            type.erase(0, 6);

        if (const auto bracket = type.find('<'); bracket != std::string::npos)
            type.erase(bracket);

        return type;
    }

    std::string nativeClassAlias(const std::string& type) {
        if (type == "string")
            return "String";

        if (type == "array")
            return "Array";

        return type;
    }

    std::string receiverClassOf(const DocumentAnalysis& analysis, const std::string& receiver,
        std::size_t offset, const std::string& text) {
        if (receiver == "this")
            return enclosingClass(text, offset);

        if (const Symbol* symbol = findAnySymbol(analysis, receiver, offset))
            return nativeClassAlias(classNameOf(*symbol));

        return nativeClassAlias(receiver);
    }

    Position positionOf(const std::string& text, std::size_t offset) {
        offset = std::min(offset, text.size());

        Position position;
        for (std::size_t i = 0; i < offset; ++i) {
            if (text[i] == '\n') {
                ++position.line;
                position.character = 0;
            } else {
                ++position.character;
            }
        }

        return position;
    }

    Range rangeAt(const std::string& text, std::size_t offset, std::size_t length) {
        Range range;
        range.start = positionOf(text, offset);
        range.end = positionOf(text, offset + length);

        return range;
    }

    std::vector<std::size_t> wordOccurrences(const std::string& text, const std::string& word) {
        std::vector<std::size_t> offsets;
        if (word.empty())
            return offsets;

        std::size_t search = 0;
        while (search < text.size()) {
            const auto found = text.find(word, search);
            if (found == std::string::npos)
                break;

            const bool leftClear = found == 0 || !isIdentifierChar(text[found - 1]);
            const bool rightClear = found + word.size() >= text.size()
                || !isIdentifierChar(text[found + word.size()]);

            if (leftClear && rightClear)
                offsets.push_back(found);

            search = found + word.size();
        }

        return offsets;
    }

    bool sameSymbol(const Symbol& left, const Symbol& right) {
        return left.name == right.name && left.uri == right.uri &&
            left.offset == right.offset && left.kind == right.kind &&
            left.container == right.container;
    }

    const Symbol* resolveAt(const DocumentAnalysis& analysis, const std::string& text,
        std::size_t offset) {
        const std::string word = wordAt(text, offset);
        if (word.empty())
            return nullptr;

        const Receiver receiver = receiverBefore(text, offset);
        if (!receiver.name.empty()) {
            const std::string className = receiverClassOf(analysis, receiver.name, offset, text);
            if (!className.empty())
                return findMember(analysis, className, word);

            return nullptr;
        }

        return findAnySymbol(analysis, word, offset);
    }

    bool isWriteOccurrence(const std::string& text, std::size_t offset, const std::string& word) {
        std::size_t previous = offset;
        while (previous > 0 && (text[previous - 1] == ' ' || text[previous - 1] == '\t'))
            --previous;

        if (previous > 0 && text[previous - 1] == ':')
            return false;

        std::size_t next = offset + word.size();
        while (next < text.size() && (text[next] == ' ' || text[next] == '\t'))
            ++next;

        if (next < text.size() && text[next] == '=' &&
            (next + 1 >= text.size() || text[next + 1] != '='))
            return true;

        if (next + 1 < text.size() && text[next + 1] == '=' &&
            std::string_view("+-*/%").find(text[next]) != std::string_view::npos)
            return true;

        std::size_t before = offset;
        while (before > 0 && (text[before - 1] == ' ' || text[before - 1] == '\t' || text[before - 1] == '\n'))
            --before;

        const std::size_t keywordStart = wordStartAt(text, before);
        const std::string_view keyword(text.data() + keywordStart, before - keywordStart);

        return keyword == "let" || keyword == "const" || keyword == "for";
    }

    CompletionKind completionKindOf(SymbolKind kind) {
        switch (kind) {
            case SymbolKind::Function: return CompletionKind::Function;
            case SymbolKind::Method:
            case SymbolKind::StaticMethod: return CompletionKind::Method;
            case SymbolKind::Field:
            case SymbolKind::StaticField: return CompletionKind::Field;
            case SymbolKind::Class: return CompletionKind::Class;
            case SymbolKind::Interface: return CompletionKind::Interface;
            case SymbolKind::Enum: return CompletionKind::Enum;
            case SymbolKind::EnumMember: return CompletionKind::EnumMember;
            case SymbolKind::Component: return CompletionKind::Function;
            case SymbolKind::Module: return CompletionKind::Module;
            case SymbolKind::Constant: return CompletionKind::Constant;
            default: return CompletionKind::Variable;
        }
    }

    std::string paramTypeName(ParamType type) {
        switch (type) {
            case ParamType::INT: return "int";
            case ParamType::FLOAT: return "float";
            case ParamType::STRING: return "string";
            case ParamType::BOOL: return "bool";
            case ParamType::OBJECT: return "object";
            case ParamType::FUNCTION: return "function";
            case ParamType::ARRAY: return "array";
        }

        return "object";
    }

    std::string signatureLabel(const std::string& name, const CallbackTypeArgs& args) {
        std::string label = name + "(";

        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i != 0)
                label += ", ";
            label += paramTypeName(args[i]);
        }

        return label + ")";
    }

    SignatureInformation signatureInfo(const std::string& label) {
        SignatureInformation signature;
        signature.label = label;

        const auto open = label.find('(');
        const auto close = label.find(')', open == std::string::npos ? 0 : open);
        if (open == std::string::npos || close == std::string::npos)
            return signature;

        const std::string body = label.substr(open + 1, close - open - 1);

        std::size_t segmentStart = 0;
        for (std::size_t i = 0; i <= body.size(); ++i) {
            if (i != body.size() && body[i] != ',')
                continue;

            const auto trimmed = body.substr(segmentStart, i - segmentStart);
            const auto first = trimmed.find_first_not_of(" \t");
            const auto last = trimmed.find_last_not_of(" \t");

            if (first != std::string::npos)
                signature.parameters.push_back(trimmed.substr(first, last - first + 1));

            segmentStart = i + 1;
        }

        return signature;
    }

    bool isNamespace(const std::string& name) {
        const auto namespaces = FunctionCall::getInstance().getNamespaces();

        return std::find(namespaces.begin(), namespaces.end(), name) != namespaces.end();
    }

    std::vector<SignatureInformation> namespaceSignatures(const std::string& namespaces,
        const std::string& function) {
        std::vector<SignatureInformation> result;

        for (const auto& args : FunctionCall::getInstance().getFunctionSignatures(namespaces, function))
            result.push_back(signatureInfo(signatureLabel(function, args)));

        return result;
    }

    std::vector<SignatureInformation> macroSignatures(const std::string& name) {
        std::vector<SignatureInformation> result;

        for (const auto& args : MacroCall::getInstance().getMacroSignatures(name))
            result.push_back(signatureInfo(signatureLabel(name, args)));

        return result;
    }

    std::vector<SignatureInformation> constructorSignatures(const std::string& className) {
        std::vector<SignatureInformation> result;

        for (const auto& args : ClassCall::getInstance().getConstructorSignatures(className))
            result.push_back(signatureInfo(signatureLabel(className, args)));

        return result;
    }

    std::vector<SignatureInformation> classSignatures(const std::string& className,
        const std::string& method) {
        std::vector<SignatureInformation> result;
        ClassCall& classes = ClassCall::getInstance();

        for (const auto& args : classes.getMethodSignatures(className, method))
            result.push_back(signatureInfo(signatureLabel(method, args)));

        for (const auto& args : classes.getStaticMethodSignatures(className, method))
            result.push_back(signatureInfo(signatureLabel(method, args)));

        for (const auto& args : classes.getValueMethodSignatures(className, method))
            result.push_back(signatureInfo(signatureLabel(method, args)));

        return result;
    }

    std::string joinLabels(const std::vector<SignatureInformation>& signatures) {
        std::string result;

        for (const auto& signature : signatures) {
            if (!result.empty())
                result += "\n";
            result += signature.label;
        }

        return result;
    }

    void pushUnique(std::vector<CompletionItem>& items, std::unordered_set<std::string>& seen,
        std::string label, CompletionKind kind, std::string detail) {
        if (label.empty() || !seen.insert(label).second)
            return;

        items.push_back({ std::move(label), kind, std::move(detail) });
    }

    void appendSymbolCompletions(std::vector<CompletionItem>& items,
        std::unordered_set<std::string>& seen, const std::vector<Symbol>& symbols,
        const std::string& container) {
        for (const auto& symbol : symbols) {
            if (symbol.container != container || symbol.name.empty() ||
                symbol.name.starts_with('.'))
                continue;

            pushUnique(items, seen, symbol.name, completionKindOf(symbol.kind), symbol.detail);
        }
    }

    void appendReachableCompletions(std::vector<CompletionItem>& items,
        std::unordered_set<std::string>& seen, const std::vector<Symbol>& symbols,
        std::size_t offset) {
        for (const auto& symbol : symbols) {
            if (symbol.name.empty() || symbol.name.starts_with('.') ||
                symbol.kind == SymbolKind::Module || !symbol.container.empty())
                continue;

            if (offset < symbol.scopeStart || offset > symbol.scopeEnd)
                continue;

            pushUnique(items, seen, symbol.name, completionKindOf(symbol.kind), symbol.detail);
        }
    }

    void appendImportedCompletions(std::vector<CompletionItem>& items,
        std::unordered_set<std::string>& seen, const std::vector<Symbol>& symbols) {
        for (const auto& symbol : symbols) {
            if (symbol.name.empty() || symbol.kind == SymbolKind::Module ||
                !symbol.container.empty())
                continue;

            pushUnique(items, seen, symbol.name, completionKindOf(symbol.kind), symbol.detail);
        }
    }

    void appendNativeClassMembers(std::vector<CompletionItem>& items,
        std::unordered_set<std::string>& seen, const std::string& className) {
        ClassCall& classes = ClassCall::getInstance();
        if (!classes.isRegistered(className))
            return;

        for (const auto& field : classes.getFields(className))
            pushUnique(items, seen, field, CompletionKind::Field, className + "::" + field);

        for (const auto& field : classes.getStaticFields(className))
            pushUnique(items, seen, field, CompletionKind::Field, className + "::" + field);

        for (const auto& method : classes.getMethodNames(className)) {
            const auto signatures = classSignatures(className, method);
            const std::string detail = signatures.empty()
                ? className + "::" + method
                : joinLabels(signatures);

            pushUnique(items, seen, method, CompletionKind::Method, detail);
        }
    }

    void appendNativeGlobals(std::vector<CompletionItem>& items,
        std::unordered_set<std::string>& seen) {
        FunctionCall& functions = FunctionCall::getInstance();
        ClassCall& classes = ClassCall::getInstance();
        MacroCall& macros = MacroCall::getInstance();

        for (const auto& namespaces : functions.getNamespaces())
            pushUnique(items, seen, namespaces, CompletionKind::Module, "namespace");

        for (const auto& className : classes.getClassNames())
            pushUnique(items, seen, className, CompletionKind::Class, "class " + className);

        for (const auto& name : macros.getMacroNames()) {
            const auto signatures = macroSignatures(name);
            pushUnique(items, seen, name, CompletionKind::Function,
                signatures.empty() ? "macro" : joinLabels(signatures));
        }

        for (const auto& form : kFallbackForms)
            pushUnique(items, seen, std::string(form), CompletionKind::Class, "form");
    }

    void appendNamespaceFunctions(std::vector<CompletionItem>& items,
        std::unordered_set<std::string>& seen, const std::string& namespaces) {
        FunctionCall& functions = FunctionCall::getInstance();

        for (const auto& name : functions.getFunctionNames(namespaces)) {
            const auto signatures = namespaceSignatures(namespaces, name);
            pushUnique(items, seen, name, CompletionKind::Function,
                signatures.empty() ? namespaces + "::" + name : joinLabels(signatures));
        }
    }

    std::string normalizeUri(const std::string& value) {
        const auto scheme = value.find("://");

        std::string prefix;
        std::string rest = value;

        if (scheme != std::string::npos) {
            prefix = value.substr(0, scheme + 3);
            rest = value.substr(scheme + 3);
        }

        std::vector<std::string> parts;
        std::size_t start = 0;

        while (start <= rest.size()) {
            const auto slash = rest.find('/', start);
            const std::string part = rest.substr(start,
                slash == std::string::npos ? std::string::npos : slash - start);

            if (part == "..") {
                if (!parts.empty())
                    parts.pop_back();
            } else if (!part.empty() && part != ".") {
                parts.push_back(part);
            }

            if (slash == std::string::npos)
                break;

            start = slash + 1;
        }

        std::string result = prefix;
        for (const auto& part : parts) {
            result += '/';
            result += part;
        }

        return result;
    }

    std::string resolveUri(const std::string& base, const std::string& path) {
        if (path.find("://") != std::string::npos)
            return normalizeUri(path);

        if (!path.empty() && path[0] == '/')
            return normalizeUri("file://" + path);

        const auto slash = base.rfind('/');
        if (slash == std::string::npos)
            return normalizeUri(path);

        return normalizeUri(base.substr(0, slash + 1) + path);
    }

    Position endPositionOf(const std::string& text) {
        return positionOf(text, text.size());
    }

    nlohmann::ordered_json initializeResult() {
        nlohmann::ordered_json tokenTypes = nlohmann::ordered_json::array();
        for (const auto& type : kSemanticTokenTypes)
            tokenTypes.push_back(std::string(type));

        return nlohmann::ordered_json{
            {
                "capabilities", nlohmann::ordered_json{
                    { "textDocumentSync", 2 },
                    { "completionProvider", nlohmann::ordered_json{
                        { "triggerCharacters", nlohmann::ordered_json::array({ ".", ":" }) },
                    } },
                    { "hoverProvider", true },
                    { "definitionProvider", true },
                    { "typeDefinitionProvider", true },
                    { "referencesProvider", true },
                    { "documentHighlightProvider", true },
                    { "renameProvider", true },
                    { "documentSymbolProvider", true },
                    { "signatureHelpProvider", nlohmann::ordered_json{
                        { "triggerCharacters", nlohmann::ordered_json::array({ "(", "," }) },
                    } },
                    { "semanticTokensProvider", nlohmann::ordered_json{
                        { "legend", nlohmann::ordered_json{
                            { "tokenTypes", std::move(tokenTypes) },
                            { "tokenModifiers", nlohmann::ordered_json::array() },
                        } },
                        { "full", true },
                    } },
                    { "inlayHintProvider", true },
                    { "documentFormattingProvider", true },
                    { "codeActionProvider", true },
                },
            },
            { "serverInfo", nlohmann::ordered_json{ { "name", "LOICollectionA LCUI" } } },
        };
    }
}

namespace LOICollection::frontend::lsp {
    using namespace detail;

    std::string LanguageServer::handle(std::string_view bytes) {
        this->inbound.append(bytes);

        std::string outbound;
        nlohmann::ordered_json message;

        while (decodeMessage(this->inbound, message))
            outbound += this->dispatch(message);

        return outbound;
    }

    std::vector<CompletionItem> LanguageServer::completionsAt(
        const std::string& text, const Position& position, const ImportResolver& resolver
    ) {
        std::vector<CompletionItem> items;
        std::unordered_set<std::string> seen;

        const auto analysis = analyze(text, resolver);
        const std::size_t offset = offsetOf(text, position);

        if (const Receiver receiver = receiverBefore(text, offset); !receiver.name.empty()) {
            if (isNamespace(receiver.name)) {
                appendNamespaceFunctions(items, seen, receiver.name);
                return items;
            }

            std::string className = receiver.name;
            if (receiver.name == "this")
                className = enclosingClass(text, offset);
            else if (const Symbol* symbol = findAnySymbol(analysis, receiver.name, offset))
                className = classNameOf(*symbol);

            className = nativeClassAlias(className);

            if (className.empty())
                return items;

            appendSymbolCompletions(items, seen, analysis.symbols, className);
            appendSymbolCompletions(items, seen, analysis.imported, className);
            appendNativeClassMembers(items, seen, className);

            if (ClassCall::getInstance().isRegistered(className))
                return items;

            for (const auto& symbol : analysis.symbols)
                if (symbol.kind == SymbolKind::Enum && symbol.name == className)
                    for (const auto& member : analysis.symbols)
                        if (member.kind == SymbolKind::EnumMember && member.container == className)
                            pushUnique(items, seen, member.name,
                                CompletionKind::EnumMember, member.detail);

            return items;
        }

        appendReachableCompletions(items, seen, analysis.symbols, offset);
        appendImportedCompletions(items, seen, analysis.imported);

        for (const auto& symbol : analysis.symbols)
            if (symbol.kind == SymbolKind::EnumMember && !symbol.container.empty())
                pushUnique(items, seen, symbol.name, CompletionKind::EnumMember, symbol.detail);

        for (const auto& keyword : kKeywords)
            pushUnique(items, seen, std::string(keyword), CompletionKind::Keyword, "keyword");

        for (const auto& type : kTypes)
            pushUnique(items, seen, std::string(type), CompletionKind::Keyword, "type");

        appendNativeGlobals(items, seen);

        return items;
    }

    std::optional<Hover> LanguageServer::hoverAt(const std::string& text, const Position& position,
        const ImportResolver& resolver) {
        const std::size_t offset = offsetOf(text, position);
        const std::string word = wordAt(text, offset);

        if (word.empty())
            return std::nullopt;

        const auto analysis = analyze(text, resolver);
        const Receiver receiver = receiverBefore(text, offset);

        if (!receiver.name.empty()) {
            if (isNamespace(receiver.name)) {
                const auto signatures = namespaceSignatures(receiver.name, word);
                if (!signatures.empty())
                    return Hover{ joinLabels(signatures), rangeAt(text, wordStartAt(text, offset), word.size()) };
            }

            const std::string className = receiverClassOf(analysis, receiver.name, offset, text);
            if (const Symbol* member = findMember(analysis, className, word)) {
                Hover hover;
                hover.contents = member->detail.empty() ? member->name : member->detail;
                hover.range = member->range;

                return hover;
            }

            const auto signatures = classSignatures(className, word);
            if (!signatures.empty())
                return Hover{ joinLabels(signatures), rangeAt(text, wordStartAt(text, offset), word.size()) };

            if (ClassCall::getInstance().hasField(className, word) ||
                ClassCall::getInstance().hasStaticField(className, word)) {
                std::string contents = className + "::" + word;

                return Hover{ std::move(contents), rangeAt(text, wordStartAt(text, offset), word.size()) };
            }

            return std::nullopt;
        }

        if (const Symbol* symbol = findAnySymbol(analysis, word, offset)) {
            Hover hover;
            hover.contents = symbol->detail.empty() ? symbol->name : symbol->detail;
            hover.range = symbol->range;

            return hover;
        }

        if (const auto signatures = macroSignatures(word); !signatures.empty())
            return Hover{ joinLabels(signatures), rangeAt(text, wordStartAt(text, offset), word.size()) };

        ClassCall& classes = ClassCall::getInstance();
        if (classes.isRegistered(word))
            return Hover{ "class " + word, rangeAt(text, wordStartAt(text, offset), word.size()) };

        if (isNamespace(word))
            return Hover{ "namespace " + word, rangeAt(text, wordStartAt(text, offset), word.size()) };

        return std::nullopt;
    }

    std::optional<Location> LanguageServer::definitionAt(
        const std::string& uri, const std::string& text, const Position& position,
        const ImportResolver& resolver
    ) {
        const std::size_t offset = offsetOf(text, position);
        const std::string word = wordAt(text, offset);

        if (word.empty())
            return std::nullopt;

        const auto analysis = analyze(text, resolver);
        const Symbol* symbol = resolveAt(analysis, text, offset);
        if (!symbol)
            return std::nullopt;

        return Location{ symbol->uri.empty() ? uri : symbol->uri, symbol->range };
    }

    std::vector<CodeAction> LanguageServer::codeActionsAt(
        const std::string& uri, const std::string& text, const Range& range,
        const std::vector<Diagnostic>& diagnostics
    ) {
        std::vector<CodeAction> actions;

        const auto before = [](const Position& left, const Position& right) {
            return left.line < right.line || (left.line == right.line && left.character < right.character);
        };

        const auto overlaps = [&before](const Range& left, const Range& right) {
            return !before(left.end, right.start) && !before(right.end, left.start);
        };

        for (const auto& diagnostic : diagnostics) {
            constexpr std::string_view kNotDeclared = "is not declared";

            if (diagnostic.message.find(kNotDeclared) == std::string::npos)
                continue;

            if (!overlaps(diagnostic.range, range))
                continue;

            const auto quoteStart = diagnostic.message.find('\'');
            const auto quoteEnd = quoteStart == std::string::npos
                ? std::string::npos
                : diagnostic.message.find('\'', quoteStart + 1);

            if (quoteEnd == std::string::npos || quoteEnd == quoteStart + 1)
                continue;

            const std::string name = diagnostic.message.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
            if (wordAt(text, offsetOf(text, diagnostic.range.start)) != name)
                continue;

            const Position cursor = diagnostic.range.start;

            CodeAction action;
            action.title = "Declare '" + name + "' with let";
            action.uri = uri;
            action.diagnostics.push_back(diagnostic);
            action.edits.push_back(TextEdit{ Range{ cursor, cursor }, "let " });

            actions.push_back(std::move(action));
        }

        return actions;
    }

    std::vector<SymbolInformation> LanguageServer::symbolsAt(
        const std::string& uri, const std::string& text
    ) {
        std::vector<SymbolInformation> result;
        const auto analysis = analyze(text);

        for (const auto& symbol : analysis.symbols)
            if (!symbol.name.empty() && !symbol.local && !symbol.name.starts_with('.'))
                result.push_back({ symbol.name, symbol.kind, uri, symbol.range, symbol.container });

        return result;
    }

    std::vector<Location> LanguageServer::referencesAt(
        const std::string& uri, const std::string& text, const Position& position
    ) {
        const std::size_t cursor = offsetOf(text, position);
        const std::string word = wordAt(text, cursor);

        if (word.empty())
            return {};

        const auto analysis = analyze(text);
        const Symbol* target = resolveAt(analysis, text, cursor);

        std::vector<Location> result;
        for (const std::size_t offset : wordOccurrences(text, word)) {
            if (target) {
                if (offset == target->offset)
                    continue;

                const Symbol* occurrence = resolveAt(analysis, text, offset);
                if (!occurrence || !sameSymbol(*occurrence, *target))
                    continue;
            }

            result.push_back(Location{ uri, rangeAt(text, offset, word.size()) });
        }

        return result;
    }

    std::vector<DocumentHighlight> LanguageServer::highlightsAt(
        const std::string& text, const Position& position
    ) {
        const std::size_t cursor = offsetOf(text, position);
        const std::string word = wordAt(text, cursor);

        if (word.empty())
            return {};

        const auto analysis = analyze(text);
        const Symbol* target = resolveAt(analysis, text, cursor);

        std::vector<DocumentHighlight> result;
        for (const std::size_t offset : wordOccurrences(text, word)) {
            if (target) {
                const Symbol* occurrence = resolveAt(analysis, text, offset);
                if (!occurrence || !sameSymbol(*occurrence, *target))
                    continue;
            }

            const bool write = (target && offset == target->offset) ||
                isWriteOccurrence(text, offset, word);

            result.push_back({ rangeAt(text, offset, word.size()), write ? 3 : 2 });
        }

        return result;
    }

    std::vector<TextEdit> LanguageServer::renameAt(
        const std::string& text, const Position& position, const std::string& newName
    ) {
        const std::size_t cursor = offsetOf(text, position);
        const std::string word = wordAt(text, cursor);

        if (word.empty() || newName.empty())
            return {};

        const auto analysis = analyze(text);
        const Symbol* target = resolveAt(analysis, text, cursor);

        std::vector<TextEdit> edits;
        for (const std::size_t offset : wordOccurrences(text, word)) {
            if (target) {
                const Symbol* occurrence = resolveAt(analysis, text, offset);
                if (!occurrence || !sameSymbol(*occurrence, *target))
                    continue;
            }

            edits.push_back(TextEdit{ rangeAt(text, offset, word.size()), newName });
        }

        return edits;
    }

    std::optional<Location> LanguageServer::typeDefinitionAt(
        const std::string& uri, const std::string& text, const Position& position,
        const ImportResolver& resolver
    ) {
        const std::size_t offset = offsetOf(text, position);
        const std::string word = wordAt(text, offset);

        if (word.empty())
            return std::nullopt;

        const auto analysis = analyze(text, resolver);
        const Symbol* symbol = resolveAt(analysis, text, offset);
        if (!symbol)
            return std::nullopt;

        const std::string type = classNameOf(*symbol);
        if (type.empty() || type == word)
            return std::nullopt;

        for (const auto& candidate : analysis.symbols)
            if ((candidate.kind == SymbolKind::Class || candidate.kind == SymbolKind::Enum ||
                 candidate.kind == SymbolKind::Interface) && candidate.name == type)
                return Location{ uri, candidate.range };

        for (const auto& candidate : analysis.imported)
            if ((candidate.kind == SymbolKind::Class || candidate.kind == SymbolKind::Enum ||
                 candidate.kind == SymbolKind::Interface) && candidate.name == type)
                return Location{ candidate.uri, candidate.range };

        return std::nullopt;
    }

    std::optional<SignatureHelp> LanguageServer::signatureHelpAt(
        const std::string& text, const Position& position, const ImportResolver& resolver
    ) {
        const std::size_t cursor = offsetOf(text, position);

        std::size_t open = std::string::npos;
        int depth = 0;

        for (std::size_t i = cursor; i > 0; --i) {
            const char current = text[i - 1];

            if (current == ')') {
                ++depth;
                continue;
            }

            if (current != '(')
                continue;

            if (depth == 0) {
                open = i - 1;
                break;
            }

            --depth;
        }

        if (open == std::string::npos)
            return std::nullopt;

        std::size_t nameEnd = open;
        while (nameEnd > 0 && (text[nameEnd - 1] == ' ' || text[nameEnd - 1] == '\t'))
            --nameEnd;

        std::size_t nameStart = nameEnd;
        while (nameStart > 0 && isIdentifierChar(text[nameStart - 1]))
            --nameStart;

        const std::string callee = text.substr(nameStart, nameEnd - nameStart);
        if (callee.empty())
            return std::nullopt;

        const Receiver receiver = receiverBefore(text, nameStart);
        const auto analysis = analyze(text, resolver);

        std::vector<SignatureInformation> signatures;

        if (!receiver.name.empty()) {
            if (isNamespace(receiver.name)) {
                signatures = namespaceSignatures(receiver.name, callee);
            } else {
                const std::string className = receiverClassOf(analysis, receiver.name, open, text);

                if (const Symbol* member = findMember(analysis, className, callee)) {
                    if (member->detail.find('(') != std::string::npos)
                        signatures.push_back(signatureInfo(member->detail));
                }

                if (signatures.empty())
                    signatures = classSignatures(className, callee);
            }
        } else {
            const Symbol* symbol = findAnySymbol(analysis, callee, open);

            if (symbol && symbol->kind == SymbolKind::Class) {
                if (const Symbol* constructor = findMember(analysis, symbol->name, callee))
                    if (constructor->detail.find('(') != std::string::npos)
                        signatures.push_back(signatureInfo(constructor->detail));
            } else if (symbol && symbol->kind == SymbolKind::EnumMember) {
                signatures.push_back(signatureInfo(symbol->detail));
            } else if (symbol && symbol->detail.find('(') != std::string::npos) {
                signatures.push_back(signatureInfo(symbol->detail));
            }

            if (signatures.empty())
                signatures = macroSignatures(callee);

            if (signatures.empty())
                signatures = constructorSignatures(callee);
        }

        if (signatures.empty())
            return std::nullopt;

        SignatureHelp help;
        help.signatures = std::move(signatures);

        int active = 0;
        int nested = 0;
        for (std::size_t i = open + 1; i < cursor; ++i) {
            const char current = text[i];

            if (current == '(' || current == '[' || current == '{')
                ++nested;
            else if (current == ')' || current == ']' || current == '}')
                nested = nested > 0 ? nested - 1 : 0;
            else if (current == ',' && nested == 0)
                ++active;
        }

        help.activeParameter = active;

        return help;
    }

    SemanticTokens LanguageServer::semanticTokens(const std::string& text) {
        std::vector<TokenInfo> tokens;
        const auto analysis = analyze(text);

        std::unordered_map<std::size_t, SymbolKind> symbolKinds;
        for (const auto& symbol : analysis.symbols)
            if (symbol.uri.empty() && !symbol.name.empty() && !symbol.name.starts_with('.'))
                symbolKinds.emplace(symbol.offset, symbol.kind);

        DiagnosticEngine engine;
        Lexer lexer(text, engine);

        std::vector<Token> lexed;
        while (true) {
            Token token = lexer.getNextToken();
            if (token.type == TokenType::TOKEN_EOF)
                break;
            lexed.push_back(std::move(token));
        }

        const auto tokenTypeOf = [&symbolKinds](TokenType type, const Token& token,
            const Token* previous, const Token* next) -> int {
            switch (type) {
                case TokenType::TOKEN_STRING: return SemanticString;
                case TokenType::TOKEN_INT:
                case TokenType::TOKEN_FLOAT: return SemanticNumber;
                case TokenType::TOKEN_BOOL_LIT:
                case TokenType::TOKEN_IF:
                case TokenType::TOKEN_CLASS:
                case TokenType::TOKEN_FUNC:
                case TokenType::TOKEN_NEW:
                case TokenType::TOKEN_THIS:
                case TokenType::TOKEN_SUPER:
                case TokenType::TOKEN_RETURN:
                case TokenType::TOKEN_PUBLIC:
                case TokenType::TOKEN_PRIVATE:
                case TokenType::TOKEN_EXTENDS:
                case TokenType::TOKEN_INSTANCEOF:
                case TokenType::TOKEN_STATIC:
                case TokenType::TOKEN_USING:
                case TokenType::TOKEN_NONE:
                case TokenType::TOKEN_WHILE:
                case TokenType::TOKEN_FOR:
                case TokenType::TOKEN_BREAK:
                case TokenType::TOKEN_CONTINUE:
                case TokenType::TOKEN_IMPORT:
                case TokenType::TOKEN_COMPONENT:
                case TokenType::TOKEN_LET:
                case TokenType::TOKEN_TRAIT:
                case TokenType::TOKEN_IMPL:
                case TokenType::TOKEN_CONST:
                case TokenType::TOKEN_DYN:
                case TokenType::TOKEN_MATCH:
                case TokenType::TOKEN_PANIC:
                case TokenType::TOKEN_ENUM:
                    return SemanticKeyword;
                case TokenType::TOKEN_IDENT: {
                    if (const auto it = symbolKinds.find(token.loc.offset); it != symbolKinds.end()) {
                        switch (it->second) {
                            case SymbolKind::Function:
                            case SymbolKind::Component: return SemanticFunction;
                            case SymbolKind::Method:
                            case SymbolKind::StaticMethod: return SemanticMethod;
                            case SymbolKind::Field:
                            case SymbolKind::StaticField: return SemanticProperty;
                            case SymbolKind::Class: return SemanticClass;
                            case SymbolKind::Interface: return SemanticInterface;
                            case SymbolKind::Enum: return SemanticEnum;
                            case SymbolKind::EnumMember: return SemanticEnumMember;
                            case SymbolKind::Module: return SemanticNamespace;
                            case SymbolKind::Parameter: return SemanticParameter;
                            default: return SemanticVariable;
                        }
                    }

                    if (previous) {
                        switch (previous->type) {
                            case TokenType::TOKEN_FUNC: return SemanticFunction;
                            case TokenType::TOKEN_CLASS: return SemanticClass;
                            case TokenType::TOKEN_ENUM: return SemanticEnum;
                            case TokenType::TOKEN_COMPONENT: return SemanticFunction;
                            case TokenType::TOKEN_TRAIT: return SemanticInterface;
                            case TokenType::TOKEN_NEW: return SemanticClass;
                            case TokenType::TOKEN_DOT:
                            case TokenType::TOKEN_QUESTION_DOT:
                                return next && next->type == TokenType::TOKEN_LPAREN
                                    ? SemanticMethod
                                    : SemanticProperty;
                            default: break;
                        }
                    }

                    if (next && next->type == TokenType::TOKEN_LPAREN)
                        return SemanticFunction;

                    return SemanticVariable;
                }
                default:
                    return -1;
            }
        };

        for (std::size_t i = 0; i < lexed.size(); ++i) {
            const Token& token = lexed[i];
            const Token* previous = i > 0 ? &lexed[i - 1] : nullptr;
            const Token* next = i + 1 < lexed.size() ? &lexed[i + 1] : nullptr;

            const int type = tokenTypeOf(token.type, token, previous, next);
            if (type < 0)
                continue;

            std::size_t length = token.value.size();
            if (token.type == TokenType::TOKEN_STRING) {
                const std::size_t start = token.loc.offset;
                std::size_t end = start + 1;

                while (end < text.size() && text[end] != text[start] && text[end] != '\n') {
                    if (text[end] == '\\')
                        ++end;
                    ++end;
                }

                if (end < text.size() && text[end] == text[start])
                    ++end;

                length = end > start ? end - start : 1;
            }

            if (length == 0)
                length = 1;

            Position position = positionOf(text, token.loc.offset);
            const std::size_t end = std::min(token.loc.offset + length, text.size());
            std::size_t segment = token.loc.offset;

            for (std::size_t cursor = token.loc.offset; cursor < end; ++cursor) {
                if (text[cursor] != '\n')
                    continue;

                if (cursor > segment)
                    tokens.push_back({ position.line, position.character, cursor - segment, type });

                ++position.line;
                position.character = 0;
                segment = cursor + 1;
            }

            if (end > segment)
                tokens.push_back({ position.line, position.character, end - segment, type });
        }

        for (std::size_t i = 0; i < text.size();) {
            if (text[i] == '"' || text[i] == '\'') {
                const char quote = text[i++];
                while (i < text.size() && text[i] != quote) {
                    if (text[i] == '\\')
                        ++i;
                    ++i;
                }
                ++i;
                continue;
            }

            if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/') {
                const std::size_t start = i;
                while (i < text.size() && text[i] != '\n')
                    ++i;

                tokens.push_back({
                    positionOf(text, start).line,
                    positionOf(text, start).character,
                    i - start,
                    SemanticComment
                });
                continue;
            }

            if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*') {
                const std::size_t start = i;
                i += 2;
                while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/'))
                    ++i;
                i = std::min(i + 2, text.size());

                const Position position = positionOf(text, start);
                std::size_t segment = start;
                std::size_t line = position.line;
                std::size_t character = position.character;

                for (std::size_t cursor = start; cursor < i; ++cursor) {
                    if (text[cursor] != '\n')
                        continue;

                    if (cursor > segment)
                        tokens.push_back({ line, character, cursor - segment, SemanticComment });

                    ++line;
                    character = 0;
                    segment = cursor + 1;
                }

                if (i > segment)
                    tokens.push_back({ line, character, i - segment, SemanticComment });

                continue;
            }

            ++i;
        }

        std::stable_sort(tokens.begin(), tokens.end(), [](const TokenInfo& left, const TokenInfo& right) {
            return left.line < right.line ||
                (left.line == right.line && left.character < right.character);
        });

        SemanticTokens result;
        std::size_t lastLine = 0;
        std::size_t lastCharacter = 0;

        for (const auto& token : tokens) {
            const std::size_t deltaLine = token.line - lastLine;
            const std::size_t deltaStart = deltaLine == 0 ? token.character - lastCharacter : token.character;

            result.data.push_back(static_cast<std::uint32_t>(deltaLine));
            result.data.push_back(static_cast<std::uint32_t>(deltaStart));
            result.data.push_back(static_cast<std::uint32_t>(token.length));
            result.data.push_back(static_cast<std::uint32_t>(token.type));
            result.data.push_back(0);

            lastLine = token.line;
            lastCharacter = token.character;
        }

        return result;
    }

    std::vector<InlayHint> LanguageServer::inlayHintsAt(const std::string& text, const Range& range) {
        std::vector<InlayHint> result;
        const auto analysis = analyze(text);

        const auto before = [](const Position& left, const Position& right) {
            return left.line < right.line || (left.line == right.line && left.character < right.character);
        };

        for (const auto& hint : analysis.hints)
            if (!before(hint.position, range.start) && !before(range.end, hint.position))
                result.push_back(hint);

        return result;
    }

    std::vector<TextEdit> LanguageServer::formattingEdits(const std::string& text) {
        std::string formatted;
        std::size_t depth = 0;
        std::size_t lineStart = 0;

        while (lineStart < text.size()) {
            std::size_t newline = text.find('\n', lineStart);
            if (newline == std::string::npos)
                newline = text.size();

            const std::string line = text.substr(lineStart, newline - lineStart);
            const auto contentStart = line.find_first_not_of(" \t");

            if (contentStart == std::string::npos) {
                formatted += '\n';
            } else {
                const char first = line[contentStart];
                if (first == '}' || first == ']')
                    depth = depth == 0 ? 0 : depth - 1;

                formatted.append(depth * 4, ' ');
                formatted.append(line, contentStart, std::string::npos);
                formatted += '\n';

                const auto contentEnd = line.find_last_not_of(" \t");
                const char last = contentEnd == std::string::npos ? '\0' : line[contentEnd];
                if (last == '{' || last == '[')
                    ++depth;
            }

            if (newline == text.size())
                break;

            lineStart = newline + 1;
        }

        if (formatted.empty())
            return {};

        if (!text.ends_with('\n') && formatted.back() == '\n')
            formatted.pop_back();

        return { TextEdit{
            Range{ Position{ 0, 0 }, positionOf(text, text.size()) },
            std::move(formatted),
        } };
    }

    std::string LanguageServer::dispatch(const nlohmann::ordered_json& message) {
        if (!message.is_object() || !message.contains("method"))
            return {};

        const std::string method = message.value("method", std::string{});
        const nlohmann::ordered_json& id = message.contains("id") ? message.at("id") : nlohmann::ordered_json(nullptr);
        const nlohmann::ordered_json params = message.value("params", nlohmann::ordered_json::object());

        if (method == "initialize")
            return encodeMessage(makeResponse(id, initializeResult()));

        if (method == "initialized") {
            this->initialized = true;
            return {};
        }

        if (method == "shutdown")
            return encodeMessage(makeResponse(id, nlohmann::ordered_json(nullptr)));

        if (method == "exit" || method.starts_with("$/"))
            return {};

        if (method == "textDocument/didOpen") {
            this->openDocument(params);
            return this->diagnosticsNotification(this->uriOf(params));
        }

        if (method == "textDocument/didChange") {
            this->changeDocument(params);
            return this->diagnosticsNotification(this->uriOf(params));
        }

        if (method == "textDocument/didClose") {
            const std::string uri = this->uriOf(params);
            this->closeDocument(params);

            return encodeMessage(makeNotification("textDocument/publishDiagnostics",
                nlohmann::ordered_json{ { "uri", uri }, { "diagnostics", nlohmann::ordered_json::array() } }));
        }

        if (method == "textDocument/completion")
            return encodeMessage(makeResponse(id, this->documentCompletion(params)));

        if (method == "textDocument/hover")
            return encodeMessage(makeResponse(id, this->documentHover(params)));

        if (method == "textDocument/definition" || method == "textDocument/declaration")
            return encodeMessage(makeResponse(id, this->documentDefinition(params)));

        if (method == "textDocument/typeDefinition")
            return encodeMessage(makeResponse(id, this->documentTypeDefinition(params)));

        if (method == "textDocument/codeAction")
            return encodeMessage(makeResponse(id, this->documentCodeActions(params)));

        if (method == "textDocument/documentSymbol")
            return encodeMessage(makeResponse(id, this->documentSymbols(params)));

        if (method == "textDocument/references")
            return encodeMessage(makeResponse(id, this->documentReferences(params)));

        if (method == "textDocument/documentHighlight")
            return encodeMessage(makeResponse(id, this->documentHighlights(params)));

        if (method == "textDocument/rename")
            return encodeMessage(makeResponse(id, this->documentRename(params)));

        if (method == "textDocument/signatureHelp")
            return encodeMessage(makeResponse(id, this->documentSignatureHelp(params)));

        if (method == "textDocument/semanticTokens/full" ||
            method == "textDocument/semanticTokens/full/delta")
            return encodeMessage(makeResponse(id, this->documentSemanticTokens(params)));

        if (method == "textDocument/inlayHint")
            return encodeMessage(makeResponse(id, this->documentInlayHints(params)));

        if (method == "textDocument/formatting")
            return encodeMessage(makeResponse(id, this->documentFormatting(params)));

        return encodeMessage(makeError(id, -32601, "Method not found: " + method));
    }

    ImportResolver LanguageServer::importResolverFor(const std::string& uri) const {
        return [this, uri](const std::string& path) -> std::optional<ResolvedImport> {
            const std::string target = resolveUri(uri, path);

            if (const auto it = this->documents.find(target); it != this->documents.end())
                return ResolvedImport{ target, it->second.text };

            for (const auto& [documentUri, document] : this->documents) {
                if (documentUri == path || documentUri.ends_with("/" + path) ||
                    documentUri.ends_with(path))
                    return ResolvedImport{ documentUri, document.text };
            }

            return std::nullopt;
        };
    }

    std::string LanguageServer::diagnosticsNotification(const std::string& uri) const {
        const auto it = this->documents.find(uri);
        if (it == this->documents.end())
            return {};

        nlohmann::ordered_json diagnostics = nlohmann::ordered_json::array();
        for (const auto& diagnostic : analyze(it->second.text, this->importResolverFor(uri)).diagnostics)
            diagnostics.push_back(diagnostic);

        return encodeMessage(makeNotification("textDocument/publishDiagnostics",
            nlohmann::ordered_json{ { "uri", uri }, { "diagnostics", std::move(diagnostics) } }));
    }

    void LanguageServer::openDocument(const nlohmann::ordered_json& params) {
        if (!params.contains("textDocument"))
            return;

        const auto& document = params.at("textDocument");

        Document opened;
        opened.text = document.value("text", std::string{});
        opened.version = document.value("version", 0);

        this->documents.insert_or_assign(document.value("uri", std::string{}), std::move(opened));
    }

    void LanguageServer::changeDocument(const nlohmann::ordered_json& params) {
        const auto it = this->documents.find(this->uriOf(params));
        if (it == this->documents.end())
            return;

        if (params.contains("textDocument"))
            it->second.version = params.at("textDocument").value("version", it->second.version);

        if (!params.contains("contentChanges"))
            return;

        for (const auto& change : params.at("contentChanges")) {
            if (!change.contains("text"))
                continue;

            const std::string replacement = change.at("text").get<std::string>();

            if (!change.contains("range")) {
                it->second.text = replacement;
                continue;
            }

            const Range range = change.at("range").get<Range>();
            std::size_t start = offsetOf(it->second.text, range.start);
            std::size_t end = offsetOf(it->second.text, range.end);

            if (end < start)
                std::swap(start, end);

            it->second.text.replace(start, end - start, replacement);
        }
    }

    void LanguageServer::closeDocument(const nlohmann::ordered_json& params) {
        this->documents.erase(this->uriOf(params));
    }

    nlohmann::ordered_json LanguageServer::documentCompletion(const nlohmann::ordered_json& params) const {
        nlohmann::ordered_json items = nlohmann::ordered_json::array();

        if (params.contains("position")) {
            for (const auto& item : LanguageServer::completionsAt(
                    this->textOf(params), params.at("position").get<Position>(),
                    this->importResolverFor(this->uriOf(params))))
                items.push_back(item);
        }

        return nlohmann::ordered_json{ { "isIncomplete", false }, { "items", std::move(items) } };
    }

    nlohmann::ordered_json LanguageServer::documentHover(const nlohmann::ordered_json& params) const {
        if (!params.contains("position"))
            return nlohmann::ordered_json(nullptr);

        const auto hover = LanguageServer::hoverAt(
            this->textOf(params), params.at("position").get<Position>(),
            this->importResolverFor(this->uriOf(params)));

        return hover ? nlohmann::ordered_json(*hover) : nlohmann::ordered_json(nullptr);
    }

    nlohmann::ordered_json LanguageServer::documentDefinition(const nlohmann::ordered_json& params) const {
        if (!params.contains("position"))
            return nlohmann::ordered_json(nullptr);

        const auto location = LanguageServer::definitionAt(
            this->uriOf(params), this->textOf(params), params.at("position").get<Position>(),
            this->importResolverFor(this->uriOf(params)));

        return location ? nlohmann::ordered_json(*location) : nlohmann::ordered_json(nullptr);
    }

    nlohmann::ordered_json LanguageServer::documentCodeActions(const nlohmann::ordered_json& params) const {
        if (!params.contains("range"))
            return nlohmann::ordered_json::array();

        const std::string& text = this->textOf(params);
        const auto analysis = analyze(text);

        return LanguageServer::codeActionsAt(
            this->uriOf(params), text, params.at("range").get<Range>(), analysis.diagnostics);
    }

    nlohmann::ordered_json LanguageServer::documentSymbols(const nlohmann::ordered_json& params) const {
        nlohmann::ordered_json symbols = nlohmann::ordered_json::array();

        for (const auto& symbol : LanguageServer::symbolsAt(this->uriOf(params), this->textOf(params)))
            symbols.push_back(symbol);

        return symbols;
    }

    nlohmann::ordered_json LanguageServer::documentReferences(const nlohmann::ordered_json& params) const {
        if (!params.contains("position"))
            return nlohmann::ordered_json::array();

        nlohmann::ordered_json locations = nlohmann::ordered_json::array();

        for (const auto& location : LanguageServer::referencesAt(
                this->uriOf(params), this->textOf(params), params.at("position").get<Position>()))
            locations.push_back(location);

        return locations;
    }

    nlohmann::ordered_json LanguageServer::documentHighlights(const nlohmann::ordered_json& params) const {
        if (!params.contains("position"))
            return nlohmann::ordered_json::array();

        nlohmann::ordered_json highlights = nlohmann::ordered_json::array();

        for (const auto& highlight : LanguageServer::highlightsAt(
                this->textOf(params), params.at("position").get<Position>()))
            highlights.push_back(highlight);

        return highlights;
    }

    nlohmann::ordered_json LanguageServer::documentRename(const nlohmann::ordered_json& params) const {
        if (!params.contains("position"))
            return nlohmann::ordered_json::object();

        const std::string newName = params.value("newName", std::string{});
        const auto edits = LanguageServer::renameAt(
            this->textOf(params), params.at("position").get<Position>(), newName);

        if (edits.empty())
            return nlohmann::ordered_json::object();

        return nlohmann::ordered_json(WorkspaceEdit{ this->uriOf(params), edits });
    }

    nlohmann::ordered_json LanguageServer::documentTypeDefinition(const nlohmann::ordered_json& params) const {
        if (!params.contains("position"))
            return nlohmann::ordered_json(nullptr);

        const auto location = LanguageServer::typeDefinitionAt(
            this->uriOf(params), this->textOf(params), params.at("position").get<Position>(),
            this->importResolverFor(this->uriOf(params)));

        return location ? nlohmann::ordered_json(*location) : nlohmann::ordered_json(nullptr);
    }

    nlohmann::ordered_json LanguageServer::documentSignatureHelp(const nlohmann::ordered_json& params) const {
        if (!params.contains("position"))
            return nlohmann::ordered_json(nullptr);

        const auto help = LanguageServer::signatureHelpAt(
            this->textOf(params), params.at("position").get<Position>(),
            this->importResolverFor(this->uriOf(params)));

        return help ? nlohmann::ordered_json(*help) : nlohmann::ordered_json(nullptr);
    }

    nlohmann::ordered_json LanguageServer::documentSemanticTokens(const nlohmann::ordered_json& params) const {
        return nlohmann::ordered_json(LanguageServer::semanticTokens(this->textOf(params)));
    }

    nlohmann::ordered_json LanguageServer::documentInlayHints(const nlohmann::ordered_json& params) const {
        const std::string& text = this->textOf(params);
        const Range range = params.contains("range")
            ? params.at("range").get<Range>()
            : Range{ Position{ 0, 0 }, endPositionOf(text) };

        nlohmann::ordered_json hints = nlohmann::ordered_json::array();
        for (const auto& hint : LanguageServer::inlayHintsAt(text, range))
            hints.push_back(hint);

        return hints;
    }

    nlohmann::ordered_json LanguageServer::documentFormatting(const nlohmann::ordered_json& params) const {
        nlohmann::ordered_json edits = nlohmann::ordered_json::array();

        for (const auto& edit : LanguageServer::formattingEdits(this->textOf(params)))
            edits.push_back(edit);

        return edits;
    }

    const std::string& LanguageServer::textOf(const nlohmann::ordered_json& params) const {
        static const std::string empty;

        const auto it = this->documents.find(this->uriOf(params));

        return it == this->documents.end() ? empty : it->second.text;
    }

    std::string LanguageServer::uriOf(const nlohmann::ordered_json& params) const {
        if (!params.contains("textDocument"))
            return {};

        return params.at("textDocument").value("uri", std::string{});
    }
}
