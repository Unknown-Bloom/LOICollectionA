#include <gtest/gtest.h>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(ReceiverBindingTest, ThisRefersToCallReceiver) {
    EXPECT_EQ(eval(
        "class Box { "
        "public: "
        "v = 41; "
        "func run(cb) { return cb(); } "
        "} "
        "let b = new Box(); "
        "b.run(func () -> int { return this.v + 1; })"),
        "42");
}

TEST(ReceiverBindingTest, CapturingReceiverIsRejected) {
    try {
        eval(
            "class Box { "
            "public: "
            "v = 41; "
            "func run(cb) { return cb(); } "
            "} "
            "let b = new Box(); "
            "b.run(func () -> int { return b.v + 1; })");
        FAIL() << "capturing the receiver inside a callback must be rejected";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("captures its receiver"), std::string::npos);
    }
}

TEST(ReceiverBindingTest, CapturingReceiverInNestedClosureIsRejected) {
    try {
        eval(
            "class Box { "
            "public: "
            "v = 41; "
            "func run(cb) { return cb(); } "
            "} "
            "let b = new Box(); "
            "b.run(func () -> int { return b.run(func () -> int { return b.v; }); })");
        FAIL() << "capturing the receiver from a nested closure must be rejected";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("captures its receiver"), std::string::npos);
    }
}

TEST(ReceiverBindingTest, UnrelatedObjectIsStillCapturable) {
    EXPECT_EQ(eval(
        "class Box { "
        "public: "
        "v = 41; "
        "func run(cb) { return cb(); } "
        "} "
        "let b = new Box(); "
        "let other = new Box(); "
        "other.v = 1; "
        "b.run(func () -> int { return other.v + this.v; })"),
        "42");
}

TEST(ReceiverBindingTest, NestedClosureBindsInnermostReceiver) {
    EXPECT_EQ(eval(
        "class Outer { "
        "public: "
        "v = 1; "
        "func run(cb) { return cb(); } "
        "} "
        "class Inner { "
        "public: "
        "v = 2; "
        "func run(cb) { return cb(); } "
        "} "
        "let a = new Outer(); "
        "let b = new Inner(); "
        "a.run(func () -> int { return this.v + b.run(func () -> int { return this.v; }); })"),
        "3");
}

TEST(ReceiverBindingTest, MethodReceiverIsNotRebound) {
    EXPECT_EQ(eval(
        "class Outer { "
        "public: "
        "v = 10; "
        "func run(cb) { return cb(); } "
        "} "
        "class Holder { "
        "public: "
        "v = 32; "
        "func fetch(target: Outer) { return target.run(func () -> int { return this.v; }); } "
        "} "
        "new Holder().fetch(new Outer())"),
        "32");
}

TEST(ReceiverBindingTest, BoundCallbackDoesNotRetainReceiver) {
    DiagnosticEngine diagnostics;
    auto chunk = compile(
        "class Box { "
        "public: "
        "v = 7; "
        "cb = 0; "
        "func keep(c) { this.cb = c; } "
        "} "
        "let b = new Box(); "
        "b.keep(func () -> int { return this.v; }); "
        "b",
        diagnostics);
    ASSERT_NE(chunk, nullptr);

    std::weak_ptr<Object> watched;
    {
        ir::VM vm(diagnostics);
        auto result = vm.run(chunk, {});
        ASSERT_FALSE(diagnostics.hasErrors());
        ASSERT_TRUE(std::holds_alternative<ObjectRef>(result));

        watched = std::get<ObjectRef>(result);
    }

    EXPECT_TRUE(watched.expired());
}

TEST(ReceiverBindingTest, CallbackDoesNotRetainGlobals) {
    DiagnosticEngine diagnostics;
    auto chunk = compile("let g = 1; let f = func () -> int { return g; }; f", diagnostics);
    ASSERT_NE(chunk, nullptr);

    FunctionRefPtr func;
    {
        ir::VM vm(diagnostics);
        auto result = vm.run(chunk, {});
        ASSERT_FALSE(diagnostics.hasErrors());
        ASSERT_TRUE(std::holds_alternative<FunctionRefPtr>(result));

        func = std::get<FunctionRefPtr>(result);
    }

    EXPECT_TRUE(func->globals.expired());
}
