#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <string>

#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/ir/VM.h"

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;
using namespace LOICollection::frontend::ir;

TEST(VMValueTest, ValueToString) {
    EXPECT_EQ(VM::valueToString(ValueNode::ValueType(42)), "42");
    EXPECT_EQ(VM::valueToString(ValueNode::ValueType(3.5f)), "3.5");
    EXPECT_EQ(VM::valueToString(ValueNode::ValueType(std::string("hi"))), "hi");
    EXPECT_EQ(VM::valueToString(ValueNode::ValueType(true)), "true");
    EXPECT_EQ(VM::valueToString(ValueNode::ValueType(std::monostate{})), "None");

    auto obj = std::make_shared<Object>();
    obj->className = "Foo";
    EXPECT_EQ(VM::valueToString(ValueNode::ValueType(obj)), "instance of Foo");
}

TEST(VMValueTest, TypeNameOf) {
    EXPECT_EQ(VM::typeNameOf(ValueNode::ValueType(42)), "int");
    EXPECT_EQ(VM::typeNameOf(ValueNode::ValueType(3.5f)), "float");
    EXPECT_EQ(VM::typeNameOf(ValueNode::ValueType(std::string("hi"))), "string");
    EXPECT_EQ(VM::typeNameOf(ValueNode::ValueType(true)), "bool");
    EXPECT_EQ(VM::typeNameOf(ValueNode::ValueType(std::monostate{})), "none");

    auto obj = std::make_shared<Object>();
    obj->className = "Foo";
    EXPECT_EQ(VM::typeNameOf(ValueNode::ValueType(obj)), "Foo");
}

TEST(VMArithmeticTest, Arithmetic) {
    DiagnosticEngine diag;

    auto intResult = VM::applyArithmetic(2, 3, "+", diag);
    ASSERT_TRUE(std::holds_alternative<int>(intResult));
    EXPECT_EQ(std::get<int>(intResult), 5);

    auto floatResult = VM::applyArithmetic(1, 2.5f, "+", diag);
    ASSERT_TRUE(std::holds_alternative<float>(floatResult));
    EXPECT_FLOAT_EQ(std::get<float>(floatResult), 3.5f);

    auto strResult = VM::applyArithmetic(std::string("a"), 1, "+", diag);
    ASSERT_TRUE(std::holds_alternative<std::string>(strResult));
    EXPECT_EQ(std::get<std::string>(strResult), "a1");

    EXPECT_FALSE(diag.hasErrors());
}

TEST(VMArithmeticTest, ModuloRequiresIntegers) {
    DiagnosticEngine diag;

    [[maybe_unused]] auto result = VM::applyArithmetic(5.0f, 2.0f, "%", diag);
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_NE(diag.getErrorMessage().find("Modulo requires integral types"), std::string::npos);
}

TEST(VMArithmeticTest, ModuloByZero) {
    DiagnosticEngine diag;

    [[maybe_unused]] auto result = VM::applyArithmetic(5, 0, "%", diag);
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_NE(diag.getErrorMessage().find("Modulo by zero"), std::string::npos);
}

TEST(VMArithmeticTest, IntegerOverflow) {
    DiagnosticEngine diag;

    [[maybe_unused]] auto result = VM::applyArithmetic(std::numeric_limits<int>::max(), 1, "+", diag);
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_NE(diag.getErrorMessage().find("Integer overflow"), std::string::npos);
}

TEST(VMArithmeticTest, IntMinModuloMinusOne) {
    DiagnosticEngine diag;

    auto result = VM::applyArithmetic(std::numeric_limits<int>::min(), -1, "%", diag);
    EXPECT_FALSE(diag.hasErrors());
    EXPECT_EQ(std::get<int>(result), 0);
}

TEST(VMArithmeticTest, UnknownOperator) {
    DiagnosticEngine diag;

    [[maybe_unused]] auto result = VM::applyArithmetic(1, 2, "??", diag);
    EXPECT_TRUE(diag.hasErrors());
}

TEST(VMUnaryTest, Unary) {
    DiagnosticEngine diag;

    EXPECT_EQ(std::get<int>(VM::applyUnary(5, "-", diag)), -5);
    EXPECT_EQ(std::get<int>(VM::applyUnary(5, "+", diag)), 5);
    EXPECT_EQ(std::get<bool>(VM::applyUnary(true, "!", diag)), false);
    EXPECT_EQ(std::get<bool>(VM::applyUnary(0, "!", diag)), true);
    EXPECT_FALSE(diag.hasErrors());
}

