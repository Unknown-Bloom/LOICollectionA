#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/ScriptLoader.h"
#include "LOICollectionA/frontend/SemanticAnalyzer.h"
#include "LOICollectionA/frontend/ir/Compiler.h"
#include "LOICollectionA/frontend/ir/MirLowering.h"
#include "LOICollectionA/frontend/ir/Optimizer.h"
#include "LOICollectionA/frontend/ir/VM.h"
#include "LOICollectionA/utils/core/Sha256.h"

using namespace LOICollection::frontend;
using LOICollection::utils::Sha256;

namespace {
    using FileMap = std::map<std::string, std::string>;

    ScriptLoader::FileReader readerFor(const FileMap& files) {
        return [&files](const std::string& path) -> std::optional<std::string> {
            auto it = files.find(path);
            return it == files.end() ? std::nullopt : std::optional<std::string>(it->second);
        };
    }

    std::string evalFiles(const FileMap& files, const std::string& entry) {
        DiagnosticEngine diagnostics;

        auto loaded = ScriptLoader::load(entry, "/", readerFor(files), diagnostics);
        if (!loaded)
            throw std::runtime_error(diagnostics.getErrorMessage());

        SemanticAnalyzer analyzer(diagnostics);
        analyzer.analyze(*loaded->program);
        if (diagnostics.hasErrors())
            throw std::runtime_error(diagnostics.getErrorMessage());

        ir::Compiler compiler(diagnostics);
        auto mir = std::make_shared<ir::MirChunk>(compiler.compile(*loaded->program));
        if (diagnostics.hasErrors())
            throw std::runtime_error(diagnostics.getErrorMessage());

        ir::Optimizer optimizer;
        optimizer.optimize(*mir);

        auto bytecode = std::make_shared<ir::BytecodeChunk>(ir::MirLowering::lower(*mir));

        ir::VM vm(diagnostics);
        auto result = vm.run(bytecode, {});
        if (diagnostics.hasErrors())
            throw std::runtime_error(diagnostics.getErrorMessage());

        return ir::VM::valueToString(result);
    }
}

TEST(ScriptLoaderTest, ResolvesAndMergesNestedImports) {
    FileMap files = {
        {"/main.lcui", "import \"lib.lcui\";\nlet result = helper();"},
        {"/lib.lcui", "import \"sub.lcui\";\n\nfunc helper() -> int {\n    return base() + 1;\n}"},
        {"/sub.lcui", "func base() -> int {\n    return 41;\n}"},
    };

    EXPECT_EQ(evalFiles(files, "/main.lcui"), "42");
}

TEST(ScriptLoaderTest, DiamondImportsMergeDefinitionsOnce) {
    FileMap files = {
        {"/main.lcui", "import \"a.lcui\";\nimport \"b.lcui\";\nlet result = aVal() + bVal();"},
        {"/a.lcui", "import \"common.lcui\";\n\nfunc aVal() -> int {\n    return base() + 1;\n}"},
        {"/b.lcui", "import \"common.lcui\";\n\nfunc bVal() -> int {\n    return base() + 2;\n}"},
        {"/common.lcui", "func base() -> int {\n    return 40;\n}"},
    };

    EXPECT_EQ(evalFiles(files, "/main.lcui"), "83");
}

TEST(ScriptLoaderTest, HashesMatchFileContentsInDependencyOrder) {
    FileMap files = {
        {"/main.lcui", "import \"lib.lcui\";\nlet result = helper();"},
        {"/lib.lcui", "func helper() -> int {\n    return 42;\n}"},
    };

    DiagnosticEngine diagnostics;
    auto loaded = ScriptLoader::load("/main.lcui", "/", readerFor(files), diagnostics);

    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->files, (std::vector<std::string>{"/lib.lcui", "/main.lcui"}));
    ASSERT_EQ(loaded->hashes.size(), 2u);
    EXPECT_EQ(loaded->hashes[0], Sha256::compute(files.at("/lib.lcui")));
    EXPECT_EQ(loaded->hashes[1], Sha256::compute(files.at("/main.lcui")));
}

TEST(ScriptLoaderTest, DetectsCircularImports) {
    FileMap files = {
        {"/main.lcui", "import \"a.lcui\";"},
        {"/a.lcui", "import \"b.lcui\";\n\nfunc aVal() -> int {\n    return 1;\n}"},
        {"/b.lcui", "import \"a.lcui\";\n\nfunc bVal() -> int {\n    return 2;\n}"},
    };

    DiagnosticEngine diagnostics;
    auto loaded = ScriptLoader::load("/main.lcui", "/", readerFor(files), diagnostics);

    EXPECT_FALSE(loaded.has_value());
    EXPECT_NE(diagnostics.getErrorMessage().find("Circular import"), std::string::npos);
}

TEST(ScriptLoaderTest, RejectsExecutableStatementsInImportedFile) {
    FileMap files = {
        {"/main.lcui", "import \"lib.lcui\";\nlet result = 1;"},
        {"/lib.lcui", "let sideEffect = 1;\n\nfunc helper() -> int {\n    return 42;\n}"},
    };

    DiagnosticEngine diagnostics;
    auto loaded = ScriptLoader::load("/main.lcui", "/", readerFor(files), diagnostics);

    EXPECT_FALSE(loaded.has_value());
    EXPECT_NE(diagnostics.getErrorMessage().find("top-level definitions"), std::string::npos);
}

TEST(ScriptLoaderTest, RejectsConflictingDefinitions) {
    FileMap files = {
        {"/main.lcui", "import \"a.lcui\";\nimport \"b.lcui\";"},
        {"/a.lcui", "func dup() -> int {\n    return 1;\n}"},
        {"/b.lcui", "func dup() -> int {\n    return 2;\n}"},
    };

    DiagnosticEngine diagnostics;
    auto loaded = ScriptLoader::load("/main.lcui", "/", readerFor(files), diagnostics);

    EXPECT_FALSE(loaded.has_value());
    EXPECT_NE(diagnostics.getErrorMessage().find("conflicts with"), std::string::npos);
}

TEST(ScriptLoaderTest, MissingImportFileReportsError) {
    FileMap files = {
        {"/main.lcui", "import \"gone.lcui\";"},
    };

    DiagnosticEngine diagnostics;
    auto loaded = ScriptLoader::load("/main.lcui", "/", readerFor(files), diagnostics);

    EXPECT_FALSE(loaded.has_value());
    EXPECT_NE(diagnostics.getErrorMessage().find("Cannot read imported file"), std::string::npos);
}
