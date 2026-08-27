#include <gtest/gtest.h>

#include <chrono>
#include <string>

#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/ir/VM.h"
#include "LOICollectionA/frontend/sandbox/SandboxBudget.h"

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;
using namespace LOICollection::frontend::ir;
using namespace LOICollection::frontend::sandbox;

namespace {
    struct RunResult {
        SandboxBudget::Violation violation = SandboxBudget::Violation::None;
        bool hasErrors = false;
        std::string errorMessage;
    };

    RunResult runWithBudget(const std::string& input, const SandboxBudget& budget) {
        DiagnosticEngine diagnostics;
        auto chunk = compile(input, diagnostics);

        VM vm(diagnostics);
        vm.setBudget(budget);
        if (chunk)
            (void)vm.run(chunk, {});

        return { vm.report().violation, diagnostics.hasErrors(), diagnostics.getErrorMessage() };
    }
}

TEST(SandboxBudgetTest, InstructionLimit) {
    SandboxBudget budget;
    budget.maxInstructions = 500;

    const RunResult result = runWithBudget("i = 0; while (true) [ i = i + 1; ]; i", budget);

    EXPECT_TRUE(result.hasErrors);
    EXPECT_EQ(result.violation, SandboxBudget::Violation::InstructionLimit);
}

TEST(SandboxBudgetTest, WallTimeLimit) {
    SandboxBudget budget;
    budget.maxInstructions = 10'000'000;
    budget.maxWallTime = std::chrono::milliseconds(1);

    const RunResult result = runWithBudget("i = 0; while (true) [ i = i + 1; ]; i", budget);

    EXPECT_TRUE(result.hasErrors);
    EXPECT_EQ(result.violation, SandboxBudget::Violation::WallTimeLimit);
}

TEST(SandboxBudgetTest, NativeCallLimit) {
    SandboxBudget budget;
    budget.maxNativeCalls = 3;

    const RunResult result = runWithBudget(
        "i = 0; s = 0; while (i < 10) [ s = s + math::abs(0 - i); i = i + 1; ]; s",
        budget
    );

    EXPECT_TRUE(result.hasErrors);
    EXPECT_EQ(result.violation, SandboxBudget::Violation::NativeCallLimit);
}

TEST(SandboxBudgetTest, ObjectCountLimit) {
    SandboxBudget budget;
    budget.maxObjectCount = 2;

    const RunResult result = runWithBudget(
        "class A {} a = new A(); b = new A(); c = new A();",
        budget
    );

    EXPECT_TRUE(result.hasErrors);
    EXPECT_EQ(result.violation, SandboxBudget::Violation::ObjectCountLimit);
}

TEST(SandboxBudgetTest, ArrayElementLimit) {
    SandboxBudget budget;
    budget.maxArrayElements = 3;

    // `n` is a runtime value (a native call result), so the literal cannot be
    // constant-folded and the runtime MAKE_ARRAY path is exercised.
    const RunResult result = runWithBudget("n = string::length(\"abc\"); [n, n, n, n]", budget);

    EXPECT_TRUE(result.hasErrors);
    EXPECT_EQ(result.violation, SandboxBudget::Violation::ArrayElementLimit);
}
