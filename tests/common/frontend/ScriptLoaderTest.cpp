#include <fstream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "GuiTestEnv.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/ScriptLoader.h"
#include "LOICollectionA/frontend/SemanticAnalyzer.h"
#include "LOICollectionA/frontend/ir/Compiler.h"
#include "LOICollectionA/frontend/ir/Optimizer.h"
#include "LOICollectionA/frontend/ir/VM.h"
#include "LOICollectionA/utils/core/Sha256.h"

using namespace LOICollection::frontend;
using LOICollection::utils::Sha256;

/* §6.2 — multi-file imports: dependency resolution, definition merging and
 * the diagnostics for every failure mode. §6.3 — the shipped GUI scripts
 * must compile through the same loader with the declarative-block syntax. */

namespace {
    using FileMap = std::map<std::string, std::string>;

    ScriptLoader::FileReader readerFor(const FileMap& files) {
        return [&files](const std::string& path) -> std::optional<std::string> {
            auto it = files.find(path);
            return it == files.end() ? std::nullopt : std::optional<std::string>(it->second);
        };
    }

    std::optional<std::string> readRealFile(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return std::nullopt;

        return std::string(std::istreambuf_iterator<char>(file), {});
    }

    /* Full pipeline (load → analyze → compile → run) over a virtual FS. */
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
        auto bytecode = std::make_shared<ir::BytecodeChunk>(compiler.compile(*loaded->program));
        if (diagnostics.hasErrors())
            throw std::runtime_error(diagnostics.getErrorMessage());

        ir::Optimizer optimizer;
        optimizer.optimize(*bytecode);

        ir::VM vm(diagnostics);
        auto result = vm.run(bytecode, {});
        if (diagnostics.hasErrors())
            throw std::runtime_error(diagnostics.getErrorMessage());

        return ir::VM::valueToString(result);
    }
}

TEST(ScriptLoaderTest, ResolvesAndMergesNestedImports) {
    FileMap files = {
        {"/main.lcui", "import \"lib.lcui\";\nresult = helper();"},
        {"/lib.lcui", "import \"sub.lcui\";\n\nfunc helper() -> int {\n    return base() + 1;\n}"},
        {"/sub.lcui", "func base() -> int {\n    return 41;\n}"},
    };

    EXPECT_EQ(evalFiles(files, "/main.lcui"), "42");
}

TEST(ScriptLoaderTest, DiamondImportsMergeDefinitionsOnce) {
    FileMap files = {
        {"/main.lcui", "import \"a.lcui\";\nimport \"b.lcui\";\nresult = aVal() + bVal();"},
        {"/a.lcui", "import \"common.lcui\";\n\nfunc aVal() -> int {\n    return base() + 1;\n}"},
        {"/b.lcui", "import \"common.lcui\";\n\nfunc bVal() -> int {\n    return base() + 2;\n}"},
        {"/common.lcui", "func base() -> int {\n    return 40;\n}"},
    };

    EXPECT_EQ(evalFiles(files, "/main.lcui"), "83");
}

TEST(ScriptLoaderTest, HashesMatchFileContentsInDependencyOrder) {
    FileMap files = {
        {"/main.lcui", "import \"lib.lcui\";\nresult = helper();"},
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
        {"/main.lcui", "import \"lib.lcui\";\nresult = 1;"},
        {"/lib.lcui", "sideEffect = 1;\n\nfunc helper() -> int {\n    return 42;\n}"},
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

/* §6.3 — the migrated built-in scripts compile cleanly through the loader
 * (import resolution + declarative blocks) with the native GUI environment
 * registered. Compilation is the observable contract here: running them
 * needs the live GUIManager, which the standalone build does not carry. */

class ShippedGuiScriptTest : public ::testing::Test {
protected:
    void SetUp() override {
        LOICollection::frontend::test::registerGuiTestEnvironment();
    }

    void compileShippedScript(const std::string& name, size_t expectedFiles) {
        const std::string dir = LOICOLLECTION_TEST_ASSETS_DIR;
        const std::string entry = dir + "/" + name;

        DiagnosticEngine diagnostics;

        auto loaded = ScriptLoader::load(entry, dir, readRealFile, diagnostics);
        ASSERT_TRUE(loaded.has_value()) << diagnostics.getErrorMessage();
        EXPECT_EQ(loaded->files.size(), expectedFiles);

        SemanticAnalyzer analyzer(diagnostics);
        analyzer.analyze(*loaded->program);
        EXPECT_FALSE(diagnostics.hasErrors()) << diagnostics.getErrorMessage();
        EXPECT_FALSE(diagnostics.hasWarnings()) << diagnostics.getWarningMessage();

        ir::Compiler compiler(diagnostics);
        [[maybe_unused]] auto bytecode = compiler.compile(*loaded->program);
        EXPECT_FALSE(diagnostics.hasErrors()) << diagnostics.getErrorMessage();
    }
};

TEST_F(ShippedGuiScriptTest, LanguageScriptCompilesWithImport) {
    compileShippedScript("language.lcui", 2);
}

TEST_F(ShippedGuiScriptTest, BlacklistScriptCompilesWithImport) {
    compileShippedScript("blacklist.lcui", 2);
}

TEST_F(ShippedGuiScriptTest, WalletScriptCompilesWithImport) {
    compileShippedScript("wallet.lcui", 2);
}
