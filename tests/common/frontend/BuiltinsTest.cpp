#include <gtest/gtest.h>

#include "LOICollectionA/frontend/Callback.h"

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(MathBuiltinsTest, BasicMath) {
    EXPECT_EQ(eval("math::sqrt(9)"), "3");
    EXPECT_EQ(eval("math::pow(2,4)"), "16");
    EXPECT_EQ(eval("math::sin(0)"), "0");
    EXPECT_EQ(eval("math::cos(0)"), "1");
}

TEST(MathBuiltinsTest, MoreMath) {
    EXPECT_EQ(eval("math::sqrt(16)"), "4");
    EXPECT_EQ(eval("math::log(1)"), "0");
    EXPECT_EQ(eval("math::pow(2.5, 2.0)"), "6.25");
}

TEST(MathBuiltinsTest, RandomInRange) {
    for (int i = 0; i < 20; ++i) {
        int value = std::stoi(eval("math::random(5, 7)"));
        EXPECT_GE(value, 5);
        EXPECT_LE(value, 7);
    }
}

TEST(MathBuiltinsTest, RandomInvalidRangeErrors) {
    EXPECT_THROW(eval("math::random(5, 1)"), std::runtime_error);
}

TEST(BuiltinsTest, UnregisteredFunction) {
    EXPECT_THROW(eval("nonexistent::func()"), std::runtime_error);
    EXPECT_THROW(eval("math::nonexistent(1)"), std::runtime_error);
}

TEST(BuiltinsTest, WrongArgumentType) {
    EXPECT_THROW(eval("math::sqrt(\"x\")"), std::runtime_error);
    EXPECT_THROW(eval("string::length(123)"), std::runtime_error);
}

TEST(FormatBuiltinTest, NullArrayArgumentErrorsGracefully) {
    CallbackTypeValues args;
    args.emplace_back(std::string("{0}"));
    args.emplace_back(ArrayRef{});

    DiagnosticEngine diagnostics;
    auto result = FunctionCall::getInstance().callFunction("std", "format", args, {}, diagnostics);

    EXPECT_FALSE(result.has_value());
}

TEST(MacroTest, CustomMacro) {
    MacroCall::getInstance().registerMacro("test_greet",
        [](const CallbackTypeValues&) -> std::string {
            return "Hello from macro!";
        }, {});
    EXPECT_EQ(eval("{test_greet}"), "Hello from macro!");

    MacroCall::getInstance().registerMacro("test_add",
        [](const CallbackTypeValues& args) -> std::string {
            int a = std::get<int>(args[0]);
            int b = std::get<int>(args[1]);
            return std::to_string(a + b);
        }, { ParamType::INT, ParamType::INT });
    EXPECT_EQ(eval("{test_add(6,7)}"), "13");
}
