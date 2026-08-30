#include <gtest/gtest.h>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(InheritanceTest, BasicFieldAndMethodInheritance) {
    EXPECT_EQ(eval(
        "class Animal { "
        "public: "
        "name = \"animal\"; "
        "func speak() -> string { return name; } "
        "} "
        "class Dog extends Animal {} "
        "let d = new Dog(); "
        "d.name + \":\" + d.speak()"),
        "animal:animal");
}

TEST(InheritanceTest, ChildDeclaredBeforeBase) {
    EXPECT_EQ(eval(
        "class Dog extends Animal {} "
        "class Animal { public: x = 3; } "
        "let d = new Dog(); "
        "d.x"),
        "3");
}

TEST(InheritanceTest, ConstructorChainWithSuper) {
    EXPECT_EQ(eval(
        "class Base { "
        "public: "
        "x = 0; "
        "Base(v: int) { this.x = v; } "
        "} "
        "class Child extends Base { "
        "Child(v: int) { super(v); } "
        "} "
        "let c = new Child(42); "
        "c.x"),
        "42");
}

TEST(InheritanceTest, ImplicitSuperConstructor) {
    EXPECT_EQ(eval(
        "class Base { "
        "public: "
        "x = 1; "
        "Base() { this.x = 2; } "
        "} "
        "class Child extends Base { Child() {} } "
        "class Grand extends Base {} "
        "let c = new Child(); "
        "let g = new Grand(); "
        "c.x + g.x"),
        "4");
}

TEST(InheritanceTest, ChildConstructorWithoutBaseConstructor) {
    EXPECT_EQ(eval(
        "class A { public: x = 1; } "
        "class B extends A { B() {} } "
        "let b = new B(); "
        "b.x"),
        "1");
}

TEST(InheritanceTest, MethodOverride) {
    EXPECT_EQ(eval(
        "class A { func f() -> int { return 1; } } "
        "class B extends A { func f() -> int { return 2; } } "
        "let b = new B(); "
        "b.f()"),
        "2");
}

TEST(InheritanceTest, PolymorphismThroughBaseType) {
    EXPECT_EQ(eval(
        "class A { func f() -> int { return 1; } } "
        "class B extends A { func f() -> int { return 2; } } "
        "func call(a: A) -> int { return a.f(); } "
        "call(new B())"),
        "2");
}

TEST(InheritanceTest, SuperMethodCall) {
    EXPECT_EQ(eval(
        "class A { func f() -> int { return 1; } } "
        "class B extends A { func f() -> int { return super.f() + 1; } } "
        "let b = new B(); "
        "b.f()"),
        "2");
}

TEST(InheritanceTest, GrandparentSuperChain) {
    EXPECT_EQ(eval(
        "class A { func f() -> int { return 1; } } "
        "class B extends A { func f() -> int { return super.f() + 1; } } "
        "class C extends B { func f() -> int { return super.f() + 1; } } "
        "let c = new C(); "
        "c.f()"),
        "3");
}

TEST(InheritanceTest, InstanceOf) {
    EXPECT_EQ(eval(
        "class A {} "
        "class B extends A {} "
        "let b = new B(); "
        "(b instanceof A) && (b instanceof B) && !(5 instanceof A)"),
        "true");
}

TEST(InheritanceTest, SubclassPassedToBaseParameter) {
    EXPECT_EQ(eval(
        "class A { public: x = 7; } "
        "class B extends A {} "
        "func getX(a: A) -> int { return a.x; } "
        "getX(new B())"),
        "7");
}

TEST(InheritanceTest, PrivateMemberNotInherited) {
    EXPECT_THROW(eval(
        "class A { "
        "private: "
        "secret = 1; "
        "} "
        "class B extends A { "
        "public: "
        "func steal() -> int { return this.secret; } "
        "} "
        "let b = new B(); "
        "b.steal()"),
        std::runtime_error);
}

TEST(InheritanceTest, PrivateBareNameNotInherited) {
    EXPECT_THROW(eval(
        "class A { "
        "private: "
        "secret = 1; "
        "} "
        "class B extends A { "
        "public: "
        "func steal() -> int { return secret; } "
        "} "
        "let b = new B(); "
        "b.steal()"),
        std::runtime_error);
}

TEST(InheritanceTest, MissingSuperCall) {
    EXPECT_THROW(eval(
        "class A { "
        "public: "
        "x = 0; "
        "A(v: int) { this.x = v; } "
        "} "
        "class B extends A { B() {} } "
        "let b = new B()"),
        std::runtime_error);
}

TEST(InheritanceTest, UnknownBaseClass) {
    EXPECT_THROW(eval("class B extends Missing {} new B()"), std::runtime_error);
}

TEST(InheritanceTest, CircularInheritance) {
    EXPECT_THROW(eval("class A extends B {} class B extends A {} new A()"), std::runtime_error);
}

TEST(InheritanceTest, FieldDefaultOverride) {
    EXPECT_EQ(eval(
        "class A { public: x = 1; func get() -> int { return x; } } "
        "class B extends A { public: x = 2; } "
        "let b = new B(); "
        "b.get()"),
        "2");
}
