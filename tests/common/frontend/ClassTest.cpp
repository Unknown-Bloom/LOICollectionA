#include <gtest/gtest.h>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(ClassTest, BasicClassWithConstructor) {
    EXPECT_EQ(eval(
        "class Point { "
        "public: "
        "x = 0; "
        "y = 0; "
        "Point(x, y) { this.x = x; this.y = y; } "
        "func sum() -> int { return x + y; } "
        "} "
        "let p = new Point(3, 4); "
        "p.sum()"),
        "7");
    EXPECT_EQ(eval(
        "class Point { "
        "public: "
        "x = 0; "
        "y = 0; "
        "Point(x, y) { this.x = x; this.y = y; } "
        "} "
        "let p = new Point(3, 4); "
        "p.x + p.y"),
        "7");
}

TEST(ClassTest, FieldDefaults) {
    EXPECT_EQ(eval("class A { public: x = 5; } let a = new A(); a.x"), "5");
    EXPECT_EQ(eval("class A { public: x = \"hi\"; } let a = new A(); a.x"), "hi");
}

TEST(ClassTest, DefaultConstructor) {
    EXPECT_EQ(eval("class A { public: x = 1; A() { } } let a = new A(); a.x"), "1");
}

TEST(ClassTest, MethodWithParameters) {
    EXPECT_EQ(eval(
        "class Calc { "
        "public: "
        "total = 0; "
        "func add(v: int) -> int { total = total + v; return total; } "
        "} "
        "let c = new Calc(); "
        "c.add(1); "
        "c.add(2); "
        "c.add(3)"),
        "6");
}

TEST(ClassTest, ThisKeyword) {
    EXPECT_EQ(eval(
        "class A { "
        "public: "
        "x = 1; "
        "func setX(v: int) -> int { this.x = v; return this.x; } "
        "} "
        "let a = new A(); "
        "a.setX(9)"),
        "9");
}

TEST(ClassTest, PrivateFieldAccess) {
    EXPECT_EQ(eval(
        "class A { "
        "private: "
        "secret = 42; "
        "public: "
        "func getSecret() -> int { return secret; } "
        "} "
        "let a = new A(); "
        "a.getSecret()"),
        "42");
    EXPECT_THROW(eval(
        "class A { "
        "private: "
        "secret = 42; "
        "} "
        "let a = new A(); "
        "a.secret"),
        std::runtime_error);
}

TEST(ClassTest, PrivateMethodAccess) {
    EXPECT_EQ(eval(
        "class A { "
        "private: "
        "func helper() -> int { return 1; } "
        "public: "
        "func call() -> int { return this.helper(); } "
        "} "
        "let a = new A(); "
        "a.call()"),
        "1");
    EXPECT_THROW(eval(
        "class A { "
        "private: "
        "func helper() -> int { return 1; } "
        "public: "
        "func call() -> int { return this.helper(); } "
        "} "
        "let a = new A(); "
        "a.helper()"),
        std::runtime_error);
}

TEST(ClassTest, MethodOverloads) {
    EXPECT_EQ(eval(
        "class A { "
        "public: "
        "func f(x: int) -> int { return x; } "
        "func f(x: string) -> string { return x; } "
        "} "
        "let a = new A(); "
        "a.f(1)"),
        "1");
    EXPECT_EQ(eval(
        "class A { "
        "public: "
        "func f(x: int) -> int { return x; } "
        "func f(x: string) -> string { return x; } "
        "} "
        "let a = new A(); "
        "a.f(\"s\")"),
        "s");
}

TEST(ClassTest, ObjectEquality) {
    EXPECT_EQ(eval("class A {} let a = new A(); a == a"), "true");
    EXPECT_EQ(eval("class A {} let a = new A(); let b = new A(); a == b"), "false");
}

TEST(ClassTest, UnknownClass) {
    EXPECT_THROW(eval("new Missing()"), std::runtime_error);
}

TEST(ClassTest, MissingField) {
    EXPECT_THROW(eval("class A { public: x = 1; } let a = new A(); a.y"), std::runtime_error);
}

TEST(ClassTest, MemberAccessOnNonObject) {
    EXPECT_THROW(eval("let a = 5; a.x"), std::runtime_error);
}

TEST(ClassTest, UnknownMethod) {
    EXPECT_THROW(eval("class A { public: x = 1; } let a = new A(); a.nope()"), std::runtime_error);
}

TEST(ClassTest, DuplicateClass) {
    EXPECT_THROW(eval("class A {} class A {} let a = new A()"), std::runtime_error);
}

TEST(ClassTest, NativeGlobalValue) {
    EXPECT_EQ(eval("let a = new GlobalValue(); a.value = 1; a.value"), "1");
    EXPECT_EQ(eval("let a = new GlobalValue(); a.value = 'test'; a.value"), "test");
}
