#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "LOICollectionA/frontend/lsp/Analysis.h"
#include "LOICollectionA/frontend/lsp/LanguageServer.h"
#include "LOICollectionA/frontend/lsp/Protocol.h"

namespace LOICollection::frontend::lsp {
    namespace {
        constexpr const char* kSource =
            "class Deck {\n"
            "    public:\n"
            "    cards = [];\n"
            "    func size() -> int { return 0; }\n"
            "}\n"
            "let deck: Deck = new Deck();\n"
            "let n = deck.size();\n";

        constexpr const char* kUri = "file:///deck.lcui";

        bool hasLabel(const std::vector<CompletionItem>& items, const std::string& label) {
            for (const auto& item : items)
                if (item.label == label)
                    return true;

            return false;
        }

        bool hasError(const std::vector<Diagnostic>& diagnostics) {
            for (const auto& diagnostic : diagnostics)
                if (diagnostic.severity == Severity::Error)
                    return true;

            return false;
        }

        nlohmann::ordered_json request(int id, const std::string& method, const nlohmann::ordered_json& params) {
            return nlohmann::ordered_json{
                { "jsonrpc", "2.0" },
                { "id", id },
                { "method", method },
                { "params", params },
            };
        }

        nlohmann::ordered_json singleMessage(const std::string& encoded) {
            std::string buffer = encoded;
            nlohmann::ordered_json message;

            return decodeMessage(buffer, message) ? message : nlohmann::ordered_json(nullptr);
        }
    }

    TEST(LspTest, AnalyzeCollectsDeclarations) {
        const auto analysis = analyze(kSource);

        EXPECT_FALSE(hasError(analysis.diagnostics));

        bool hasClass = false;
        bool hasMethod = false;
        bool hasVariable = false;

        for (const auto& symbol : analysis.symbols) {
            if (symbol.name == "Deck" && symbol.kind == SymbolKind::Class)
                hasClass = true;
            if (symbol.name == "size" && symbol.container == "Deck")
                hasMethod = true;
            if (symbol.name == "deck" && symbol.kind == SymbolKind::Variable)
                hasVariable = true;
        }

        EXPECT_TRUE(hasClass);
        EXPECT_TRUE(hasMethod);
        EXPECT_TRUE(hasVariable);
    }

    TEST(LspTest, AnalyzeReportsSyntaxErrors) {
        const auto analysis = analyze("let = ;");

        EXPECT_TRUE(hasError(analysis.diagnostics));
        EXPECT_FALSE(analysis.diagnostics.empty());
        EXPECT_EQ(analysis.diagnostics.front().range.start.line, 0u);
    }

    TEST(LspTest, CompletionOffersGlobalsAndKeywords) {
        const auto items = LanguageServer::completionsAt(kSource, Position{ 6, 0 });

        EXPECT_TRUE(hasLabel(items, "Deck"));
        EXPECT_TRUE(hasLabel(items, "let"));
        EXPECT_TRUE(hasLabel(items, "class"));
    }

    TEST(LspTest, CompletionOffersMembersOfTypedReceiver) {
        const auto items = LanguageServer::completionsAt(kSource, Position{ 6, 13 });

        EXPECT_TRUE(hasLabel(items, "size"));
        EXPECT_TRUE(hasLabel(items, "cards"));
        EXPECT_FALSE(hasLabel(items, "Deck"));
    }

    TEST(LspTest, HoverResolvesDeclaration) {
        const auto hover = LanguageServer::hoverAt(kSource, Position{ 6, 13 });

        ASSERT_TRUE(hover.has_value());
        EXPECT_NE(hover->contents.find("size()"), std::string::npos);
        ASSERT_TRUE(hover->range.has_value());
        EXPECT_EQ(hover->range->start.line, 3u);
    }

    TEST(LspTest, DefinitionResolvesDeclaration) {
        const auto location = LanguageServer::definitionAt(kUri, kSource, Position{ 0, 7 });

        ASSERT_TRUE(location.has_value());
        EXPECT_EQ(location->uri, kUri);
        EXPECT_EQ(location->range.start.line, 0u);
        EXPECT_EQ(location->range.start.character, 0u);
    }

    TEST(LspTest, DecodeMessageWaitsForPartialInput) {
        const std::string encoded = encodeMessage(makeNotification("$/ping", nlohmann::ordered_json::object()));

        std::string buffer;
        nlohmann::ordered_json message;

        const std::size_t split = encoded.size() / 2;

        buffer.append(encoded, 0, split);
        EXPECT_FALSE(decodeMessage(buffer, message));

        buffer.append(encoded, split, encoded.size() - split);
        ASSERT_TRUE(decodeMessage(buffer, message));
        EXPECT_EQ(message.value("method", std::string{}), "$/ping");
        EXPECT_TRUE(buffer.empty());
    }

