#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(StdlibArrayTest, PushReturnsNewLength) {
    EXPECT_EQ(eval("arr = [1, 2]; arr.push(3)"), "3");
    EXPECT_EQ(eval("arr = [1, 2]; arr.push(3); arr.length()"), "3");
    EXPECT_EQ(eval("arr = []; arr.push(\"x\"); arr"), "[x]");
}

TEST(StdlibArrayTest, ValueMethodOnDynamicTarget) {
    // Method call on a dynamically typed target (member read): the dispatch is
    // deferred to the VM, which selects "Array"/"String" by the runtime value.
    EXPECT_EQ(eval("class Holder { public: value = []; } h = new Holder(); h.value.push(1); h.value.push(2); h.value"), "[1, 2]");
    EXPECT_EQ(eval("class Holder { public: value = []; } h = new Holder(); v = h.value; v.push(7); v.length()"), "1");
    EXPECT_EQ(eval("class Holder { public: value = \"a,b\"; } h = new Holder(); h.value.split(\",\")[1]"), "b");
}

TEST(StdlibArrayTest, PopRemovesLast) {
    EXPECT_EQ(eval("arr = [1, 2, 3]; arr.pop()"), "3");
    EXPECT_EQ(eval("arr = [1, 2, 3]; arr.pop(); arr"), "[1, 2]");
    EXPECT_THROW(eval("arr = []; arr.pop()"), std::runtime_error);
}

TEST(StdlibArrayTest, Contains) {
    EXPECT_EQ(eval("arr = [1, 2, 3]; arr.contains(2)"), "true");
    EXPECT_EQ(eval("arr = [1, 2, 3]; arr.contains(9)"), "false");
    EXPECT_EQ(eval("arr = [\"a\", \"b\"]; arr.contains(\"a\")"), "true");
}

TEST(StdlibArrayTest, IndexOf) {
    EXPECT_EQ(eval("arr = [1, 2, 3]; arr.indexOf(3)"), "2");
    EXPECT_EQ(eval("arr = [1, 2, 3]; arr.indexOf(9)"), "-1");
}

TEST(StdlibArrayTest, Join) {
    EXPECT_EQ(eval("arr = [1, 2, 3]; arr.join(\"-\")"), "1-2-3");
    EXPECT_EQ(eval("arr = [\"a\", \"b\"]; arr.join(\", \")"), "a, b");
    EXPECT_EQ(eval("arr = []; arr.join(\"-\")"), "");
}

TEST(StdlibArrayTest, Slice) {
    EXPECT_EQ(eval("arr = [1, 2, 3, 4]; arr.slice(1, 3)"), "[2, 3]");
    EXPECT_EQ(eval("arr = [1, 2, 3, 4]; arr.slice(0, 2)"), "[1, 2]");
    EXPECT_EQ(eval("arr = [1, 2, 3, 4]; arr.slice(3, 1)"), "[]");
    EXPECT_EQ(eval("arr = [1, 2, 3, 4]; arr.slice(0, 99)"), "[1, 2, 3, 4]");
}

TEST(StdlibArrayTest, Sort) {
    EXPECT_EQ(eval("arr = [3, 1, 2]; arr.sort(); arr"), "[1, 2, 3]");
    EXPECT_EQ(eval("arr = [\"c\", \"a\", \"b\"]; arr.sort(); arr"), "[a, b, c]");
    EXPECT_EQ(eval("arr = [2.5, 1.5]; arr.sort(); arr"), "[1.5, 2.5]");
    EXPECT_THROW(eval("arr = [1, \"a\"]; arr.sort();"), std::runtime_error);
}

TEST(StdlibArrayTest, SortWithComparator) {
    EXPECT_EQ(eval(
        "arr = [3, 1, 2]; "
        "arr.sort(func (a, b) -> int { return b - a; }); "
        "arr"),
        "[3, 2, 1]");
}

TEST(StdlibStringTest, Split) {
    EXPECT_EQ(eval("\"a,b,c\".split(\",\")"), "[a, b, c]");
    EXPECT_EQ(eval("\"abc\".split(\"\")"), "[a, b, c]");
    EXPECT_EQ(eval("\"a--b\".split(\"--\")"), "[a, b]");
}

TEST(StdlibStringTest, ContainsStartsEnds) {
    EXPECT_EQ(eval("\"hello\".contains(\"ell\")"), "true");
    EXPECT_EQ(eval("\"hello\".contains(\"xyz\")"), "false");
    EXPECT_EQ(eval("\"hello\".startsWith(\"he\")"), "true");
    EXPECT_EQ(eval("\"hello\".startsWith(\"lo\")"), "false");
    EXPECT_EQ(eval("\"hello\".endsWith(\"lo\")"), "true");
    EXPECT_EQ(eval("\"hello\".endsWith(\"hel\")"), "false");
}

TEST(StdlibStringTest, IndexOf) {
    EXPECT_EQ(eval("\"hello\".indexOf(\"l\")"), "2");
    EXPECT_EQ(eval("\"hello\".indexOf(\"z\")"), "-1");
}

