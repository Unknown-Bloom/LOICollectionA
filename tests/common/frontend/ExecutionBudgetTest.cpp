#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "CommonTest.h"

using namespace LOICollection::frontend;

namespace {
    std::string failureOf(const std::string& source) {
        try {
            eval(source);
        } catch (const std::runtime_error& e) {
            return e.what();
        }

        return "";
    }
}

TEST(ExecutionBudgetTest, InfiniteLoopExhaustsBudget) {
    const std::string message = failureOf(R"(
        while (true) {
            let x = 1;
        }
    )");

    EXPECT_NE(message.find("Execution budget exhausted"), std::string::npos) << message;
}

TEST(ExecutionBudgetTest, UnboundedRecursionHitsDepthLimit) {
    const std::string message = failureOf(R"(
        func boom(n) -> int {
            return boom(n);
        }

        let result = boom(1);
    )");

    EXPECT_NE(message.find("Call stack depth limit exceeded"), std::string::npos) << message;
}

TEST(ExecutionBudgetTest, BoundedLoopCompletesWithinBudget) {
    EXPECT_EQ(eval(R"(
        let total = 0;
        let i = 0;
        while (i < 10000) {
            total = total + 1;
            i = i + 1;
        }
        total;
    )"), "10000");
}

TEST(ExecutionBudgetTest, ReasonableRecursionDepthAllowed) {
    EXPECT_EQ(eval(R"(
        func sum(n) -> int {
            if (n <= 0) [
                return 0;
            ]

            return n + sum(n - 1);
        }

        sum(100);
    )"), "5050");
}