    TEST(LspTest, ServerHandshakeAndDiagnostics) {
        LanguageServer server;

        const auto initialize = singleMessage(
            server.handle(encodeMessage(request(1, "initialize", nlohmann::ordered_json::object()))));

        ASSERT_TRUE(initialize.is_object());
        EXPECT_EQ(initialize.value("id", 0), 1);
        EXPECT_TRUE(initialize.at("result").contains("capabilities"));

        nlohmann::ordered_json document;
        document["uri"] = kUri;
        document["languageId"] = "lcui";
        document["version"] = 1;
        document["text"] = kSource;

        nlohmann::ordered_json didOpen;
        didOpen["textDocument"] = document;

        const auto published = singleMessage(
            server.handle(encodeMessage(makeNotification("textDocument/didOpen", didOpen))));

        ASSERT_TRUE(published.is_object());
        EXPECT_EQ(published.value("method", std::string{}), "textDocument/publishDiagnostics");
        EXPECT_EQ(published.at("params").value("uri", std::string{}), kUri);
        EXPECT_TRUE(published.at("params").at("diagnostics").empty());
    }

    TEST(LspTest, ServerAnswersCompletionAndHover) {
        LanguageServer server;

        nlohmann::ordered_json document;
        document["uri"] = kUri;
        document["languageId"] = "lcui";
        document["version"] = 1;
        document["text"] = kSource;

        nlohmann::ordered_json didOpen;
        didOpen["textDocument"] = document;

        server.handle(encodeMessage(makeNotification("textDocument/didOpen", didOpen)));

        nlohmann::ordered_json completionParams;
        completionParams["textDocument"] = nlohmann::ordered_json{ { "uri", kUri } };
        completionParams["position"] = Position{ 6, 13 };

        const auto completion = singleMessage(server.handle(
            encodeMessage(request(2, "textDocument/completion", completionParams))));

        ASSERT_TRUE(completion.is_object());
        EXPECT_FALSE(completion.at("result").at("items").empty());

        nlohmann::ordered_json hoverParams;
        hoverParams["textDocument"] = nlohmann::ordered_json{ { "uri", kUri } };
        hoverParams["position"] = Position{ 6, 13 };

        const auto hover = singleMessage(server.handle(
            encodeMessage(request(3, "textDocument/hover", hoverParams))));

        ASSERT_TRUE(hover.is_object());
        EXPECT_TRUE(hover.at("result").contains("contents"));
    }

    TEST(LspTest, CodeActionDeclaresAMissingVariable) {
        const std::string text =
            "func total() -> int {\n"
            "    sum = 1;\n"
            "    return sum;\n"
            "}\n";

        const auto analysis = analyze(text);
        ASSERT_FALSE(analysis.diagnostics.empty());

        const Range cursor{ Position{ 1, 4 }, Position{ 1, 4 } };
        const auto actions = LanguageServer::codeActionsAt(kUri, text, cursor, analysis.diagnostics);

        ASSERT_EQ(actions.size(), 1u);
        EXPECT_EQ(actions.front().kind, "quickfix");
        EXPECT_NE(actions.front().title.find("sum"), std::string::npos);
        ASSERT_EQ(actions.front().edits.size(), 1u);
        EXPECT_EQ(actions.front().edits.front().newText, "let ");
        EXPECT_EQ(actions.front().edits.front().range.start.line, 1u);
        EXPECT_EQ(actions.front().edits.front().range.start.character, 4u);
    }

    TEST(LspTest, CodeActionIgnoresUnrelatedDiagnostics) {
        const std::string text = "func total() -> int { return 1; }\n";

        const auto analysis = analyze(text);
        const Range cursor{ Position{ 0, 0 }, Position{ 0, 0 } };

        EXPECT_TRUE(LanguageServer::codeActionsAt(kUri, text, cursor, analysis.diagnostics).empty());
    }

