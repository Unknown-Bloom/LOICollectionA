#include <gtest/gtest.h>

#include <cmath>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(ParserEvalTest, LiteralValues) {
    EXPECT_EQ(eval("42"), "42");
    EXPECT_EQ(eval("3.14000"), "3.14");
    EXPECT_EQ(eval("true"), "true");
    EXPECT_EQ(eval("false"), "false");
    EXPECT_EQ(eval("\"hello\""), "hello");
}

TEST(ParserEvalTest, Arithmetic) {
    EXPECT_EQ(eval("1+2"), "3");
    EXPECT_EQ(eval("2*3+1"), "7");
    EXPECT_EQ(eval("10/3"), std::to_string(static_cast<float>(10.0 / 3.0)));
    EXPECT_EQ(eval("2^3"), "8");
    EXPECT_EQ(eval("10%3"), "1");
    EXPECT_EQ(eval("5^3^2"), std::to_string(static_cast<int>(std::pow(5, std::pow(3, 2)))));
}

TEST(ParserEvalTest, Unary) {
    EXPECT_EQ(eval("-5"), "-5");
    EXPECT_EQ(eval("-(-3)"), "3");
    EXPECT_EQ(eval("!true"), "false");
    EXPECT_EQ(eval("!false"), "true");
}

TEST(ParserEvalTest, Comparison) {
    EXPECT_EQ(eval("5>3"), "true");
    EXPECT_EQ(eval("5<3"), "false");
    EXPECT_EQ(eval("5==5"), "true");
    EXPECT_EQ(eval("5!=5"), "false");
    EXPECT_EQ(eval("5>=5"), "true");
    EXPECT_EQ(eval("5<=4"), "false");
}

TEST(ParserEvalTest, Logical) {
    EXPECT_EQ(eval("true && false"), "false");
    EXPECT_EQ(eval("true || false"), "true");
    EXPECT_EQ(eval("!true || 0"), "false");
    EXPECT_EQ(eval("true && 1"), "true");
}

TEST(ParserEvalTest, IfStatement) {
    EXPECT_EQ(eval("if(true)[1:2]"), "1");
    EXPECT_EQ(eval("if(false)[1:2]"), "2");
    EXPECT_EQ(eval("if(2>1)['yes':'no']"), "yes");
}

TEST(ParserEvalTest, IfWithoutElseKeepsStackBalanced) {
    EXPECT_EQ(eval("if(false)[1]; 42"), "42");
    EXPECT_EQ(eval("if(true)[1]; 42"), "42");
}

TEST(ParserEvalTest, IfStatementNested) {
    EXPECT_EQ(eval("if(6<7)[if(true)[1:2]:2]"), "1");
    EXPECT_EQ(eval("if(6<7)[if(false)[1:2]:2]"), "2");
    EXPECT_EQ(eval("if(6>7)[if(true)['yes':'no']:if(false)['yes2':'no2']]"), "no2");
}

TEST(ParserEvalTest, TemplateConcatenation) {
    EXPECT_EQ(eval("'hello' + 'world'"), "helloworld");
    EXPECT_EQ(eval("'abc' + 123"), "abc123");
}

TEST(ParserEvalTest, Transpile) {
    EXPECT_EQ(eval("$raw text}"), "rawtext}");
    EXPECT_EQ(eval("'prefix' + $inner} + ' suffix'"), "prefixinner} suffix");
}

TEST(ParserEvalTest, SyntaxError) {
    EXPECT_THROW(eval("if("), std::runtime_error);
    EXPECT_THROW(eval("(1+2"), std::runtime_error);
}

TEST(ParserEvalTest, MoreSyntaxErrors) {
    EXPECT_THROW(eval("1 +"), std::runtime_error);
    EXPECT_THROW(eval("if(1)"), std::runtime_error);
    EXPECT_THROW(eval("class Foo {"), std::runtime_error);
    EXPECT_THROW(eval("func foo("), std::runtime_error);
    EXPECT_THROW(eval("a = "), std::runtime_error);
    EXPECT_THROW(eval("}"), std::runtime_error);
    EXPECT_THROW(eval("1 2"), std::runtime_error);
}

TEST(ParserEvalTest, NestedFunctionDefinitionRejected) {
    EXPECT_THROW(eval("func f() { func g() {} }"), std::runtime_error);
    EXPECT_THROW(eval("func f() { class A {} }"), std::runtime_error);
}

TEST(ParserEvalTest, UntypedFunctionIsDynamic) {
    EXPECT_EQ(eval("func id(x) { return x; } id(1) + id(\"s\")"), "1s");
    EXPECT_EQ(eval("func id(x) { return x; } id(1); id(\"s\")"), "s");
}

TEST(ParserEvalTest, ErroneousConstantIsNotSilentlyFolded) {
    EXPECT_THROW(eval("10.0 % 3.0"), std::runtime_error);
}

TEST(ParserEvalTest, NestedExpressions) {
    EXPECT_EQ(eval("(1+2)*3"), "9");
    EXPECT_EQ(eval("if(3>2)[10:20] + 5"), "15");
    EXPECT_EQ(eval("if(3>2)['10':'20'] + '5'"), "105");
}

