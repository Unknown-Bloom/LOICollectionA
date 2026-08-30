#include <gtest/gtest.h>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(StaticTest, StaticFieldDefault) {
    EXPECT_EQ(eval("class A { public: static count = 1; } A.count"), "1");
}

TEST(StaticTest, StaticFieldAssignment) {
    EXPECT_EQ(eval("class A { public: static count = 1; } A.count = 5; A.count"), "5");
}

TEST(StaticTest, StaticMethod) {
    EXPECT_EQ(eval("class A { public: static func make() -> int { return 42; } } A.make()"), "42");
}

TEST(StaticTest, StaticMethodAccessStaticField) {
    EXPECT_EQ(eval(
        "class A { "
        "public: "
        "static count = 1; "
        "static func inc() -> int { count = count + 1; return count; } "
        "} "
        "A.inc(); "
        "A.inc()"),
        "3");
}

TEST(StaticTest, StaticMethodCallsStaticMethod) {
    EXPECT_EQ(eval(
        "class A { "
        "public: "
        "static func helper() -> int { return 1; } "
        "static func call() -> int { return helper(); } "
        "} "
        "A.call()"),
        "1");
}

TEST(StaticTest, StaticFieldSharedAcrossInstances) {
    EXPECT_EQ(eval(
        "class A { "
        "public: "
        "static count = 10; "
        "func get() -> int { return A.count; } "
        "} "
        "a = new A(); "
        "A.count = 20; "
        "a.get()"),
        "20");
}

TEST(StaticTest, StaticInheritance) {
    EXPECT_EQ(eval(
        "class A { "
        "public: "
        "static count = 7; "
        "static func get() -> int { return count; } "
        "} "
        "class B extends A {} "
        "B.count + B.get()"),
        "14");
}

TEST(StaticTest, StaticFieldOverride) {
    EXPECT_EQ(eval(
        "class A { public: static count = 1; } "
        "class B extends A { public: static count = 2; } "
        "A.count + B.count"),
        "3");
}

TEST(StaticTest, StaticMethodOverride) {
    EXPECT_EQ(eval(
        "class A { public: static func f() -> int { return 1; } } "
        "class B extends A { public: static func f() -> int { return 2; } } "
        "A.f() + B.f()"),
        "3");
}

TEST(StaticTest, PrivateStaticMember) {
    EXPECT_EQ(eval(
        "class A { "
        "private: "
        "static secret = 42; "
        "public: "
        "static func get() -> int { return secret; } "
        "} "
        "A.get()"),
        "42");
    EXPECT_THROW(eval(
        "class A { private: static secret = 42; } "
        "A.secret"),
        std::runtime_error);
}

TEST(StaticTest, ThisNotAllowedInStaticMethod) {
    EXPECT_THROW(eval(
        "class A { "
        "public: "
        "x = 1; "
        "static func bad() -> int { return this.x; } "
        "} "
        "A.bad()"),
        std::runtime_error);
}

TEST(StaticTest, InstanceCannotCallStaticMethod) {
    EXPECT_THROW(eval(
        "class A { "
        "public: "
        "static func f() -> int { return 1; } "
        "} "
        "a = new A(); "
        "a.f()"),
        std::runtime_error);
}

TEST(StaticTest, StaticMethodInInstanceMethodViaClassName) {
    EXPECT_EQ(eval(
        "class A { "
        "public: "
        "static func f() -> int { return 9; } "
        "func g() -> int { return A.f(); } "
        "} "
        "a = new A(); "
        "a.g()"),
        "9");
}

TEST(StaticTest, StaticFieldInInstanceMethodBareName) {
    EXPECT_EQ(eval(
        "class A { "
        "public: "
        "static count = 5; "
        "func get() -> int { return count; } "
        "} "
        "a = new A(); "
        "a.get()"),
        "5");
}

TEST(StaticTest, NativeStaticFieldAndMethod) {
    auto& cc = ClassCall::getInstance();
    const std::string name = "NativeStaticDemo";

    cc.registerClass(name, {});
    cc.registerField(name, "value", 7);
    cc.registerStaticField(name, "version", 1);
    cc.registerStaticMethod(name, "create",
        [](const CallbackTypeValues& args) -> TypedValue {
            auto obj = std::make_shared<Object>();
            obj->className = "NativeStaticDemo";
            obj->assign("value", std::get<int>(args[0]));
            return obj;
        }, { ParamType::INT });
    cc.registerStaticMethod(name, "versioned",
        [](const CallbackTypeValues&) -> TypedValue {
            return 42;
        }, {});

    EXPECT_EQ(eval("NativeStaticDemo.version"), "1");
    EXPECT_EQ(eval("NativeStaticDemo.version = 9; NativeStaticDemo.version"), "9");
    EXPECT_EQ(eval("NativeStaticDemo.versioned()"), "42");
    EXPECT_EQ(eval("o = NativeStaticDemo.create(5); o.value"), "5");
    EXPECT_EQ(eval("new NativeStaticDemo().value"), "7");
}
