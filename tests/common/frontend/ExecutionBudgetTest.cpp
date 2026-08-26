#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "CommonTest.h"

using namespace LOICollection::frontend;

/* §7.2 — execution budget and call depth: a runaway script must abort with a
 * diagnostic instead of hanging the host or exhausting the native stack,
 * while legitimate bounded work stays unaffected. */

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
            x = 1;
        }
    )");

    EXPECT_NE(message.find("Execution budget exhausted"), std::string::npos) << message;
}

TEST(ExecutionBudgetTest, UnboundedRecursionHitsDepthLimit) {
    const std::string message = failureOf(R"(
        func boom(n) -> int {
            return boom(n);
        }

        result = boom(1);
    )");

    EXPECT_NE(message.find("Call stack depth limit exceeded"), std::string::npos) << message;
}

TEST(ExecutionBudgetTest, BoundedLoopCompletesWithinBudget) {
    /* 10k iterations, well under the 1M-instruction budget. */
    EXPECT_EQ(eval(R"(
        total = 0;
        i = 0;
        while (i < 10000) {
            total = total + 1;
            i = i + 1;
        }
        total;
    )"), "10000");
}

TEST(ExecutionBudgetTest, ReasonableRecursionDepthAllowed) {
    /* Depth 100 must stay far below the 256-frame ceiling. */
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
