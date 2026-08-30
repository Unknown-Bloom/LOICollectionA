#include <gtest/gtest.h>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(FunctionTest, SimpleFunction) {
    EXPECT_EQ(eval("func add(a: int, b: int) -> int { return a + b; } add(2, 3)"), "5");
}

TEST(FunctionTest, Recursion) {
    EXPECT_EQ(eval(
        "func fib(n: int) -> int { "
        "if (n <= 1) [ return n : return fib(n - 1) + fib(n - 2) ] "
        "} "
        "fib(10)"),
        "55");
}

TEST(FunctionTest, MutualRecursion) {
    EXPECT_EQ(eval(
        "func isEven(n: int) -> bool { "
        "if (n == 0) [ return true : return isOdd(n - 1) ] "
        "} "
        "func isOdd(n: int) -> bool { "
        "if (n == 0) [ return false : return isEven(n - 1) ] "
        "} "
        "isEven(10)"),
        "true");
    EXPECT_EQ(eval(
        "func isEven(n: int) -> bool { "
        "if (n == 0) [ return true : return isOdd(n - 1) ] "
        "} "
        "func isOdd(n: int) -> bool { "
        "if (n == 0) [ return false : return isEven(n - 1) ] "
        "} "
        "isEven(11)"),
        "false");
}

TEST(FunctionTest, FunctionWithoutExplicitReturnType) {
    EXPECT_EQ(eval("func pick(x) { return x; } pick(\"hello\")"), "hello");
    EXPECT_EQ(eval("func pick(x) { return x; } pick(42)"), "42");
}

TEST(FunctionTest, ClassObjectParameter) {
    EXPECT_EQ(eval(
        "class A { "
        "public: "
        "x = 1; "
        "} "
        "func getX(a: A) -> int { return a.x; } "
        "getX(new A())"),
        "1");
}

TEST(FunctionTest, ImplicitEmptyReturn) {
    EXPECT_EQ(eval("func noret() { let a = 1; } noret()"), "");
}

TEST(FunctionTest, Overloads) {
    EXPECT_EQ(eval(
        "func id(x: int) -> int { return x; } "
        "func id(x: string) -> string { return x; } "
        "id(7)"),
        "7");
    EXPECT_EQ(eval(
        "func id(x: int) -> int { return x; } "
        "func id(x: string) -> string { return x; } "
        "id(\"s\")"),
        "s");
}

TEST(FunctionTest, MultipleStatementsInBody) {
    EXPECT_EQ(eval(
        "func compute(x: int) -> int { "
        "let y = x * 2; "
        "y = y + 1; "
        "return y; "
        "} "
        "compute(5)"),
        "11");
}

TEST(FunctionTest, WrongArgumentCount) {
    EXPECT_THROW(eval("func f(a: int) -> int { return a; } f()"), std::runtime_error);
    EXPECT_THROW(eval("func f(a: int) -> int { return a; } f(1, 2)"), std::runtime_error);
}

TEST(FunctionTest, WrongArgumentType) {
    EXPECT_THROW(eval("func f(a: int) -> int { return a; } f(\"s\")"), std::runtime_error);
}

TEST(FunctionTest, UndefinedFunction) {
    EXPECT_THROW(eval("nope(1)"), std::runtime_error);
}

TEST(FunctionTest, ReturnOutsideFunction) {
    EXPECT_THROW(eval("return 1;"), std::runtime_error);
}

TEST(FunctionTest, ReturnTypeMismatch) {
    EXPECT_THROW(eval("func bad() -> int { return \"x\"; } bad()"), std::runtime_error);
}

TEST(FunctionTest, InfiniteRecursionHitsDepthLimit) {
    EXPECT_THROW(eval("func f() { return f(); } f()"), std::runtime_error);
}
