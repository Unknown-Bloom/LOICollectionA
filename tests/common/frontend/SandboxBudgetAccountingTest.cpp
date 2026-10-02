#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <limits>

#include "LOICollectionA/frontend/sandbox/SandboxBudget.h"

using namespace LOICollection::frontend::sandbox;

namespace sandbox_budget_accounting_test_support {
    using Violation = SandboxBudget::Violation;

    SandboxBudget withExpiredWallClock(std::size_t maxInstructions) {
        SandboxBudget budget;
        budget.reset();
        budget.maxInstructions = maxInstructions;
        budget.maxWallTime = std::chrono::milliseconds(1);
        budget.startTime = std::chrono::steady_clock::now() - std::chrono::seconds(1);

        return budget;
    }
}

TEST(SandboxBudgetAccountingTest, ResetClearsCounters) {
    SandboxBudget budget;
    budget.accountNativeCall();
    budget.accountObject();
    budget.accountArray(1);
    budget.accountString(1);

    budget.reset();

    EXPECT_EQ(budget.executedInstructions, 0u);
    EXPECT_EQ(budget.nativeCallCount, 0u);
    EXPECT_EQ(budget.objectCount, 0u);
    EXPECT_EQ(budget.allocatedBytes, 0u);
    EXPECT_EQ(budget.activeFrames, 0u);
}

TEST(SandboxBudgetAccountingTest, InstructionLimitTriggersAfterTheBudgetIsSpent) {
    SandboxBudget budget;
    budget.reset();
    budget.maxInstructions = 1;

    EXPECT_EQ(budget.tickInstruction(), SandboxBudget::Violation::None);
    EXPECT_EQ(budget.executedInstructions, 1u);

    EXPECT_EQ(budget.tickInstruction(), SandboxBudget::Violation::InstructionLimit);
    EXPECT_EQ(budget.executedInstructions, 2u);
}

TEST(SandboxBudgetAccountingTest, WallClockIsCheckedEveryThousandAndTwentyFourTicks) {
    SandboxBudget budget = sandbox_budget_accounting_test_support::withExpiredWallClock(1'000'000);

    for (std::size_t tick = 1; tick < 1024; ++tick)
        EXPECT_EQ(budget.tickInstruction(), SandboxBudget::Violation::None) << "tick " << tick;

    EXPECT_EQ(budget.tickInstruction(), SandboxBudget::Violation::WallTimeLimit);
}

TEST(SandboxBudgetAccountingTest, WallClockIsIgnoredBeforeTheFirstThousandTicks) {
    SandboxBudget budget = sandbox_budget_accounting_test_support::withExpiredWallClock(1);

    EXPECT_EQ(budget.tickInstruction(), SandboxBudget::Violation::None);
    EXPECT_EQ(budget.tickInstruction(), SandboxBudget::Violation::InstructionLimit);
}

TEST(SandboxBudgetAccountingTest, NativeCallLimitIsStrictlyGreaterThan) {
    SandboxBudget budget;
    budget.reset();
    budget.maxNativeCalls = 2;

    EXPECT_EQ(budget.accountNativeCall(), SandboxBudget::Violation::None);
    EXPECT_EQ(budget.accountNativeCall(), SandboxBudget::Violation::None);
    EXPECT_EQ(budget.accountNativeCall(), SandboxBudget::Violation::NativeCallLimit);
    EXPECT_EQ(budget.nativeCallCount, 3u);
}

TEST(SandboxBudgetAccountingTest, ObjectCountLimitIsStrictlyGreaterThan) {
    SandboxBudget budget;
    budget.reset();
    budget.maxObjectCount = 1;

    EXPECT_EQ(budget.accountObject(), SandboxBudget::Violation::None);
    EXPECT_EQ(budget.accountObject(), SandboxBudget::Violation::ObjectCountLimit);
    EXPECT_EQ(budget.objectCount, 2u);
}

