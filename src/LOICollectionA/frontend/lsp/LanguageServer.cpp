#include <algorithm>
#include <string_view>

#include "LOICollectionA/frontend/lsp/LanguageServer.h"

namespace LOICollection::frontend::lsp::detail {
    constexpr std::string_view kKeywords[] = {
        "if", "class", "func", "new", "this", "super", "return",
        "public", "private", "extends", "instanceof", "static", "using",
        "None", "while", "for", "in", "break", "continue",
        "import", "component", "let", "const", "trait", "impl",
        "panic",
        "true", "false"
    };

    constexpr std::string_view kTypes[] = {
        "int", "float", "string", "bool", "void", "optional", "variant"
    };

    constexpr std::string_view kBuiltins[] = {
        "tr", "score", "entity", "print", "println", "error"
    };

    constexpr std::string_view kFormClasses[] = {
        "CustomForm", "MessageBox", "PaginatedForm", "ScriptForm"
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

    std::string receiverBefore(const std::string& text, std::size_t offset) {
        if (offset > text.size())
            offset = text.size();

        std::size_t end = offset;
        while (end > 0 && isIdentifierChar(text[end - 1]))
            --end;

        if (end == 0 || text[end - 1] != '.')
            return {};

        std::size_t start = end - 1;
        while (start > 0 && isIdentifierChar(text[start - 1]))
            --start;

        return text.substr(start, end - 1 - start);
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

    const Symbol* findSymbol(const std::vector<Symbol>& symbols, const std::string& name) {
        const Symbol* fallback = nullptr;

        for (const auto& symbol : symbols) {
            if (symbol.name != name)
                continue;

            if (symbol.container.empty())
                return &symbol;

            if (!fallback)
                fallback = &symbol;
        }

        return fallback;
    }

    std::string classNameOf(const Symbol& symbol) {
        switch (symbol.kind) {
            case SymbolKind::Variable:
            case SymbolKind::Field:
            case SymbolKind::StaticField:
            case SymbolKind::Constant:
                break;
            default:
                return symbol.name;
        }

        std::string type = symbol.detail;
        if (type.starts_with("class "))
            type.erase(0, 6);

        if (const auto bracket = type.find('<'); bracket != std::string::npos)
            type.erase(bracket);

        return type;
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

    std::size_t declarationOffsetOf(const std::string& text, const Symbol& symbol) {
        const std::size_t start = offsetOf(text, symbol.range.start);
        const auto newline = text.find('\n', start);
        const std::size_t limit = newline == std::string::npos ? text.size() : newline;

        for (const std::size_t offset : wordOccurrences(text, symbol.name))
            if (offset >= start && offset < limit)
                return offset;

        return start;
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
            case SymbolKind::Constant: return CompletionKind::Constant;
            default: return CompletionKind::Variable;
        }
    }

    nlohmann::ordered_json initializeResult() {
        return nlohmann::ordered_json{
            {
                "capabilities", nlohmann::ordered_json{
                    { "textDocumentSync", 1 },
                    { "completionProvider", nlohmann::ordered_json{
                        { "triggerCharacters", nlohmann::ordered_json::array({ "." }) },
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
        const std::string& text, const Position& position
    ) {
        std::vector<CompletionItem> items;

        const auto analysis = analyze(text);
        const std::size_t offset = offsetOf(text, position);

        if (const auto receiver = receiverBefore(text, offset); !receiver.empty()) {
            std::string className = receiver;

            if (receiver == "this")
                className = enclosingClass(text, offset);
            else if (const auto* symbol = findSymbol(analysis.symbols, receiver))
                className = classNameOf(*symbol);

            if (className.empty())
                return items;

            for (const auto& symbol : analysis.symbols)
                if (symbol.container == className && !symbol.name.empty())
                    items.push_back({ symbol.name, completionKindOf(symbol.kind), symbol.detail });

            return items;
        }

        for (const auto& symbol : analysis.symbols)
            if (symbol.container.empty() && !symbol.name.empty())
                items.push_back({ symbol.name, completionKindOf(symbol.kind), symbol.detail });

        for (const auto& keyword : kKeywords)
            items.push_back({ std::string(keyword), CompletionKind::Keyword, "keyword" });

        for (const auto& type : kTypes)
            items.push_back({ std::string(type), CompletionKind::Keyword, "type" });

        for (const auto& builtin : kBuiltins)
            items.push_back({ std::string(builtin), CompletionKind::Function, "builtin" });

        for (const auto& form : kFormClasses)
            items.push_back({ std::string(form), CompletionKind::Class, "form" });

        return items;
    }

    std::optional<Hover> LanguageServer::hoverAt(const std::string& text, const Position& position) {
        const std::string word = wordAt(text, offsetOf(text, position));
        if (word.empty())
            return std::nullopt;

        const auto analysis = analyze(text);
        const auto* symbol = findSymbol(analysis.symbols, word);
        if (!symbol)
            return std::nullopt;

        Hover hover;
        hover.contents = symbol->detail.empty() ? symbol->name : symbol->detail;
        hover.range = symbol->range;

        return hover;
    }

    std::optional<Location> LanguageServer::definitionAt(
        const std::string& uri, const std::string& text, const Position& position
    ) {
        const std::string word = wordAt(text, offsetOf(text, position));
        if (word.empty())
            return std::nullopt;

        const auto analysis = analyze(text);
        const auto* symbol = findSymbol(analysis.symbols, word);
        if (!symbol)
            return std::nullopt;

        return Location{ uri, symbol->range };
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
            if (!symbol.name.empty())
                result.push_back({ symbol.name, symbol.kind, uri, symbol.range, symbol.container });

        return result;
    }

    std::vector<Location> LanguageServer::referencesAt(
        const std::string& uri, const std::string& text, const Position& position
    ) {
        const std::string word = wordAt(text, offsetOf(text, position));
        if (word.empty())
            return {};

        const auto analysis = analyze(text);
        const auto* symbol = findSymbol(analysis.symbols, word);

        const std::size_t declarationOffset = symbol
            ? declarationOffsetOf(text, *symbol)
            : std::string::npos;

        std::vector<Location> result;
        for (const std::size_t offset : wordOccurrences(text, word))
            if (offset != declarationOffset)
                result.push_back(Location{ uri, rangeAt(text, offset, word.size()) });

        return result;
    }

    std::vector<DocumentHighlight> LanguageServer::highlightsAt(
        const std::string& text, const Position& position
    ) {
        const std::string word = wordAt(text, offsetOf(text, position));
        if (word.empty())
            return {};

        const auto analysis = analyze(text);
        const auto* symbol = findSymbol(analysis.symbols, word);

        const std::size_t declarationOffset = symbol
            ? declarationOffsetOf(text, *symbol)
            : std::string::npos;

        std::vector<DocumentHighlight> result;
        for (const std::size_t offset : wordOccurrences(text, word)) {
            const bool write = offset == declarationOffset;
            result.push_back({ rangeAt(text, offset, word.size()), write ? 3 : 2 });
        }

        return result;
    }

    std::vector<TextEdit> LanguageServer::renameAt(
        const std::string& text, const Position& position, const std::string& newName
    ) {
        const std::string word = wordAt(text, offsetOf(text, position));
        if (word.empty() || newName.empty())
            return {};

        std::vector<TextEdit> edits;
        for (const std::size_t offset : wordOccurrences(text, word))
            edits.push_back(TextEdit{ rangeAt(text, offset, word.size()), newName });

        return edits;
    }

    std::optional<Location> LanguageServer::typeDefinitionAt(
        const std::string& uri, const std::string& text, const Position& position
    ) {
        const std::string word = wordAt(text, offsetOf(text, position));
        if (word.empty())
            return std::nullopt;

        const auto analysis = analyze(text);
        const auto* symbol = findSymbol(analysis.symbols, word);
        if (!symbol)
            return std::nullopt;

        const std::string type = classNameOf(*symbol);
        if (type.empty() || type == word)
            return std::nullopt;

        for (const auto& candidate : analysis.symbols)
            if (candidate.kind == SymbolKind::Class && candidate.name == type)
                return Location{ uri, candidate.range };

        return std::nullopt;
    }

    std::optional<SignatureHelp> LanguageServer::signatureHelpAt(
        const std::string& text, const Position& position
    ) {
        const std::size_t cursor = offsetOf(text, position);

        std::size_t open = std::string::npos;
        for (std::size_t i = cursor; i > 0; --i) {
            if (text[i - 1] == '(') {
                open = i - 1;
                break;
            }
        }

        if (open == std::string::npos)
            return std::nullopt;

        for (std::size_t i = open + 1; i < cursor; ++i)
            if (text[i] == ')')
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

        const auto analysis = analyze(text);
        const auto* symbol = findSymbol(analysis.symbols, callee);
        if (!symbol)
            return std::nullopt;

        const auto openParen = symbol->detail.find('(');
        const auto closeParen = symbol->detail.find(')', openParen);
        if (openParen == std::string::npos || closeParen == std::string::npos)
            return std::nullopt;

        SignatureHelp help;
        help.signatures.push_back({ symbol->detail, {} });

        const std::string body = symbol->detail.substr(
            openParen + 1, closeParen - openParen - 1);

        std::size_t segmentStart = 0;
        for (std::size_t i = 0; i <= body.size(); ++i) {
            if (i != body.size() && body[i] != ',')
                continue;

            const auto trimmed = body.substr(segmentStart, i - segmentStart);
            const auto first = trimmed.find_first_not_of(" \t");
            const auto last = trimmed.find_last_not_of(" \t");

            if (first != std::string::npos)
                help.signatures.front().parameters.push_back(
                    trimmed.substr(first, last - first + 1));

            segmentStart = i + 1;
        }

        int active = 0;
        for (std::size_t i = open + 1; i < cursor; ++i)
            if (text[i] == ',')
                ++active;

        help.activeParameter = active;

        return help;
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

        if (method == "textDocument/formatting")
            return encodeMessage(makeResponse(id, this->documentFormatting(params)));

        return encodeMessage(makeError(id, -32601, "Method not found: " + method));
    }

    std::string LanguageServer::diagnosticsNotification(const std::string& uri) const {
        const auto it = this->documents.find(uri);
        if (it == this->documents.end())
            return {};

        nlohmann::ordered_json diagnostics = nlohmann::ordered_json::array();
        for (const auto& diagnostic : analyze(it->second.text).diagnostics)
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

        for (const auto& change : params.at("contentChanges"))
            if (change.contains("text"))
                it->second.text = change.at("text").get<std::string>();
    }

    void LanguageServer::closeDocument(const nlohmann::ordered_json& params) {
        this->documents.erase(this->uriOf(params));
    }

    nlohmann::ordered_json LanguageServer::documentCompletion(const nlohmann::ordered_json& params) const {
        nlohmann::ordered_json items = nlohmann::ordered_json::array();

        if (params.contains("position")) {
            for (const auto& item : LanguageServer::completionsAt(
                    this->textOf(params), params.at("position").get<Position>()))
                items.push_back(item);
        }

        return nlohmann::ordered_json{ { "isIncomplete", false }, { "items", std::move(items) } };
    }

    nlohmann::ordered_json LanguageServer::documentHover(const nlohmann::ordered_json& params) const {
        if (!params.contains("position"))
            return nlohmann::ordered_json(nullptr);

        const auto hover = LanguageServer::hoverAt(
            this->textOf(params), params.at("position").get<Position>());

        return hover ? nlohmann::ordered_json(*hover) : nlohmann::ordered_json(nullptr);
    }

    nlohmann::ordered_json LanguageServer::documentDefinition(const nlohmann::ordered_json& params) const {
        if (!params.contains("position"))
            return nlohmann::ordered_json(nullptr);

        const auto location = LanguageServer::definitionAt(
            this->uriOf(params), this->textOf(params), params.at("position").get<Position>());

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
            this->uriOf(params), this->textOf(params), params.at("position").get<Position>());

        return location ? nlohmann::ordered_json(*location) : nlohmann::ordered_json(nullptr);
    }

    nlohmann::ordered_json LanguageServer::documentSignatureHelp(const nlohmann::ordered_json& params) const {
        if (!params.contains("position"))
            return nlohmann::ordered_json(nullptr);

        const auto help = LanguageServer::signatureHelpAt(
            this->textOf(params), params.at("position").get<Position>());

        return help ? nlohmann::ordered_json(*help) : nlohmann::ordered_json(nullptr);
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