TEST(StdlibStringTest, ToInt) {
    EXPECT_EQ(eval("\"42\".toInt()"), "42");
    EXPECT_EQ(eval("\"-7\".toInt()"), "-7");
    EXPECT_EQ(eval("\"4x\".toInt()"), "None");
    EXPECT_EQ(eval("\"\".toInt()"), "None");
}

TEST(StdlibStringTest, ToFloat) {
    EXPECT_EQ(eval("\"3.5\".toFloat()"), "3.5");
    EXPECT_EQ(eval("\"3.5x\".toFloat()"), "None");
}

TEST(StdlibMapTest, SetGetHasRemove) {
    EXPECT_EQ(eval("m = new Map(); m.set(\"apple\", 3); m.get(\"apple\")"), "3");
    EXPECT_EQ(eval("m = new Map(); m.get(\"missing\")"), "None");
    EXPECT_EQ(eval("m = new Map(); m.set(\"a\", 1); m.has(\"a\")"), "true");
    EXPECT_EQ(eval("m = new Map(); m.has(\"a\")"), "false");
    EXPECT_EQ(eval("m = new Map(); m.set(\"a\", 1); m.remove(\"a\"); m.has(\"a\")"), "false");
    EXPECT_EQ(eval("m = new Map(); m.remove(\"a\")"), "false");
}

TEST(StdlibMapTest, OverwriteKeepsInsertOrder) {
    EXPECT_EQ(eval("m = new Map(); m.set(\"a\", 1); m.set(\"b\", 2); m.set(\"a\", 9); m.get(\"a\")"), "9");
    EXPECT_EQ(eval("m = new Map(); m.set(\"a\", 1); m.set(\"b\", 2); m.set(\"a\", 9); m.keys"), "[a, b]");
    EXPECT_EQ(eval("m = new Map(); m.set(\"a\", 1); m.set(\"b\", 2); m.set(\"a\", 9); m.length()"), "2");
}

TEST(StdlibMapTest, KeysAndLengthTrackState) {
    EXPECT_EQ(eval("m = new Map(); m.keys"), "[]");
    EXPECT_EQ(eval("m = new Map(); m.length()"), "0");
    EXPECT_EQ(eval("m = new Map(); m.set(\"x\", 1); m.set(\"y\", 2); m.keys"), "[x, y]");
}

TEST(StdlibMapTest, NonStringKeys) {
    EXPECT_EQ(eval("m = new Map(); m.set(1, \"one\"); m.get(1)"), "one");
    EXPECT_EQ(eval("m = new Map(); m.set(1.5, \"f\"); m.has(1.5)"), "true");
    EXPECT_EQ(eval("m = new Map(); m.set(true, \"t\"); m.get(true)"), "t");
}

TEST(StdlibMapTest, ForInOverKeys) {
    EXPECT_EQ(eval(
        "m = new Map(); "
        "m.set(\"a\", 1); m.set(\"b\", 2); m.set(\"c\", 3); "
        "total = \"\"; "
        "for (k in m.keys) [ total += k; ]; "
        "total"),
        "abc");
}

TEST(StdlibMapTest, ChainedUsage) {
    EXPECT_EQ(eval(
        "m = new Map(); "
        "m.set(\"count\", 2); "
        "m.set(\"count\", m.get(\"count\") + 3); "
        "m.get(\"count\")"),
        "5");
}

#ifndef LOICOLLECTION_TEST_NO_OBSERVABLE

TEST(StdlibObservableTest, NumberOperators) {
    EXPECT_EQ(eval("n = new ObservableNumber(10, false); n + 5"), "15");
    EXPECT_EQ(eval("n = new ObservableNumber(10, false); n - 4"), "6");
    EXPECT_EQ(eval("n = new ObservableNumber(10, false); n * 2"), "20");
    EXPECT_EQ(eval("n = new ObservableNumber(10, false); n / 4"), "2.5");
    EXPECT_EQ(eval("n = new ObservableNumber(10, false); n > 5"), "true");
    EXPECT_EQ(eval("n = new ObservableNumber(10, false); n == 10"), "true");
    EXPECT_EQ(eval("n = new ObservableNumber(10, false); n != 10"), "false");
    EXPECT_EQ(eval("a = new ObservableNumber(2, false); b = new ObservableNumber(3, false); a * b"), "6");
    EXPECT_EQ(eval("n = new ObservableNumber(10, false); n.setData(3); n * 2"), "6");
    EXPECT_THROW(eval("n = new ObservableNumber(1, false); n + \"x\""), std::runtime_error);
}

TEST(StdlibObservableTest, StringOperators) {
    EXPECT_EQ(eval("s = new ObservableString(\"ab\", false); s + \"cd\""), "abcd");
    EXPECT_EQ(eval("s = new ObservableString(\"ab\", false); s + 12"), "ab12");
    EXPECT_EQ(eval("s = new ObservableString(\"ab\", false); s == \"ab\""), "true");
    EXPECT_EQ(eval("s = new ObservableString(\"ab\", false); s == \"cd\""), "false");
}

TEST(StdlibObservableTest, BooleanOperators) {
    EXPECT_EQ(eval("b = new ObservableBoolean(true, false); b == true"), "true");
    EXPECT_EQ(eval("b = new ObservableBoolean(true, false); b != true"), "false");
}

#endif
