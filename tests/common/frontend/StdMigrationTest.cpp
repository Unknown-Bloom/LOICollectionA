#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "LOICollectionA/LOICollectionA.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/ScriptLoader.h"
#include "LOICollectionA/frontend/SemanticAnalyzer.h"
#include "LOICollectionA/frontend/ir/Compiler.h"
#include "LOICollectionA/frontend/ir/Optimizer.h"
#include "LOICollectionA/frontend/ir/VM.h"

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

namespace {
    std::string evalWithStd(const std::string& program) {
        std::map<std::string, std::string> files = {
            {"/main.lcui", program},
        };

        static const std::string stdDir = [] {
            std::vector<std::filesystem::path> candidates;

            const std::filesystem::path modDir =
                LOICollection::A::getInstance().getSelf().getModDir();
            if (!modDir.empty()) {
                candidates.push_back(modDir / "std");
                candidates.push_back(modDir / "assets" / "common" / "std");
            }
#ifdef LOICOLLECTION_A_STD_DIR
            candidates.push_back(std::filesystem::path(LOICOLLECTION_A_STD_DIR));
#endif
            for (const char* cand : {
                "assets/common/std",
                "../assets/common/std",
                "../../assets/common/std",
                "../../../assets/common/std",
                "../../../repo/assets/common/std",
            }) {
                candidates.push_back(std::filesystem::path(cand));
            }

            for (const auto& cand : candidates) {
                if (std::filesystem::exists(cand / "iter.lcui"))
                    return cand.string();
            }

            throw std::runtime_error("cannot locate assets/common/std");
        }();

        for (const char* name : { "iter", "array", "string", "math", "result", "map", "json", "random", "time", "set", "vec", "num" }) {
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

    std::string withResult(const std::string& body) {
        return "import \"/result.lcui\";\n" + body;
    }
}

TEST(StdMigrationTest, ResultIsNativeEnum) {
    EXPECT_EQ(evalWithStd(withResult("let r = Ok(42)\n r")), "enum#0[0](42)");
    EXPECT_EQ(evalWithStd(withResult("let r = Err(\"boom\")\n r")), "enum#0[1](boom)");
}

TEST(StdMigrationTest, ResultHelpers) {
    EXPECT_EQ(evalWithStd(withResult("resultIsOk(Ok(1))")), "true");
    EXPECT_EQ(evalWithStd(withResult("resultIsOk(Err(\"x\"))")), "false");
    EXPECT_EQ(evalWithStd(withResult("resultIsErr(Err(\"x\"))")), "true");
    EXPECT_EQ(evalWithStd(withResult("resultUnwrap(Ok(7))")), "7");
    EXPECT_EQ(evalWithStd(withResult("resultUnwrapOr(Err(\"x\"), 9)")), "9");
    EXPECT_EQ(evalWithStd(withResult("resultExpect(Ok(3), \"nope\")")), "3");
    EXPECT_EQ(evalWithStd(withResult("resultErrOr(Err(\"boom\"), \"none\")")), "boom");
}

TEST(StdMigrationTest, ResultMapTakesClosure) {
    EXPECT_EQ(evalWithStd(withResult(
        "resultUnwrap(resultMap(Ok(21), func (x) { return x * 2; }))")), "42");
}

TEST(StdMigrationTest, ResultMapErrAndAndThenTakeClosures) {
    EXPECT_EQ(evalWithStd(withResult(
        "resultErrOr(resultMapErr(Err(\"boom\"), func (e) { return e + \"!\"; }), \"\")")), "boom!");
    EXPECT_EQ(evalWithStd(withResult(
        "resultUnwrap(resultAndThen(Ok(20), func (x) { return Ok(x + 2); }))")), "22");
}

TEST(StdMigrationTest, ResultDestructuresWithMatch) {
    EXPECT_EQ(evalWithStd(withResult(
        "let r = Ok(5)\n"
        " match (r) [ Ok(v) [ \"ok:\" + v ] Err(e) [ \"err:\" + e ] ]")), "ok:5");
    EXPECT_EQ(evalWithStd(withResult(
        "let r = Err(\"down\")\n"
        " match (r) [ Ok(v) [ \"ok:\" + v ] Err(e) [ \"err:\" + e ] ]")), "err:down");
}