TEST(ParserEvalTest, StringConcatenation) {
    EXPECT_EQ(eval("\"Value: \" + '42'"), "Value: 42");
}

TEST(ParserEvalTest, ArithmeticPrecedence) {
    EXPECT_EQ(eval("1 + 2 * 3"), "7");
    EXPECT_EQ(eval("10 - 2 - 3"), "5");
    EXPECT_EQ(eval("20 / 2 / 5"), "2");
    EXPECT_EQ(eval("2 ^ 3 ^ 2"), "512");
}

TEST(ParserEvalTest, ArithmeticWithoutSpaces) {
    EXPECT_EQ(eval("a = 1; b = 2; a+b"), "3");
    EXPECT_EQ(eval("a = 5; b = 3; a-b"), "2");
    EXPECT_EQ(eval("a = 6; b = 7; a*b"), "42");
    EXPECT_EQ(eval("a = 9; b = 2; a/b"), "4.5");
    EXPECT_EQ(eval("a = 10; b = 3; a%b"), "1");
    EXPECT_EQ(eval("a = 2; b = 3; a^b"), "8");
}

TEST(ParserEvalTest, FractionalPower) {
    EXPECT_NEAR(std::stof(eval("2^0.5")), std::sqrt(2.0f), 1e-5f);
    EXPECT_NEAR(std::stof(eval("2^-1")), 0.5f, 1e-6f);
}

TEST(ParserEvalTest, MixedNumericArithmetic) {
    EXPECT_EQ(eval("1 + 2.5"), "3.5");
    EXPECT_EQ(eval("7 / 2"), "3.5");
    EXPECT_EQ(eval("2.5 * 4"), "10");
    EXPECT_EQ(eval("5 - 1.5"), "3.5");
}

TEST(ParserEvalTest, MoreUnary) {
    EXPECT_EQ(eval("+5"), "5");
    EXPECT_EQ(eval("-2.5"), "-2.5");
    EXPECT_EQ(eval("-(-5)"), "5");
    EXPECT_EQ(eval("!0"), "true");
    EXPECT_EQ(eval("!\"false\""), "true");
    EXPECT_EQ(eval("!!true"), "true");
}

TEST(ParserEvalTest, MoreComparisons) {
    EXPECT_EQ(eval("\"abc\" == \"abc\""), "true");
    EXPECT_EQ(eval("\"abc\" != \"abd\""), "true");
    EXPECT_EQ(eval("\"abc\" < \"abd\""), "true");
    EXPECT_EQ(eval("true == true"), "true");
    EXPECT_EQ(eval("false != true"), "true");
    EXPECT_EQ(eval("1 == 1.0"), "true");
}

TEST(ParserEvalTest, ComparisonTypeMismatchAtRuntime) {
    EXPECT_THROW(eval("a = 1; b = \"1\"; a == b"), std::runtime_error);
    EXPECT_THROW(eval("a = 1.5; b = \"1.5\"; a == b"), std::runtime_error);
}

TEST(ParserEvalTest, Truthiness) {
    EXPECT_EQ(eval("if(0)[1:2]"), "2");
    EXPECT_EQ(eval("if(0.0)[1:2]"), "2");
    EXPECT_EQ(eval("if(\"\")[1:2]"), "2");
    EXPECT_EQ(eval("if(\"false\")[1:2]"), "2");
    EXPECT_EQ(eval("if(\"TRUE\")[1:2]"), "1");
    EXPECT_EQ(eval("if(5)[1:2]"), "1");
    EXPECT_EQ(eval("if(\"hello\")[1:2]"), "1");
}

TEST(ParserEvalTest, VariableReassignment) {
    EXPECT_EQ(eval("a = 1; a = 2; a"), "2");
    EXPECT_EQ(eval("a = 1; a = a + 1; a"), "2");
    EXPECT_EQ(eval("a = 1; a = \"str\"; a"), "str");
    EXPECT_EQ(eval("a = 1; b = a; a = 5; b"), "1");
}

TEST(ParserEvalTest, UndefinedVariable) {
    EXPECT_THROW(eval("undefinedVar"), std::runtime_error);
}

TEST(ParserEvalTest, StatementSeparation) {
    EXPECT_EQ(eval("a = 1; b = 2; a + b"), "3");
    EXPECT_EQ(eval("a = 1\nb = 2\na + b"), "3");
}

TEST(ParserEvalTest, StringConcatenationWithOtherTypes) {
    EXPECT_EQ(eval("1 + \"a\""), "1a");
    EXPECT_EQ(eval("true + \"x\""), "truex");
    EXPECT_EQ(eval("\"pi=\" + 3.14"), "pi=3.14");
    EXPECT_EQ(eval("1 + 2 + \"abc\""), "3abc");
    EXPECT_EQ(eval("\"abc\" + 1 + 2"), "abc12");
}

TEST(ParserEvalTest, TranspileEdgeCases) {
    EXPECT_EQ(eval("$}"), "}");
    EXPECT_EQ(eval("$a}"), "a}");
    EXPECT_EQ(eval("$a b}"), "ab}");
    EXPECT_EQ(eval("$a;"), "a;");
    EXPECT_EQ(eval("1 + $x}"), "1x}");
}
