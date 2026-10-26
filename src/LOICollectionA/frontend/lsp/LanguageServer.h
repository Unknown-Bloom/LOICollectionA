#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/lsp/Analysis.h"
#include "LOICollectionA/frontend/lsp/Protocol.h"

namespace LOICollection::frontend::lsp {
    class LanguageServer {
    public:
        LOICOLLECTION_A_API std::string handle(std::string_view bytes);

        LOICOLLECTION_A_NDAPI static std::vector<CompletionItem> completionsAt(
            const std::string& text, const Position& position, const ImportResolver& resolver = {}
        );

        LOICOLLECTION_A_NDAPI static std::optional<Hover> hoverAt(
            const std::string& text, const Position& position, const ImportResolver& resolver = {}
        );

        LOICOLLECTION_A_NDAPI static std::optional<Location> definitionAt(
            const std::string& uri, const std::string& text, const Position& position,
            const ImportResolver& resolver = {}
        );

        LOICOLLECTION_A_NDAPI static std::vector<CodeAction> codeActionsAt(
            const std::string& uri, const std::string& text, const Range& range,
            const std::vector<Diagnostic>& diagnostics
        );

        LOICOLLECTION_A_NDAPI static std::vector<SymbolInformation> symbolsAt(
            const std::string& uri, const std::string& text
        );

        LOICOLLECTION_A_NDAPI static std::vector<Location> referencesAt(
            const std::string& uri, const std::string& text, const Position& position
        );

        LOICOLLECTION_A_NDAPI static std::vector<DocumentHighlight> highlightsAt(
            const std::string& text, const Position& position
        );

        LOICOLLECTION_A_NDAPI static std::vector<TextEdit> renameAt(
            const std::string& text, const Position& position, const std::string& newName
        );

        LOICOLLECTION_A_NDAPI static std::optional<Location> typeDefinitionAt(
            const std::string& uri, const std::string& text, const Position& position,
            const ImportResolver& resolver = {}
        );

        LOICOLLECTION_A_NDAPI static std::optional<SignatureHelp> signatureHelpAt(
            const std::string& text, const Position& position, const ImportResolver& resolver = {}
        );

        LOICOLLECTION_A_NDAPI static SemanticTokens semanticTokens(const std::string& text);

        LOICOLLECTION_A_NDAPI static std::vector<InlayHint> inlayHintsAt(
            const std::string& text, const Range& range
        );

        LOICOLLECTION_A_NDAPI static std::vector<TextEdit> formattingEdits(const std::string& text);

    private:
        struct Document {
            std::string text;
            int version = 0;
        };

        std::string inbound;
        std::unordered_map<std::string, Document> documents;
        bool initialized = false;

        ImportResolver importResolverFor(const std::string& uri) const;

        std::string dispatch(const nlohmann::ordered_json& message);

        std::string diagnosticsNotification(const std::string& uri) const;

        void openDocument(const nlohmann::ordered_json& params);
        void changeDocument(const nlohmann::ordered_json& params);
        void closeDocument(const nlohmann::ordered_json& params);

        nlohmann::ordered_json documentCompletion(const nlohmann::ordered_json& params) const;
        nlohmann::ordered_json documentHover(const nlohmann::ordered_json& params) const;
        nlohmann::ordered_json documentDefinition(const nlohmann::ordered_json& params) const;
        nlohmann::ordered_json documentCodeActions(const nlohmann::ordered_json& params) const;
        nlohmann::ordered_json documentSymbols(const nlohmann::ordered_json& params) const;
        nlohmann::ordered_json documentReferences(const nlohmann::ordered_json& params) const;
        nlohmann::ordered_json documentHighlights(const nlohmann::ordered_json& params) const;
        nlohmann::ordered_json documentRename(const nlohmann::ordered_json& params) const;
        nlohmann::ordered_json documentTypeDefinition(const nlohmann::ordered_json& params) const;
        nlohmann::ordered_json documentSignatureHelp(const nlohmann::ordered_json& params) const;
        nlohmann::ordered_json documentSemanticTokens(const nlohmann::ordered_json& params) const;
        nlohmann::ordered_json documentInlayHints(const nlohmann::ordered_json& params) const;
        nlohmann::ordered_json documentFormatting(const nlohmann::ordered_json& params) const;

        const std::string& textOf(const nlohmann::ordered_json& params) const;
        std::string uriOf(const nlohmann::ordered_json& params) const;
    };
}
