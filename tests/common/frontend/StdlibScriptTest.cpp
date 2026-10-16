#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
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

using namespace LOICollection::frontend;

/* Runs entry "/main.lcui" against an in-memory file map that already
 * contains the real std modules (assets/common/std/*.lcui) under "/".
 * Std modules import each other by file-relative paths, which resolve
 * to "/iter.lcui" etc. — this exercises the ScriptLoader resolution the
 * shipped assets rely on. */

namespace {
    std::string evalStd(const std::string& program) {
        std::map<std::string, std::string> files = {
            {"/main.lcui", program},
        };

        static const std::string stdDir = [] {
#ifdef LOICOLLECTION_A_STD_DIR
            return std::string(LOICOLLECTION_A_STD_DIR);
#else
            for (const char* cand : {
                "assets/common/std",
                "../assets/common/std",
                "../../assets/common/std",
                "../../../assets/common/std",
                "../../../repo/assets/common/std",
            }) {
                if (std::filesystem::exists(std::filesystem::path(cand) / "iter.lcui"))
                    return std::string(cand);
            }
            throw std::runtime_error("cannot locate assets/common/std (define LOICOLLECTION_A_STD_DIR)");
#endif
        }();

        for (const char* name : { "iter", "array", "string", "math", "result" }) {
            std::ifstream file(std::filesystem::path(stdDir) / (std::string(name) + ".lcui"));
            if (!file)
                throw std::runtime_error("cannot open std module: " + std::string(name));

            std::stringstream ss;
            ss << file.rdbuf();
            files["/" + std::string(name) + ".lcui"] = ss.str();
        }

        ScriptLoader::FileReader reader = [&files](const std::string& path) -> std::optional<std::string> {
            auto it = files.find(path);
            if (it == files.end())
                return std::nullopt;
            return it->second;
        };

        DiagnosticEngine diagnostics;
        auto loaded = ScriptLoader::load("/main.lcui", "/", reader, diagnostics);
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

TEST(StdlibScriptTest, StringHelpers) {
    EXPECT_EQ(evalStd("import \"/string.lcui\"; upper(\"aBc\")"), "ABC");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; lower(\"XyZ\")"), "xyz");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; upper(\"中文abc\")"), "中文ABC");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; substr(\"hello\", 1, 3)"), "ell");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; substr(\"hello\", 10, 2)"), "");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; trim(\"  hi \")"), "hi");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; replace(\"abcabc\", \"a\", \"x\")"), "xbcxbc");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; replace(\"abc\", \"\", \"x\")"), "abc");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; capitalize(\"hello world\")"), "Hello world");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; reverse(\"abc\")"), "cba");
    EXPECT_EQ(evalStd("import \"/string.lcui\"; padLeft(\"7\", 3, \"0\")"), "007");
}

TEST(StdlibScriptTest, MathHelpers) {
    EXPECT_EQ(evalStd("import \"/math.lcui\"; abs(-10)"), "10");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; abs(-3.5)"), "3.5");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; min(5, 3)"), "3");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; max(2.5, 1.5)"), "2.5");
    EXPECT_EQ(evalStd("import \"/math.lcui\"; clamp(9, 0, 5)"), "5");
}

TEST(StdlibScriptTest, IteratorCombinatorsChain) {
    EXPECT_EQ(evalStd(
        "import \"/iter.lcui\";"
        "class Square extends Fn1 { func call(x) { return x * x; } }"
        "class Big extends Fn1 { func call(x) { return x > 4; } }"
        "range(1, 10).map(new Square()).filter(new Big()).collect().join(\",\")"),
        "9,16,25,36,49,64,81");
    EXPECT_EQ(evalStd(
        "import \"/iter.lcui\"; range(1, 10).skip(7).collect().join(\",\")"), "8,9");
    EXPECT_EQ(evalStd(
        "import \"/iter.lcui\"; range(1, 10).take(3).collect().join(\",\")"), "1,2,3");
    EXPECT_EQ(evalStd(
        "import \"/iter.lcui\"; range(1, 5).sum()"), "10");
    EXPECT_EQ(evalStd(
        "import \"/iter.lcui\"; range(1, 9).stepBy(3).collect().join(\",\")"), "1,4,7");
    EXPECT_EQ(evalStd(
        "import \"/iter.lcui\"; range(1, 5).chain(range(10, 12)).collect().join(\",\")"),
        "1,2,3,4,10,11");
}

TEST(StdlibScriptTest, IteratorSearchAndFold) {
    EXPECT_EQ(evalStd(
        "import \"/iter.lcui\"; class Big extends Fn1 { func call(x) { return x > 4; } }"
        "range(1, 10).find(new Big())"), "5");
    EXPECT_EQ(evalStd(
        "import \"/iter.lcui\"; class Big extends Fn1 { func call(x) { return x > 4; } }"
        "range(1, 10).position(new Big())"), "4");
    EXPECT_EQ(evalStd(
        "import \"/iter.lcui\"; range(1, 5).max()"), "4");
    EXPECT_EQ(evalStd(
        "import \"/iter.lcui\"; range(1, 5).min()"), "1");
    EXPECT_EQ(evalStd(
        "import \"/iter.lcui\"; repeat(\"x\").take(3).collect().join(\"\")"), "xxx");
}

TEST(StdlibScriptTest, ArrayHelpersPullFn1ThroughSelectiveImport) {
    EXPECT_EQ(evalStd(
        "import \"/array.lcui\";"
        "class Twice extends Fn1 { func call(x) { return x * 2; } }"
        "arrMap([1, 2, 3], new Twice()).join(\",\")"), "2,4,6");
    EXPECT_EQ(evalStd(
        "import \"/array.lcui\"; arrSum([1, 2, 3])"), "6");
    EXPECT_EQ(evalStd(
        "import \"/array.lcui\"; arrDedup([1, 2, 1, 3, 2]).join(\",\")"), "1,2,3");
    EXPECT_EQ(evalStd(
        "import \"/array.lcui\"; arrLast([]) == None"), "true");
    EXPECT_EQ(evalStd(
        "import \"/array.lcui\"; arrZip([1, 2], [\"a\", \"b\"])[1][1]"), "b");
}

TEST(StdlibScriptTest, ResultType) {
    EXPECT_EQ(evalStd(
        "import \"/result.lcui\"; Result::Ok(42).unwrap()"), "42");
    EXPECT_EQ(evalStd(
        "import \"/result.lcui\"; Result::Err(\"boom\").unwrapOr(-1)"), "-1");
    EXPECT_EQ(evalStd(
        "import \"/result.lcui\"; Result::Err(\"boom\").errOr(\"none\")"), "boom");
    EXPECT_EQ(evalStd(
        "import \"/result.lcui\";"
        "class Inc extends Fn1 { func call(x) { return x + 1; } }"
        "Result::Ok(1).map(new Inc()).unwrap()"), "2");
    EXPECT_ANY_THROW(evalStd(
        "import \"/result.lcui\"; Result::Err(\"boom\").unwrap()"));
}

TEST(StdlibScriptTest, ManualNextKeepsOptionalOnDynamicLet) {
    EXPECT_EQ(evalStd(
        "import \"/iter.lcui\";"
        "let it = range(1, 3);"
        "let a = it.next();"
        "let b = it.next();"
        "let c = it.next();"
        "let s = \"\";"
        "if (a) [ s = s + a; ];"
        "if (b) [ s = s + b; ];"
        "if (!c) [ s = s + \"end\"; ];"
        "s"), "12end");
}
