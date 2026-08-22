#include <gtest/gtest.h>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(TypeSystemTest, VariantBasic) {
    EXPECT_EQ(eval("a: variant<string, bool, int> = 1; a.type"), "int");
    EXPECT_EQ(eval("a: variant<string, bool, int> = 1; a.value"), "1");
    EXPECT_EQ(eval("a: variant<string, bool, int> = 1; a"), "1");
    EXPECT_EQ(eval("a: variant<string, bool, int> = 1; a = \"test\"; a.type"), "string");
    EXPECT_EQ(eval("a: variant<string, bool, int> = 1; a = true; a.type"), "bool");
    EXPECT_EQ(eval("a: variant<string, bool, int> = 1; a = 2; a + 3"), "5");
}

TEST(TypeSystemTest, VariantTypeMismatch) {
    EXPECT_THROW(eval("a: variant<string, bool, int> = 1.1;"), std::runtime_error);
    EXPECT_THROW(eval("a: variant<string, bool, int> = 1; a = 1.1;"), std::runtime_error);
    EXPECT_THROW(eval("a: variant<string, bool, int> = 1; a.has_value"), std::runtime_error);
    EXPECT_THROW(eval("a: variant<string, bool, int> = 1; a.nope"), std::runtime_error);
}

TEST(TypeSystemTest, OptionalBasic) {
    EXPECT_EQ(eval("a: optional<int> = 1; a"), "1");
    EXPECT_EQ(eval("a: optional<int> = 1; a.value"), "1");
    EXPECT_EQ(eval("a: optional<int> = 1; a.has_value"), "true");
    EXPECT_EQ(eval("a: optional<int> = 1; a.type"), "int");
    EXPECT_EQ(eval("a: optional<int> = 1; a = None; a.has_value"), "false");
    EXPECT_EQ(eval("b: optional<string> = None; b.has_value"), "false");
    EXPECT_EQ(eval("b: optional<string> = None; b.type"), "none");
    EXPECT_EQ(eval("b: optional<string> = None; b = \"s\"; b.has_value"), "true");
    EXPECT_EQ(eval("b: optional<string> = None; b = \"s\"; b"), "s");
}

TEST(TypeSystemTest, OptionalEmptyAccessErrors) {
    EXPECT_THROW(eval("b: optional<string> = None; b"), std::runtime_error);
    EXPECT_THROW(eval("b: optional<string> = None; b.value"), std::runtime_error);
    EXPECT_THROW(eval("b: optional<string> = None; b + \"x\""), std::runtime_error);
}

TEST(TypeSystemTest, NoneOnlyInOptionalContext) {
    // Dynamic variables may hold 'None' (needed for '??' / '?.' semantics);
    // typed variables still reject it.
    EXPECT_EQ(eval("c = None; c ?? 1"), "1");
    EXPECT_THROW(eval("c: int = None;"), std::runtime_error);
    EXPECT_THROW(eval("func f() -> int { return None; } f()"), std::runtime_error);
    EXPECT_THROW(eval("None"), std::runtime_error);
}

TEST(TypeSystemTest, OptionalCopyPreservesEmpty) {
    EXPECT_EQ(eval("b: optional<string> = None; y: optional<string> = b; y.has_value"), "false");
    EXPECT_EQ(eval("a: optional<int> = 1; y: optional<int> = a; y"), "1");
    EXPECT_EQ(eval(
        "func none() -> optional<int> { b: optional<int> = None; return b; } "
        "a: optional<int> = none(); a.has_value"), "false");
}

TEST(TypeSystemTest, TypedVariables) {
    EXPECT_EQ(eval("a: int = 1; a + 2"), "3");
    EXPECT_THROW(eval("a: int = 1; a = \"s\""), std::runtime_error);
    EXPECT_THROW(eval("a: int = \"s\""), std::runtime_error);
    EXPECT_THROW(eval("a: int;"), std::runtime_error);
    EXPECT_THROW(eval("a: optional<optional<int>> = 1;"), std::runtime_error);
}