    TEST(LspTest, ServerAnswersCodeAction) {
        LanguageServer server;

        const std::string text =
            "func total() -> int {\n"
            "    sum = 1;\n"
            "    return sum;\n"
            "}\n";

        nlohmann::ordered_json document;
        document["uri"] = kUri;
        document["languageId"] = "lcui";
        document["version"] = 1;
        document["text"] = text;

        nlohmann::ordered_json didOpen;
        didOpen["textDocument"] = document;

        server.handle(encodeMessage(makeNotification("textDocument/didOpen", didOpen)));

        nlohmann::ordered_json params;
        params["textDocument"] = nlohmann::ordered_json{ { "uri", kUri } };
        params["range"] = Range{ Position{ 1, 4 }, Position{ 1, 4 } };
        params["context"] = nlohmann::ordered_json{ { "diagnostics", nlohmann::ordered_json::array() } };

        const auto response = singleMessage(server.handle(
            encodeMessage(request(5, "textDocument/codeAction", params))));

        ASSERT_TRUE(response.is_object());
        ASSERT_TRUE(response.at("result").is_array());
        ASSERT_FALSE(response.at("result").empty());
        EXPECT_EQ(response.at("result").front().value("kind", std::string{}), "quickfix");
        EXPECT_EQ(response.at("result").front().at("edit").at("changes").at(kUri).front()
            .value("newText", std::string{}), "let ");
    }

    TEST(LspTest, SymbolsListDeclarationsWithContainers) {
        const auto symbols = LanguageServer::symbolsAt(kUri, kSource);

        bool hasClass = false;
        bool hasMethod = false;
        bool hasField = false;
        bool hasVariable = false;

        for (const auto& symbol : symbols) {
            EXPECT_EQ(symbol.uri, kUri);

            if (symbol.name == "Deck" && symbol.kind == SymbolKind::Class)
                hasClass = true;
            if (symbol.name == "size" && symbol.kind == SymbolKind::Method
                && symbol.containerName == "Deck")
                hasMethod = true;
            if (symbol.name == "cards" && symbol.kind == SymbolKind::Field)
                hasField = true;
            if (symbol.name == "deck" && symbol.kind == SymbolKind::Variable)
                hasVariable = true;
        }

        EXPECT_TRUE(hasClass);
        EXPECT_TRUE(hasMethod);
        EXPECT_TRUE(hasField);
        EXPECT_TRUE(hasVariable);
    }

    TEST(LspTest, ReferencesSkipTheDeclaration) {
        const auto references = LanguageServer::referencesAt(kUri, kSource, Position{ 5, 12 });

        ASSERT_EQ(references.size(), 2u);
        for (const auto& location : references) {
            EXPECT_EQ(location.uri, kUri);
            EXPECT_EQ(location.range.start.line, 5u);
        }
    }

    TEST(LspTest, HighlightsMarkTheDeclarationAsWrite) {
        const auto highlights = LanguageServer::highlightsAt(kSource, Position{ 5, 12 });

        ASSERT_EQ(highlights.size(), 3u);

        bool sawWrite = false;
        for (const auto& highlight : highlights) {
            if (highlight.range.start.line == 0u) {
                EXPECT_EQ(highlight.kind, 3);
                sawWrite = true;
            } else {
                EXPECT_EQ(highlight.kind, 2);
            }
        }

        EXPECT_TRUE(sawWrite);
    }

    TEST(LspTest, RenameReplacesEveryOccurrence) {
        const auto edits = LanguageServer::renameAt(kSource, Position{ 5, 12 }, "Pile");

        ASSERT_EQ(edits.size(), 3u);
        for (const auto& edit : edits)
            EXPECT_EQ(edit.newText, "Pile");
    }

    TEST(LspTest, TypeDefinitionResolvesTheVariableClass) {
        const auto location = LanguageServer::typeDefinitionAt(kUri, kSource, Position{ 5, 7 });

        ASSERT_TRUE(location.has_value());
        EXPECT_EQ(location->uri, kUri);
        EXPECT_EQ(location->range.start.line, 0u);
    }

    TEST(LspTest, SignatureHelpReadsParametersAndCursor) {
        const std::string text =
            "class Vault {\n"
            "    public:\n"
            "    func take(index: int, count: int) -> int { return index; }\n"
            "}\n"
            "let vault = new Vault();\n"
            "let got = vault.take(0, 1);\n";

        const auto first = LanguageServer::signatureHelpAt(text, Position{ 5, 21 });
        ASSERT_TRUE(first.has_value());
        ASSERT_EQ(first->signatures.size(), 1u);
        EXPECT_EQ(first->signatures.front().label, "take(index: int, count: int) -> int");
        ASSERT_EQ(first->signatures.front().parameters.size(), 2u);
        EXPECT_EQ(first->signatures.front().parameters[0], "index: int");
        EXPECT_EQ(first->signatures.front().parameters[1], "count: int");
        EXPECT_EQ(first->activeParameter, 0);

        const auto second = LanguageServer::signatureHelpAt(text, Position{ 5, 24 });
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(second->activeParameter, 1);
    }

