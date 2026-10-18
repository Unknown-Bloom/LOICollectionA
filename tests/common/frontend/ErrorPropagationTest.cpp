#include <gtest/gtest.h>

#include <string>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

namespace {
    const std::string kHead =
        "func head(a) -> optional<int> {\n"
        "    if (a.length() == 0) [ return None; ]\n"
        "    return a[0];\n"
        "}\n";
}

TEST(ErrorPropagationTest, UnwrapsAPresentValue) {
    EXPECT_EQ(eval(kHead +
        "func first(a) -> optional<int> { return head(a)? + 1; }\n"
        "first([5]) ?? -1"), "6");
}

TEST(ErrorPropagationTest, ReturnsNoneEarlyFromTheEnclosingFunction) {
    EXPECT_EQ(eval(kHead +
        "func first(a) -> optional<int> { return head(a)? + 1; }\n"
        "first([]) ?? -1"), "-1");
}

TEST(ErrorPropagationTest, PropagatesThroughSeveralFrames) {
    EXPECT_EQ(eval(kHead +
        "func first(a) -> optional<int> { return head(a)? + 1; }\n"
        "func second(a) -> optional<int> { return first(a)? * 2; }\n"
        "second([]) ?? -1"), "-1");

    EXPECT_EQ(eval(kHead +
        "func first(a) -> optional<int> { return head(a)? + 1; }\n"
        "func second(a) -> optional<int> { return first(a)? * 2; }\n"
        "second([3]) ?? -1"), "8");
}

TEST(ErrorPropagationTest, StopsAtTheFirstEmptyValue) {
    EXPECT_EQ(eval(kHead +
        "func first(a) -> optional<int> { return head(a)? + 1; }\n"
        "func both(a, b) -> optional<int> { return first(a)? + first(b)?; }\n"
        "both([], [9]) ?? -1"), "-1");
}

TEST(ErrorPropagationTest, RejectsANonOptionalOperand) {
    EXPECT_THROW(
        eval("func f(a) -> optional<int> { return a?; }\nf(1)"),
        std::runtime_error);
}

TEST(ErrorPropagationTest, RejectsAConcreteReturnType) {
    EXPECT_THROW(
        eval(kHead + "func f(a) -> int { return head(a)?; }\nf([1])"),
        std::runtime_error);
}

TEST(ErrorPropagationTest, RejectsUseOutsideAFunction) {
    EXPECT_THROW(eval("let a = 1;\na?"), std::runtime_error);
}
