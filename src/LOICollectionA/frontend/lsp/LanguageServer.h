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
            const std::string& text, const Position& position
        );

        LOICOLLECTION_A_NDAPI static std::optional<Hover> hoverAt(
            const std::string& text, const Position& position
        );

        LOICOLLECTION_A_NDAPI static std::optional<Location> definitionAt(
            const std::string& uri, const std::string& text, const Position& position
        );

    private:
        struct Document {
            std::string text;
            int version = 0;
        };

        std::string inbound;
        std::unordered_map<std::string, Document> documents;
        bool initialized = false;

        std::string dispatch(const nlohmann::ordered_json& message);

        std::string diagnosticsNotification(const std::string& uri) const;

        void openDocument(const nlohmann::ordered_json& params);
        void changeDocument(const nlohmann::ordered_json& params);
        void closeDocument(const nlohmann::ordered_json& params);

        nlohmann::ordered_json documentCompletion(const nlohmann::ordered_json& params) const;
        nlohmann::ordered_json documentHover(const nlohmann::ordered_json& params) const;
        nlohmann::ordered_json documentDefinition(const nlohmann::ordered_json& params) const;

        const std::string& textOf(const nlohmann::ordered_json& params) const;
        std::string uriOf(const nlohmann::ordered_json& params) const;
    };
}