    TEST(LspTest, FormattingReindentsBothBlockStyles) {
        const std::string text =
            "class Deck {\n"
            "cards = [];\n"
            "func take(n: int) -> int {\n"
            "while (n > 0) [\n"
            "n = n - 1;\n"
            "]\n"
            "return n;\n"
            "}\n"
            "}\n";

        const auto edits = LanguageServer::formattingEdits(text);

        ASSERT_EQ(edits.size(), 1u);
        EXPECT_EQ(edits.front().newText,
            "class Deck {\n"
            "    cards = [];\n"
            "    func take(n: int) -> int {\n"
            "        while (n > 0) [\n"
            "            n = n - 1;\n"
            "        ]\n"
            "        return n;\n"
            "    }\n"
            "}\n");
    }

    TEST(LspTest, ServerAnswersDocumentQueries) {
        LanguageServer server;

        nlohmann::ordered_json document;
        document["uri"] = kUri;
        document["languageId"] = "lcui";
        document["version"] = 1;
        document["text"] = kSource;

        nlohmann::ordered_json didOpen;
        didOpen["textDocument"] = document;

        server.handle(encodeMessage(makeNotification("textDocument/didOpen", didOpen)));

        auto query = [&server](int id, const std::string& method, nlohmann::ordered_json params) {
            return singleMessage(server.handle(encodeMessage(request(id, method, params))));
        };

        nlohmann::ordered_json symbolParams;
        symbolParams["textDocument"] = nlohmann::ordered_json{ { "uri", kUri } };

        const auto symbols = query(2, "textDocument/documentSymbol", symbolParams);
        ASSERT_TRUE(symbols.at("result").is_array());
        EXPECT_FALSE(symbols.at("result").empty());

        nlohmann::ordered_json posParams;
        posParams["textDocument"] = nlohmann::ordered_json{ { "uri", kUri } };
        posParams["position"] = Position{ 5, 12 };

        const auto references = query(3, "textDocument/references", posParams);
        ASSERT_TRUE(references.at("result").is_array());
        EXPECT_EQ(references.at("result").size(), 2u);

        const auto highlights = query(4, "textDocument/documentHighlight", posParams);
        ASSERT_TRUE(highlights.at("result").is_array());
        EXPECT_EQ(highlights.at("result").size(), 3u);

        nlohmann::ordered_json renameParams = posParams;
        renameParams["newName"] = "Pile";

        const auto rename = query(5, "textDocument/rename", renameParams);
        EXPECT_EQ(rename.at("result").at("changes").at(kUri).size(), 3u);

        const auto typeDefinition = query(6, "textDocument/typeDefinition",
            nlohmann::ordered_json{
                { "textDocument", nlohmann::ordered_json{ { "uri", kUri } } },
                { "position", Position{ 5, 7 } },
            });

        ASSERT_TRUE(typeDefinition.at("result").is_object());
        EXPECT_EQ(typeDefinition.at("result").value("uri", std::string{}), kUri);

        nlohmann::ordered_json formatParams;
        formatParams["textDocument"] = nlohmann::ordered_json{ { "uri", kUri } };

        const auto formatting = query(7, "textDocument/formatting", formatParams);
        ASSERT_TRUE(formatting.at("result").is_array());
        EXPECT_EQ(formatting.at("result").size(), 1u);
    }

    TEST(LspTest, DeclarationAliasesDefinition) {
        LanguageServer server;

        nlohmann::ordered_json document;
        document["uri"] = kUri;
        document["languageId"] = "lcui";
        document["version"] = 1;
        document["text"] = kSource;

        nlohmann::ordered_json didOpen;
        didOpen["textDocument"] = document;

        server.handle(encodeMessage(makeNotification("textDocument/didOpen", didOpen)));

        nlohmann::ordered_json params;
        params["textDocument"] = nlohmann::ordered_json{ { "uri", kUri } };
        params["position"] = Position{ 0, 7 };

        const auto declaration = singleMessage(server.handle(
            encodeMessage(request(8, "textDocument/declaration", params))));

        ASSERT_TRUE(declaration.at("result").is_object());
        EXPECT_EQ(declaration.at("result").at("range").at("start").at("line"), 0u);
    }

    TEST(LspTest, ServerRejectsUnknownMethod) {
        LanguageServer server;

        const auto error = singleMessage(
            server.handle(encodeMessage(request(4, "textDocument/unknown", nlohmann::ordered_json::object()))));

        ASSERT_TRUE(error.is_object());
        EXPECT_TRUE(error.contains("error"));
        EXPECT_EQ(error.at("error").value("code", 0), -32601);
    }
}
