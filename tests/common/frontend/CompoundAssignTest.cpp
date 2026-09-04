#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(CompoundAssignTest, BasicArithmetic) {
    EXPECT_EQ(eval("let i = 0; i += 5; i"), "5");
    EXPECT_EQ(eval("let i = 10; i -= 3; i"), "7");
    EXPECT_EQ(eval("let i = 3; i *= 4; i"), "12");
    EXPECT_EQ(eval("let i = 10; i /= 4; i"), "2.5");
    EXPECT_EQ(eval("let i = 10; i %= 3; i"), "1");
}

TEST(CompoundAssignTest, StringConcat) {
    EXPECT_EQ(eval("let s = \"a\"; s += \"b\"; s"), "ab");
}

TEST(CompoundAssignTest, PostfixIncrementDecrement) {
    EXPECT_EQ(eval("let i = 0; i++; i"), "1");
    EXPECT_EQ(eval("let i = 5; i--; i"), "4");
}

TEST(CompoundAssignTest, PrefixIncrementDecrement) {
    EXPECT_EQ(eval("let i = 0; ++i; i"), "1");
    EXPECT_EQ(eval("let i = 5; --i; i"), "4");
}

TEST(CompoundAssignTest, IndexTarget) {
    EXPECT_EQ(eval("let arr = [1, 2, 3]; arr[1] += 10; arr[1]"), "12");
    EXPECT_EQ(eval("let arr = [1, 2, 3]; arr[0]++; arr[0]"), "2");
    EXPECT_EQ(eval("let arr = [1, 2, 3]; let i = 1; arr[i] *= 5; arr[1]"), "10");
}

TEST(CompoundAssignTest, IndexTargetPreservesOtherElements) {
    EXPECT_EQ(eval("let arr = [1, 2, 3]; arr[1] += 10; arr"), "[1, 12, 3]");
}

TEST(CompoundAssignTest, IndexTargetEvaluatedExactlyOnce) {
    
    EXPECT_EQ(eval(
        "let calls = 0; "
        "let arr = [10, 20]; "
        "func n() -> int { calls += 1; return 1; } "
        "arr[n()] += 5; "
        "calls"),
        "1");
    EXPECT_EQ(eval(
        "let calls = 0; "
        "let arr = [10, 20]; "
        "func n() -> int { calls += 1; return 1; } "
        "arr[n()] += 5; "
        "arr[1]"),
        "25");
    EXPECT_EQ(eval(
        "let calls = 0; "
        "let arr = [10, 20]; "
        "func n() -> int { calls += 1; return 0; } "
        "arr[n()]++; "
        "calls"),
        "1");
}

TEST(CompoundAssignTest, FieldTarget) {
    EXPECT_EQ(eval(
        "class Counter { public: count = 0; } "
        "let c = new Counter(); c.count += 7; c.count"),
        "7");
    EXPECT_EQ(eval(
        "class Counter { public: count = 10; } "
        "let c = new Counter(); c.count -= 4; c.count"),
        "6");
}

TEST(CompoundAssignTest, StaticFieldTarget) {
    EXPECT_EQ(eval(
        "class Counter { public: static total = 0; } "
        "Counter.total += 3; Counter.total"),
        "3");
}

TEST(CompoundAssignTest, InLoop) {
    EXPECT_EQ(eval("let total = 0; let i = 0; while (i < 5) [ total += i; i++; ]; total"), "10");
}

TEST(CompoundAssignTest, InvalidTarget) {
    EXPECT_THROW(eval("1 += 2;"), std::runtime_error);
    EXPECT_THROW(eval("\"a\" += \"b\";"), std::runtime_error);
}
