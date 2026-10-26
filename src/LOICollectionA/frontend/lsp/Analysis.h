#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/lsp/Protocol.h"

namespace LOICollection::frontend::lsp {
    struct ResolvedImport {
        std::string uri;
        std::string text;
    };

    using ImportResolver = std::function<std::optional<ResolvedImport>(const std::string& path)>;

    struct DocumentAnalysis {
        std::vector<Diagnostic> diagnostics;
        std::vector<Symbol> symbols;
        std::vector<Symbol> imported;
        std::vector<InlayHint> hints;
    };

    LOICOLLECTION_A_NDAPI DocumentAnalysis analyze(
        const std::string& text, const ImportResolver& resolver = {}
    );
}
