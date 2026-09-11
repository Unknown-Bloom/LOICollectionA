#pragma once

#include <string>
#include <vector>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/lsp/Protocol.h"

namespace LOICollection::frontend::lsp {
    struct DocumentAnalysis {
        std::vector<Diagnostic> diagnostics;
        std::vector<Symbol> symbols;
    };

    LOICOLLECTION_A_NDAPI DocumentAnalysis analyze(const std::string& text);
}