TEST(TypeSystemTest, UsingAlias) {
    EXPECT_EQ(eval("using Value = variant<string, bool, int>; a: Value = 1; a.type"), "int");
    EXPECT_EQ(eval("using Value = variant<string, bool, int>; a: Value = \"s\"; a"), "s");
    EXPECT_EQ(eval("using A = int; using B = A; x: B = 3; x + 1"), "4");
    EXPECT_THROW(eval("using A = A; x: A = 1"), std::runtime_error);
    EXPECT_THROW(eval("using V = variant<int>; x: V = 1"), std::runtime_error);
    EXPECT_THROW(eval("using A = int; using A = string;"), std::runtime_error);
}

TEST(TypeSystemTest, UsingOnlyAtTopLevel) {
    EXPECT_THROW(eval("func f() { using A = int; }"), std::runtime_error);
}

TEST(TypeSystemTest, OptionalParamsAndReturns) {
    EXPECT_EQ(eval(
        "func wrap(x: int) -> optional<int> { return x; } "
        "a: optional<int> = wrap(5); a"), "5");
    EXPECT_EQ(eval(
        "func none() -> optional<int> { return None; } "
        "a: optional<int> = none(); a.has_value"), "false");
    EXPECT_EQ(eval(
        "func get(x: optional<int>) -> bool { return x.has_value; } "
        "get(None)"), "false");
    EXPECT_EQ(eval(
        "func get(x: optional<int>) -> bool { return x.has_value; } "
        "get(1)"), "true");
    EXPECT_EQ(eval(
        "func pass(x: optional<int>) -> bool { return x.has_value; } "
        "b: optional<int> = None; pass(b)"), "false");
}

TEST(TypeSystemTest, OptionalReceiver) {
    EXPECT_EQ(eval(
        "class Box { public: v = 0; } "
        "b: optional<Box> = new Box(); b.v"), "0");
    EXPECT_THROW(eval(
        "class Box { public: v = 0; } "
        "b: optional<Box> = None; b.v"), std::runtime_error);
}

TEST(TypeSystemTest, VariantWithClasses) {
    EXPECT_EQ(eval("class A {} a: variant<A, int> = new A(); a.type"), "A");
}

TEST(TypeSystemTest, TypedMembers) {
    EXPECT_EQ(eval(
        "class A { public: x: variant<int, string> = 1; } "
        "a = new A(); a.x = \"s\"; a.x.type"), "string");
    EXPECT_EQ(eval("class A { public: x: int = 1; } a = new A(); a.x"), "1");
    EXPECT_THROW(eval("class A { public: x: int = 1; } a = new A(); a.x = \"s\""),
        std::runtime_error);
    EXPECT_EQ(eval(
        "class A { public: x: optional<int> = None; } "
        "a = new A(); a.x.has_value"), "false");
}

TEST(TypeSystemTest, StaticTypedMembers) {
    EXPECT_EQ(eval(
        "class A { public: static x: optional<int> = None; } "
        "A.x.has_value"), "false");
    EXPECT_THROW(eval(
        "class A { public: static x: optional<int> = None; } "
        "A.x"), std::runtime_error);
}

TEST(TypeSystemTest, UntypedMembersAreDynamic) {
    EXPECT_EQ(eval("class A { public: x = 1; } a = new A(); a.x = \"test\"; a.x"), "test");
}

TEST(TypeSystemTest, MemberWithoutDefaultRequiresConstructorAssignment) {
    EXPECT_THROW(eval("class A { public: x; }"), std::runtime_error);
    EXPECT_THROW(eval("class A { public: x; A() {} }"), std::runtime_error);
    EXPECT_EQ(eval("class A { public: x; A() { this.x = 5; } } a = new A(); a.x"), "5");
    EXPECT_EQ(eval("class A { public: x; A(v: int) { x = v; } } a = new A(3); a.x"), "3");
}

TEST(TypeSystemTest, InheritedMemberInitializationIsStrict) {
    EXPECT_THROW(eval(
        "class Base { public: x; } "
        "class Child extends Base {} c = new Child();"), std::runtime_error);
    EXPECT_EQ(eval(
        "class Base { public: x; Base() { this.x = 1; } } "
        "class Child extends Base {} c = new Child(); c.x"), "1");
}

TEST(TypeSystemTest, SpecialMembersOnlyOnVariantOptional) {
    EXPECT_THROW(eval("a = 1; a.type"), std::runtime_error);
    EXPECT_THROW(eval("a = 1; a.value"), std::runtime_error);
}