TEST(VMUnaryTest, UnknownOperator) {
    DiagnosticEngine diag;

    [[maybe_unused]] auto result = VM::applyUnary(1, "~", diag);
    EXPECT_TRUE(diag.hasErrors());
}

TEST(VMUnaryTest, NegateIntMin) {
    DiagnosticEngine diag;

    [[maybe_unused]] auto result = VM::applyUnary(std::numeric_limits<int>::min(), "-", diag);
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_NE(diag.getErrorMessage().find("Integer overflow"), std::string::npos);
}

TEST(VMTruthTest, ValueToBool) {
    EXPECT_FALSE(VM::valueToBool(0));
    EXPECT_TRUE(VM::valueToBool(1));
    EXPECT_FALSE(VM::valueToBool(0.0f));
    EXPECT_TRUE(VM::valueToBool(0.5f));
    EXPECT_FALSE(VM::valueToBool(std::string("")));
    EXPECT_FALSE(VM::valueToBool(std::string("false")));
    EXPECT_FALSE(VM::valueToBool(std::string("FALSE")));
    EXPECT_TRUE(VM::valueToBool(std::string("true")));
    EXPECT_TRUE(VM::valueToBool(std::string("hello")));
    EXPECT_FALSE(VM::valueToBool(false));
    EXPECT_TRUE(VM::valueToBool(true));

    auto obj = std::make_shared<Object>();
    EXPECT_TRUE(VM::valueToBool(obj));
}

TEST(VMTruthTest, Utf8StringIsHandledSafely) {
    EXPECT_TRUE(VM::valueToBool(std::string("中文")));
}

TEST(VMComparisonTest, Comparisons) {
    DiagnosticEngine diag;

    EXPECT_TRUE(VM::applyComparison(1, 2, "<", diag));
    EXPECT_TRUE(VM::applyComparison(2, 1, ">", diag));
    EXPECT_TRUE(VM::applyComparison(1, 1, "==", diag));
    EXPECT_TRUE(VM::applyComparison(1.0f, 1, "==", diag));
    EXPECT_TRUE(VM::applyComparison(std::string("abc"), std::string("abd"), "<", diag));
    EXPECT_TRUE(VM::applyComparison(true, false, "!=", diag));
    EXPECT_FALSE(diag.hasErrors());
}

TEST(VMComparisonTest, ObjectEquality) {
    DiagnosticEngine diag;

    auto obj = std::make_shared<Object>();
    auto other = std::make_shared<Object>();

    EXPECT_TRUE(VM::applyComparison(obj, obj, "==", diag));
    EXPECT_TRUE(VM::applyComparison(obj, other, "!=", diag));
    EXPECT_FALSE(diag.hasErrors());
}

TEST(VMComparisonTest, TypeMismatch) {
    DiagnosticEngine diag;

    EXPECT_FALSE(VM::applyComparison(1, std::string("1"), "==", diag));
    EXPECT_TRUE(diag.hasErrors());
}

TEST(VMComparisonTest, UnknownOperator) {
    DiagnosticEngine diag;

    EXPECT_FALSE(VM::applyComparison(1, 2, "~", diag));
    EXPECT_TRUE(diag.hasErrors());
}

/* ---- §6.1 inline caches: VM call sites must stay correct across repeated
 * runtime calls (first call fills the slot, later calls hit it). ---- */

TEST(VMInlineCacheTest, RepeatedNativeFunctionCalls) {
    EXPECT_EQ(eval(
        "i = 0; "
        "s = 0; "
        "while (i < 4) [ "
        "    s = s + math::abs(0 - i - 1); "
        "    i = i + 1; "
        "]; "
        "s"), "10");
}

TEST(VMInlineCacheTest, RepeatedNativeValueMethodCalls) {
    EXPECT_EQ(eval(
        "arr = []; "
        "i = 0; "
        "while (i < 3) [ "
        "    arr.push(i); "
        "    i = i + 1; "
        "]; "
        "arr.join(\",\")"), "0,1,2");
}

TEST(VMInlineCacheTest, MixedShapeNativeCallsStayCorrect) {
    /* Alternating int/float argument shapes at nearby call sites: the
     * monomorphic slots must re-dispatch on every shape change. */
    EXPECT_EQ(eval(
        "i = 0; "
        "s = \"\"; "
        "while (i < 2) [ "
        "    s = s + math::abs(0 - i); "
        "    s = s + math::abs(i - 0.5); "
        "    i = i + 1; "
        "]; "
        "s"), "00.510.5");
}
