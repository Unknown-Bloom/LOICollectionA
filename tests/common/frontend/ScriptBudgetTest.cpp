#include <gtest/gtest.h>

#include <chrono>
#include <string>

#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/ir/VM.h"
#include "LOICollectionA/frontend/sandbox/ScriptBudget.h"
#include "LOICollectionA/frontend/sandbox/ScriptPermission.h"

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;
using namespace LOICollection::frontend::sandbox;

namespace {
    constexpr std::size_t kDefaultInstructions = 1'000'000;

    const char* kBudgetPermission = R"json({
        "defaultPolicy": "deny",
        "budget": { "maxInstructions": 250000, "maxWallTimeMs": 750 },
        "scripts": {
            "wallet.lcui": {
                "enabled": true,
                "budget": { "maxInstructions": 500 }
            },
            "menu.lcui": {
                "enabled": true,
                "budget": { "maxFrames": 64 }
            },
            "greedy.lcui": {
                "enabled": true,
                "budget": { "maxInstructions": 0, "maxWallTimeMs": 999999 }
            }
        }
    })json";

    PermissionGate parseGate(const std::string& json) {
        std::string error;
        auto gate = PermissionGate::fromJson(json, error);
        EXPECT_TRUE(gate.has_value()) << error;
        return gate.value_or(PermissionGate{});
    }

    SandboxBudget::Violation runLoop(const std::string& input, const SandboxBudget& budget) {
        DiagnosticEngine diagnostics;
        auto chunk = compile(input, diagnostics);

        ir::VM vm(diagnostics);
        vm.setBudget(budget);
        if (chunk)
            (void)vm.run(chunk, {});

        return vm.report().violation;
    }
}

TEST(ScriptBudgetTest, DefaultBudgetMatchesVmDefaults) {
    BudgetPolicy policy;

    EXPECT_EQ(policy.resolve("anything").maxInstructions, kDefaultInstructions);
    EXPECT_EQ(policy.resolve("anything").maxWallTime, std::chrono::milliseconds(1000));
}

TEST(ScriptBudgetTest, OverrideLeavesUnspecifiedFieldsAlone) {
    BudgetPolicy policy;
    policy.setDefault({ 42, std::chrono::milliseconds(5) });

    const SandboxBudget resolved = policy.resolve("wallet.lcui");

    EXPECT_EQ(resolved.maxInstructions, 10'000u);
    EXPECT_EQ(resolved.maxWallTime, std::chrono::milliseconds(10));
    EXPECT_EQ(resolved.maxFrames, 256u);
}

TEST(ScriptBudgetTest, PerScriptOverrideWins) {
    BudgetPolicy policy;
    policy.setDefault({ 2'000'000 });
    policy.setOverride("wallet.lcui", { 100'000 });

    EXPECT_EQ(policy.resolve("wallet.lcui").maxInstructions, 100'000u);
    EXPECT_EQ(policy.resolve("menu.lcui").maxInstructions, 2'000'000u);
}

TEST(ScriptBudgetTest, ClampRejectsDisabledOrAbsurdBudgets) {
    BudgetPolicy policy;
    policy.setOverride("greedy.lcui", { 0, std::chrono::milliseconds(0) });
    policy.setOverride("huge.lcui", { 18446744073709551615ull, std::chrono::milliseconds(999'999) });

    const SandboxBudget tiny = policy.resolve("greedy.lcui");
    EXPECT_EQ(tiny.maxInstructions, 10'000u);
    EXPECT_EQ(tiny.maxWallTime, std::chrono::milliseconds(10));

    const SandboxBudget huge = policy.resolve("huge.lcui");
    EXPECT_EQ(huge.maxInstructions, 100'000'000u);
    EXPECT_EQ(huge.maxWallTime, std::chrono::milliseconds(30'000));
}

TEST(ScriptBudgetTest, ParseReadsDefaultAndPerScriptBudgets) {
    const PermissionGate gate = parseGate(kBudgetPermission);

    EXPECT_EQ(gate.budgetFor("wallet.lcui").maxInstructions, 10'000u);
    EXPECT_EQ(gate.budgetFor("wallet.lcui").maxWallTime, std::chrono::milliseconds(750));

    EXPECT_EQ(gate.budgetFor("menu.lcui").maxInstructions, 250'000u);
    EXPECT_EQ(gate.budgetFor("menu.lcui").maxFrames, 64u);

    EXPECT_EQ(gate.budgetFor("unknown.lcui").maxInstructions, 250'000u);
}

TEST(ScriptBudgetTest, ParseClampsOutOfRangeConfiguration) {
    const PermissionGate gate = parseGate(kBudgetPermission);
    const SandboxBudget budget = gate.budgetFor("greedy.lcui");

    EXPECT_EQ(budget.maxInstructions, 10'000u);
    EXPECT_EQ(budget.maxWallTime, std::chrono::milliseconds(30'000));
}

TEST(ScriptBudgetTest, ParseIgnoresNonNumericLimits) {
    const PermissionGate gate = parseGate(R"json({
        "budget": { "maxInstructions": "1000", "maxWallTimeMs": -5 }
    })json");

    EXPECT_EQ(gate.budgetFor("x").maxInstructions, kDefaultInstructions);
    EXPECT_EQ(gate.budgetFor("x").maxWallTime, std::chrono::milliseconds(1000));
}

TEST(ScriptBudgetTest, ConfiguredBudgetStopsRunawayScript) {
    const PermissionGate gate = parseGate(kBudgetPermission);

    EXPECT_EQ(
        runLoop("let i = 0; while (true) [ i = i + 1; ]; i", gate.budgetFor("wallet.lcui")),
        SandboxBudget::Violation::InstructionLimit
    );
}

TEST(ScriptBudgetTest, GenerousBudgetLetsTheSameScriptFinish) {
    const PermissionGate gate = parseGate(kBudgetPermission);

    EXPECT_EQ(
        runLoop("let i = 0; while (i < 100) [ i = i + 1; ]; i", gate.budgetFor("menu.lcui")),
        SandboxBudget::Violation::None
    );
}
