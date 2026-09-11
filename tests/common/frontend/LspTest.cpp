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

        nlohmann::json request(int id, const std::string& method, const nlohmann::json& params) {
            return nlohmann::json{
                { "jsonrpc", "2.0" },
                { "id", id },
                { "method", method },
                { "params", params },
            };
        }

        nlohmann::json singleMessage(const std::string& encoded) {
            std::string buffer = encoded;
            nlohmann::json message;

            return decodeMessage(buffer, message) ? message : nlohmann::json(nullptr);
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
        const std::string encoded = encodeMessage(makeNotification("$/ping", nlohmann::json::object()));

        std::string buffer;
        nlohmann::json message;

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
            server.handle(encodeMessage(request(1, "initialize", nlohmann::json::object()))));

        ASSERT_TRUE(initialize.is_object());
        EXPECT_EQ(initialize.value("id", 0), 1);
        EXPECT_TRUE(initialize.at("result").contains("capabilities"));

        nlohmann::json document;
        document["uri"] = kUri;
        document["languageId"] = "lcui";
        document["version"] = 1;
        document["text"] = kSource;

        nlohmann::json didOpen;
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

        nlohmann::json document;
        document["uri"] = kUri;
        document["languageId"] = "lcui";
        document["version"] = 1;
        document["text"] = kSource;

        nlohmann::json didOpen;
        didOpen["textDocument"] = document;

        server.handle(encodeMessage(makeNotification("textDocument/didOpen", didOpen)));

        nlohmann::json completionParams;
        completionParams["textDocument"] = nlohmann::json{ { "uri", kUri } };
        completionParams["position"] = Position{ 6, 13 };

        const auto completion = singleMessage(server.handle(
            encodeMessage(request(2, "textDocument/completion", completionParams))));

        ASSERT_TRUE(completion.is_object());
        EXPECT_FALSE(completion.at("result").at("items").empty());

        nlohmann::json hoverParams;
        hoverParams["textDocument"] = nlohmann::json{ { "uri", kUri } };
        hoverParams["position"] = Position{ 6, 13 };

        const auto hover = singleMessage(server.handle(
            encodeMessage(request(3, "textDocument/hover", hoverParams))));

        ASSERT_TRUE(hover.is_object());
        EXPECT_TRUE(hover.at("result").contains("contents"));
    }

    TEST(LspTest, ServerRejectsUnknownMethod) {
        LanguageServer server;

        const auto error = singleMessage(
            server.handle(encodeMessage(request(4, "textDocument/unknown", nlohmann::json::object()))));

        ASSERT_TRUE(error.is_object());
        EXPECT_TRUE(error.contains("error"));
        EXPECT_EQ(error.at("error").value("code", 0), -32601);
    }
}
