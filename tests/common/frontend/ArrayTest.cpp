#include <gtest/gtest.h>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(ArrayTest, LiteralToString) {
    EXPECT_EQ(eval("[1, 2, 3]"), "[1, 2, 3]");
    EXPECT_EQ(eval("[]"), "[]");
    EXPECT_EQ(eval("[1, \"a\", true]"), "[1, a, true]");
}

TEST(ArrayTest, IndexRead) {
    EXPECT_EQ(eval("[10, 20, 30][1]"), "20");
    EXPECT_EQ(eval("a = [1, 2, 3]; a[0] + a[2]"), "4");
}

TEST(ArrayTest, IndexWrite) {
    EXPECT_EQ(eval("a = [1, 2]; a[0] = 9; a[0]"), "9");
}

TEST(ArrayTest, AppendAtIndexLength) {
    EXPECT_EQ(eval("a = [1]; a[1] = 2; a.length()"), "2");
    EXPECT_EQ(eval("a = []; a[0] = 5; a[0]"), "5");
}

TEST(ArrayTest, Length) {
    EXPECT_EQ(eval("[1, 2, 3].length()"), "3");
    EXPECT_EQ(eval("a = []; a.length()"), "0");
}

TEST(ArrayTest, NestedArrays) {
    EXPECT_EQ(eval("a = [[1, 2], [3, 4]]; a[1][0]"), "3");
}

TEST(ArrayTest, MixedTypes) {
    EXPECT_EQ(eval("a = [1, \"x\", true]; a[1]"), "x");
}

TEST(ArrayTest, IdentityEquality) {
    EXPECT_EQ(eval("a = [1]; a == a"), "true");
    EXPECT_EQ(eval("a = [1]; b = [1]; a == b"), "false");
    EXPECT_EQ(eval("a = [1]; b = a; a == b"), "true");
}

TEST(ArrayTest, Truthiness) {
    EXPECT_EQ(eval("if([])[1:2]"), "2");
    EXPECT_EQ(eval("if([1])[1:2]"), "1");
}

TEST(ArrayTest, IndexOutOfRange) {
    EXPECT_THROW(eval("[1][5]"), std::runtime_error);
    EXPECT_THROW(eval("a = [1]; a[-1]"), std::runtime_error);
}

TEST(ArrayTest, IndexNonArray) {
    EXPECT_THROW(eval("a = 5; a[0]"), std::runtime_error);
}

TEST(ArrayTest, FieldDefaultIsolation) {
    EXPECT_EQ(eval(
        "class A { public: arr = [1, 2]; } "
        "a = new A(); "
        "b = new A(); "
        "a.arr[0] = 9; "
        "b.arr[0]"),
        "1");
}

TEST(ArrayTest, StaticArrayField) {
    EXPECT_EQ(eval(
        "class A { public: static arr = [1, 2]; } "
        "A.arr[1]"),
        "2");
}
