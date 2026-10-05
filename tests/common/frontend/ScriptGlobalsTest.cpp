#include <gtest/gtest.h>

#include <set>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

namespace {
    struct ScriptContext {
        std::shared_ptr<GlobalsTable> globals = std::make_shared<GlobalsTable>();

        void load(const std::shared_ptr<ir::MirChunk>& chunk) {
            if (this->loaded.contains(chunk.get()))
                return;

            DiagnosticEngine diagnostics;

            ir::VM builder(diagnostics, this->globals);
            [[maybe_unused]] auto result = builder.run(chunk, Context{});

            EXPECT_FALSE(diagnostics.hasErrors()) << diagnostics.getErrorMessage();

            this->loaded.insert(chunk.get());
        }

        FunctionRefPtr detach(const std::shared_ptr<ir::MirChunk>& chunk, const std::string& name) {
            this->load(chunk);

            auto it = this->globals->find(name);
            if (it == this->globals->end() || !std::holds_alternative<FunctionRefPtr>(it->second))
                return nullptr;

            return std::get<FunctionRefPtr>(it->second);
        }

        std::set<const ir::MirChunk*> loaded;
    };
}

TEST(ScriptGlobalsTest, CallbackOutlivesItsBuildingVm) {
    DiagnosticEngine diagnostics;
    auto chunk = compile("let cb = func () -> int { return 42; };", diagnostics);
    ASSERT_NE(chunk, nullptr) << diagnostics.getErrorMessage();

    ScriptContext script;
    auto callback = script.detach(chunk, "cb");
    ASSERT_NE(callback, nullptr);

    DiagnosticEngine callDiagnostics;
    auto value = ir::VM::callFunctionRef(callback, {}, {}, callDiagnostics);

    EXPECT_FALSE(callDiagnostics.hasErrors()) << callDiagnostics.getErrorMessage();
    EXPECT_EQ(ir::VM::valueToString(value), "42");
}

TEST(ScriptGlobalsTest, CallbackResolvesTopLevelVariableAfterVmVanish) {
    DiagnosticEngine diagnostics;
    auto chunk = compile(
        "let navigateOnline = new GlobalValue(); "
        "navigateOnline.value = false; "
        "let cb = func () -> void { navigateOnline.value = true; }; "
        "let probe = func () -> bool { return navigateOnline.value; };",
        diagnostics);
    ASSERT_NE(chunk, nullptr) << diagnostics.getErrorMessage();

    ScriptContext script;
    script.load(chunk);

    auto callback = script.detach(chunk, "cb");
    auto probe = script.detach(chunk, "probe");
    ASSERT_NE(callback, nullptr);
    ASSERT_NE(probe, nullptr);

    DiagnosticEngine callDiagnostics;
    [[maybe_unused]] auto fired = ir::VM::callFunctionRef(callback, {}, {}, callDiagnostics);
    EXPECT_FALSE(callDiagnostics.hasErrors()) << callDiagnostics.getErrorMessage();

    DiagnosticEngine probeDiagnostics;
    auto value = ir::VM::callFunctionRef(probe, {}, {}, probeDiagnostics);
    EXPECT_FALSE(probeDiagnostics.hasErrors()) << probeDiagnostics.getErrorMessage();
    EXPECT_EQ(ir::VM::valueToString(value), "true");
}

TEST(ScriptGlobalsTest, CallbacksShareWritesThroughTheirScriptGlobals) {
    DiagnosticEngine diagnostics;
    auto chunk = compile(
        "let seen = new GlobalValue(); "
        "let write = func () -> void { seen.value = true; }; "
        "let read = func () -> bool { return seen.value; };",
        diagnostics);
    ASSERT_NE(chunk, nullptr) << diagnostics.getErrorMessage();

    ScriptContext script;
    script.load(chunk);

    auto write = script.detach(chunk, "write");
    auto read = script.detach(chunk, "read");
    ASSERT_NE(write, nullptr);
    ASSERT_NE(read, nullptr);

    DiagnosticEngine writeDiagnostics;
    [[maybe_unused]] auto fired = ir::VM::callFunctionRef(write, {}, {}, writeDiagnostics);
    EXPECT_FALSE(writeDiagnostics.hasErrors()) << writeDiagnostics.getErrorMessage();

    DiagnosticEngine readDiagnostics;
    auto value = ir::VM::callFunctionRef(read, {}, {}, readDiagnostics);
    EXPECT_FALSE(readDiagnostics.hasErrors()) << readDiagnostics.getErrorMessage();
    EXPECT_EQ(ir::VM::valueToString(value), "true");
}

TEST(ScriptGlobalsTest, DetachedCallbackReportsExpiredContext) {
    DiagnosticEngine diagnostics;
    auto chunk = compile("let cb = func () -> int { return 1; };", diagnostics);
    ASSERT_NE(chunk, nullptr) << diagnostics.getErrorMessage();

    FunctionRefPtr callback;
    {
        ir::VM builder(diagnostics, std::make_shared<GlobalsTable>());
        [[maybe_unused]] auto result = builder.run(chunk, Context{});

        callback = std::make_shared<FunctionRef>();
        callback->owner = chunk;
        callback->bodyIndex = 0;
    }

    DiagnosticEngine callDiagnostics;
    [[maybe_unused]] auto value = ir::VM::callFunctionRef(callback, {}, {}, callDiagnostics);

    EXPECT_TRUE(callDiagnostics.hasErrors());
    EXPECT_NE(
        callDiagnostics.getErrorMessage().find("Script context expired"),
        std::string::npos);
}
