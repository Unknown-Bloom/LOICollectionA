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

        ir::VM vm(diagnostics);
        auto result = vm.run(mir, {});
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

TEST(ScriptLoaderTest, ResolvesAbsoluteImportsWithinRoot) {
    FileMap files = {
        {"/main.lcui", "import \"/lib.lcui\";\nlet result = helper();"},
        {"/lib.lcui", "func helper() -> int {\n    return 7;\n}"},
    };

    EXPECT_EQ(evalFiles(files, "/main.lcui"), "7");
}

TEST(ScriptLoaderTest, RejectsAbsoluteImportsOutsideRoot) {
    FileMap files = {
        {"/root/main.lcui", "import \"/outside.lcui\";"},
        {"/outside.lcui", "func helper() -> int {\n    return 1;\n}"},
    };

    DiagnosticEngine diagnostics;
    auto loaded = ScriptLoader::load("/root/main.lcui", "/root", readerFor(files), diagnostics);

    EXPECT_FALSE(loaded.has_value());
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

TEST(ScriptLoaderTest, AllowsOverloadedFunctions) {
    FileMap files = {
        {"/main.lcui",
         "func id(x: int) -> int {\n    return x;\n}\n"
         "func id(x: string) -> string {\n    return x;\n}"},
    };

    DiagnosticEngine diagnostics;
    auto loaded = ScriptLoader::load("/main.lcui", "/", readerFor(files), diagnostics);

    ASSERT_TRUE(loaded.has_value());
    EXPECT_FALSE(diagnostics.hasErrors());
}

TEST(ScriptLoaderTest, RejectsDuplicateFunctionSignatures) {
    FileMap files = {
        {"/main.lcui", "import \"a.lcui\";\nimport \"b.lcui\";"},
        {"/a.lcui", "func dup(x: int) -> int {\n    return 1;\n}"},
        {"/b.lcui", "func dup(x: int) -> int {\n    return 2;\n}"},
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

TEST(ScriptLoaderTest, SelectiveImportFiltersSymbols) {
    FileMap files = {
        {"/lib.lcui", "func keep() -> int { return 1; }\nfunc drop() -> int { return 2; }\nclass KeepClass { }\n"},
        {"/main.lcui", "import \"lib.lcui\" { keep };\nlet r = keep();\nr"},
    };

    EXPECT_EQ(evalFiles(files, "/main.lcui"), "1");
}

TEST(ScriptLoaderTest, SelectiveImportMultipleSymbols) {
    FileMap files = {
        {"/lib.lcui", "func f() -> int { return 10; }\nfunc g() -> int { return 20; }\nfunc h() -> int { return 30; }\n"},
        {"/main.lcui", "import \"lib.lcui\" { f, h };\nf() + h()"},
    };

    EXPECT_EQ(evalFiles(files, "/main.lcui"), "40");
}

TEST(ScriptLoaderTest, SelectiveImportExcludedSymbolIsUndefined) {
    FileMap files = {
        {"/lib.lcui", "func keep() -> int { return 1; }\nfunc drop() -> int { return 2; }\n"},
        {"/main.lcui", "import \"lib.lcui\" { keep };\ndrop()"},
    };

    EXPECT_THROW(evalFiles(files, "/main.lcui"), std::runtime_error);
}

TEST(ScriptLoaderTest, SelectiveImportUnlistedClassIsUndefined) {
    FileMap files = {
        {"/lib.lcui", "func keep() -> int { return 1; }\nclass Hidden { }\n"},
        {"/main.lcui", "import \"lib.lcui\" { keep };\nnew Hidden()"},
    };

    EXPECT_THROW(evalFiles(files, "/main.lcui"), std::runtime_error);
}

TEST(ScriptLoaderTest, SelectiveImportUnionAcrossSites) {
    FileMap files = {
        {"/common.lcui", "func base() -> int { return 5; }\nfunc extra() -> int { return 6; }\n"},
        {"/a.lcui", "import \"common.lcui\" { base };\nfunc aVal() -> int { return base() + 1; }\n"},
        {"/b.lcui", "import \"common.lcui\" { extra };\nfunc bVal() -> int { return extra() + 2; }\n"},
        {"/main.lcui", "import \"a.lcui\";\nimport \"b.lcui\";\naVal() + bVal()"},
    };

    EXPECT_EQ(evalFiles(files, "/main.lcui"), "14");
}

TEST(ScriptLoaderTest, SelectiveImportWildcardStillMergesEverything) {
    FileMap files = {
        {"/lib.lcui", "func f() -> int { return 3; }\nfunc g() -> int { return 4; }\n"},
        {"/main.lcui", "import \"lib.lcui\";\nf() + g()"},
    };

    EXPECT_EQ(evalFiles(files, "/main.lcui"), "7");
}

TEST(ScriptLoaderTest, SelectiveImportEnumMergesVariants) {
    FileMap files = {
        {"/lib.lcui", "enum Option { Some(int) Nothing }\nfunc drop() -> int { return 0; }\n"},
        {"/main.lcui",
         "import \"lib.lcui\" { Option };\n"
         "let v = Some(7);\n"
         "match (v) [ Some(x) [ x ] Nothing() [ 0 ] ]"},
    };

    EXPECT_EQ(evalFiles(files, "/main.lcui"), "7");
}

TEST(ScriptLoaderTest, SelectiveImportExcludedEnumIsUndefined) {
    FileMap files = {
        {"/lib.lcui", "enum Option { Some(int) }\nfunc keep() -> int { return 1; }\n"},
        {"/main.lcui", "import \"lib.lcui\" { keep };\nSome(1)"},
    };

    EXPECT_THROW(evalFiles(files, "/main.lcui"), std::runtime_error);
}

TEST(ScriptLoaderTest, DuplicateEnumNameReportsConflict) {
    FileMap files = {
        {"/a.lcui", "enum Kind { A }\n"},
        {"/b.lcui", "enum Kind { B }\n"},
        {"/main.lcui", "import \"a.lcui\";\nimport \"b.lcui\";\n1"},
    };

    DiagnosticEngine diagnostics;
    auto loaded = ScriptLoader::load("/main.lcui", "/", readerFor(files), diagnostics);

    EXPECT_FALSE(loaded.has_value());
    EXPECT_NE(diagnostics.getErrorMessage().find("conflicts with"), std::string::npos);
}
