#pragma once

/* Multi-file import resolution (§6.2).
 *
 * `import "common/components.lcui";` pulls the target file's top-level
 * definitions (class / func / using) into the importing file's top-level
 * scope. Imported files must contain definitions only — no executable
 * top-level statements — so an import can never run UI-building code as
 * a side effect. Circular imports and name collisions are compile errors
 * reported with the full import cycle / both definition sites. */

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
        /* Returns the raw content of a file, or nullopt if unreadable. */
        using FileReader = std::function<std::optional<std::string>(const std::string&)>;

        struct Result {
            /* Entry program with every import transitively replaced by the
             * imported definitions, in topological (dependencies first)
             * order. */
            std::unique_ptr<ProgramNode> program;

            /* Every file in the import graph, entry included, in the same
             * topological order. */
            std::vector<std::string> files;

            /* SHA-256 of each file's content, parallel to `files`. */
            std::vector<std::string> hashes;
        };

        /* Resolves the import graph rooted at `entryPath`. Import paths are
         * interpreted relative to `rootDir`. */
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
