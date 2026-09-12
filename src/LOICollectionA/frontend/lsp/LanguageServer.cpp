#include <algorithm>
#include <string_view>

#include "LOICollectionA/frontend/lsp/LanguageServer.h"

namespace LOICollection::frontend::lsp {
    namespace {
        constexpr std::string_view kKeywords[] = {
            "if", "class", "func", "new", "this", "super", "return",
            "public", "private", "extends", "instanceof", "static", "using",
            "None", "while", "for", "in", "break", "continue",
            "import", "component", "let", "const", "trait", "impl",
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
                    },
                },
                { "serverInfo", nlohmann::ordered_json{ { "name", "LOICollectionA LCUI" } } },
            };
        }
    }

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

        if (method == "textDocument/definition")
            return encodeMessage(makeResponse(id, this->documentDefinition(params)));

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
