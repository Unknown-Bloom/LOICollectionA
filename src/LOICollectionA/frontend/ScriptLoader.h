#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/AST.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"

namespace LOICollection::frontend {
    class ScriptLoader {
    public:
        using FileReader = std::function<std::optional<std::string>(const std::string&)>;

        struct Result {
            std::unique_ptr<ProgramNode> program;
            std::vector<std::string> files;
            std::vector<std::string> hashes;
        };

        LOICOLLECTION_A_NDAPI static std::optional<Result> load(
            const std::string& entryPath,
            const std::string& rootDir,
            const FileReader& readFile,
            DiagnosticEngine& diagnostics
        );

    private:
        ScriptLoader() = default;
    };
}
