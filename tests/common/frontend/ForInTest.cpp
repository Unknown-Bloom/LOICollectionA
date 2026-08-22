#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(ForInTest, IteratesArray) {
    EXPECT_EQ(eval("total = 0; for (x in [1, 2, 3]) [ total += x; ]; total"), "6");
    EXPECT_EQ(eval("total = \"\"; for (x in [\"a\", \"b\"]) [ total += x; ]; total"), "ab");
}

TEST(ForInTest, EmptyArray) {
    EXPECT_EQ(eval("total = 0; for (x in []) [ total += 1; ]; total"), "0");
}

TEST(ForInTest, WithIndex) {
    EXPECT_EQ(eval("sum = 0; for (i, x in [10, 20, 30]) [ sum += i; ]; sum"), "3");
    EXPECT_EQ(eval("pairs = \"\"; for (i, x in [\"a\", \"b\"]) [ pairs += i; ]; pairs"), "01");
}

TEST(ForInTest, IndexPairsWithValue) {
    EXPECT_EQ(eval("out = \"\"; for (i, x in [\"a\", \"b\", \"c\"]) [ out += x; ]; out"), "abc");
}

TEST(ForInTest, Range) {
    EXPECT_EQ(eval("total = 0; for (i in 0..5) [ total += i; ]; total"), "10");
    EXPECT_EQ(eval("total = 0; for (i in 2..2) [ total += i; ]; total"), "0");
}

TEST(ForInTest, DescendingRange) {
    EXPECT_EQ(eval("total = 0; for (i in 3..0) [ total += i; ]; total"), "6");
    EXPECT_EQ(eval("out = \"\"; for (i in 2..0) [ out += i; ]; out"), "21");
}

TEST(ForInTest, RangeWithExpressions) {
    EXPECT_EQ(eval("n = 3; total = 0; for (i in 0..n) [ total += i; ]; total"), "3");
}

TEST(ForInTest, ContinueSkipsBodyButNotIteration) {
    EXPECT_EQ(eval(
        "total = 0; "
        "for (x in [1, 2, 3, 4]) [ "
        "    if (x == 2) [ continue; ]; "
        "    total += x; "
        "]; total"),
        "8");
}

TEST(ForInTest, BreakExitsLoop) {
    EXPECT_EQ(eval(
        "total = 0; "
        "for (x in [1, 2, 3, 4]) [ "
        "    if (x == 3) [ break; ]; "
        "    total += x; "
        "]; total"),
        "3");
}

TEST(ForInTest, SeesAppendedElements) {
    EXPECT_EQ(eval(
        "arr = [1, 2, 3]; "
        "count = 0; "
        "for (x in arr) [ count += 1; if (count < 5) [ arr.push(count); ] ]; "
        "count"),
        "7");
}

TEST(ForInTest, Nested) {
    EXPECT_EQ(eval(
        "pairs = \"\"; "
        "for (i in 0..2) [ for (j in 0..2) [ pairs += i; pairs += j; ] ]; "
        "pairs"),
        "00011011");
}

TEST(ForInTest, NonArrayIterable) {
    EXPECT_THROW(eval("for (x in 5) [ x; ]"), std::runtime_error);
    EXPECT_THROW(eval("for (x in \"abc\") [ x; ]"), std::runtime_error);
}

TEST(ForInTest, LambdaCapturesCurrentIterationValue) {
    // A lambda created inside the loop must capture the value of its own
    // iteration, not a reference that later follows the loop variable.
    EXPECT_EQ(eval(
        "fns = []; "
        "for (item in [10, 20, 30]) [ "
        "    fns.push(func () -> int { return item; }); "
        "]; "
        "s = \"\"; for (f in fns) [ s += f(); ]; s"),
        "102030");
    EXPECT_EQ(eval(
        "fns = []; "
        "for (i in 1..3) [ fns.push(func () -> int { return i * 10; }); ]; "
        "s = \"\"; for (f in fns) [ s += f(); ]; s"),
        "1020");
    EXPECT_EQ(eval(
        "fns = []; "
        "for (i, item in [\"a\", \"b\"]) [ "
        "    fns.push(func () -> int { return i; }); "
        "]; "
        "last = fns[1]; last()"),
        "1");
}