TEST(SandboxBudgetAccountingTest, ArrayAccountingChargesSixteenBytesPerElement) {
    SandboxBudget budget;
    budget.reset();
    budget.maxArrayElements = 100;
    budget.maxTotalBytes = 1'000'000;

    EXPECT_EQ(budget.accountArray(0), SandboxBudget::Violation::None);
    EXPECT_EQ(budget.allocatedBytes, 0u);

    EXPECT_EQ(budget.accountArray(10), SandboxBudget::Violation::None);
    EXPECT_EQ(budget.allocatedBytes, 160u);
}

TEST(SandboxBudgetAccountingTest, OversizedArrayIsRejectedWithoutChargingBytes) {
    SandboxBudget budget;
    budget.reset();
    budget.maxArrayElements = 4;
    budget.maxTotalBytes = 1'000'000;

    EXPECT_EQ(budget.accountArray(5), SandboxBudget::Violation::ArrayElementLimit);
    EXPECT_EQ(budget.allocatedBytes, 0u);

    EXPECT_EQ(budget.accountArray(4), SandboxBudget::Violation::None);
    EXPECT_EQ(budget.allocatedBytes, 64u);
}

TEST(SandboxBudgetAccountingTest, ArrayAccountingReportsTotalBytesWhenTheSharedBudgetOverflows) {
    SandboxBudget budget;
    budget.reset();
    budget.maxArrayElements = 1000;
    budget.maxTotalBytes = 100;

    EXPECT_EQ(budget.accountArray(6), SandboxBudget::Violation::None);
    EXPECT_EQ(budget.allocatedBytes, 96u);

    EXPECT_EQ(budget.accountArray(1), SandboxBudget::Violation::TotalByteLimit);
    EXPECT_EQ(budget.allocatedBytes, 112u);
}

TEST(SandboxBudgetAccountingTest, StringAccountingChargesOneBytePerByte) {
    SandboxBudget budget;
    budget.reset();
    budget.maxStringBytes = 100;
    budget.maxTotalBytes = 1'000'000;

    EXPECT_EQ(budget.accountString(0), SandboxBudget::Violation::None);
    EXPECT_EQ(budget.accountString(40), SandboxBudget::Violation::None);
    EXPECT_EQ(budget.allocatedBytes, 40u);
}

TEST(SandboxBudgetAccountingTest, OversizedStringIsRejectedWithoutChargingBytes) {
    SandboxBudget budget;
    budget.reset();
    budget.maxStringBytes = 8;
    budget.maxTotalBytes = 1'000'000;

    EXPECT_EQ(budget.accountString(9), SandboxBudget::Violation::StringByteLimit);
    EXPECT_EQ(budget.allocatedBytes, 0u);

    EXPECT_EQ(budget.accountString(8), SandboxBudget::Violation::None);
    EXPECT_EQ(budget.allocatedBytes, 8u);
}

TEST(SandboxBudgetAccountingTest, ArraysAndStringsShareOneByteBudget) {
    SandboxBudget budget;
    budget.reset();
    budget.maxArrayElements = 1000;
    budget.maxStringBytes = 1000;
    budget.maxTotalBytes = 200;

    EXPECT_EQ(budget.accountArray(10), SandboxBudget::Violation::None);
    EXPECT_EQ(budget.allocatedBytes, 160u);

    EXPECT_EQ(budget.accountString(41), SandboxBudget::Violation::TotalByteLimit);
    EXPECT_EQ(budget.allocatedBytes, 201u);
}

TEST(SandboxBudgetAccountingTest, ZeroTotalByteBudgetRejectsAnyAllocation) {
    SandboxBudget budget;
    budget.reset();
    budget.maxArrayElements = std::numeric_limits<std::size_t>::max();
    budget.maxStringBytes = std::numeric_limits<std::size_t>::max();
    budget.maxTotalBytes = 0;

    EXPECT_EQ(budget.accountArray(1), SandboxBudget::Violation::TotalByteLimit);
    EXPECT_EQ(budget.accountString(1), SandboxBudget::Violation::TotalByteLimit);
}
