#include <chrono>
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
#include "LOICollectionA/frontend/runtime/Scheduler.h"

using namespace LOICollection::frontend;
using namespace LOICollection::frontend::runtime;

namespace {

    std::string evalProc(const std::string& program) {
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

            throw std::runtime_error("cannot locate assets/common/std (define LOICOLLECTION_A_STD_DIR)");
        }();

        for (const char* name : { "iter", "array", "string", "math", "result", "map", "json", "proc" }) {
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

    void resetScheduler() {
        ProcScheduler::getInstance().reset();
    }
}

class SchedulerTest : public ::testing::Test {
protected:
    void SetUp() override {
        resetScheduler();
    }
};

TEST_F(SchedulerTest, SpawnJoinReturnsTaskResult) {
    EXPECT_EQ(evalProc(
        "import \"/proc.lcui\";"
        "let id = spawn(func() { return 42; });"
        "join(id)"), "42");
}

TEST_F(SchedulerTest, TwoTasksRunToCompletion) {
    EXPECT_EQ(evalProc(
        "import \"/proc.lcui\";"
        "let a = spawn(func() { return 10; });"
        "let b = spawn(func() { return 20; });"
        "join(a) + join(b)"), "30");
}

TEST_F(SchedulerTest, SendRecvDeliversMessage) {
    EXPECT_EQ(evalProc(
        "import \"/proc.lcui\";"
        "let sink = spawn(func() { return recv(); });"
        "send(sink, \"hello\");"
        "join(sink)"), "hello");
}

TEST_F(SchedulerTest, PanicIsIsolated) {
    EXPECT_EQ(evalProc(
        "import \"/proc.lcui\";"
        "spawn(func() { panic(\"boom\"); });"
        "let id = spawn(func() { return 7; });"
        "join(id)"), "7");
}

TEST_F(SchedulerTest, SleepResolvesWithInjectedClock) {
    auto& sch = ProcScheduler::getInstance();
    long long vclock = 0;
    sch.clockFn = [&]() {
        vclock += 5;
        return std::chrono::steady_clock::time_point(std::chrono::milliseconds(vclock));
    };

    EXPECT_EQ(evalProc(
        "import \"/proc.lcui\";"
        "let id = spawn(func() { sleep(10); return 1; });"
        "join(id)"), "1");

    resetScheduler();
}

TEST_F(SchedulerTest, ManyTasksAllComplete) {
    std::string prog = "import \"/proc.lcui\";"
                        "let i = 0;"
                        "let last = 0;"
                        "while (i < 500) {"
                        "  last = spawn(func() { return 1; });"
                        "  i = i + 1;"
                        "}"
                        "join(last)";
    EXPECT_EQ(evalProc(prog), "1");
    EXPECT_EQ(ProcScheduler::getInstance().taskCount(), static_cast<size_t>(500));
}

TEST_F(SchedulerTest, InterleavedCounterViaMailbox) {
    EXPECT_EQ(evalProc(
        "import \"/proc.lcui\";"
        "let producer = spawn(func() {"
        "  let i = 0;"
        "  while (i < 3) { send(2, i); i = i + 1; }"
        "  return 0;"
        "});"
        "let consumer = spawn(func() {"
        "  let sum = 0;"
        "  let n = 0;"
        "  while (n < 3) { sum = sum + recv(); n = n + 1; }"
        "  return sum;"
        "});"
        "join(producer);"
        "join(consumer)"), "3");
}
