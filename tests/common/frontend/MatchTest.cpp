#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(MatchTest, IntLiteralArms) {
    EXPECT_EQ(eval("match (2) [ 1 [ \"one\" ] 2 [ \"two\" ] _ [ \"many\" ] ]"), "two");
    EXPECT_EQ(eval("match (7) [ 1 [ \"one\" ] 2 [ \"two\" ] _ [ \"many\" ] ]"), "many");
    EXPECT_EQ(eval("match (0) [ 0 [ \"zero\" ] _ [ \"other\" ] ]"), "zero");
}

TEST(MatchTest, NegativeLiteralArm) {
    EXPECT_EQ(eval("match (-3) [ -3 [ \"minus three\" ] _ [ \"other\" ] ]"), "minus three");
    EXPECT_EQ(eval("match (3) [ -3 [ \"minus three\" ] _ [ \"other\" ] ]"), "other");
}

TEST(MatchTest, FloatLiteralArm) {
    EXPECT_EQ(eval("match (1.5) [ 1.5 [ \"half\" ] _ [ \"other\" ] ]"), "half");
    EXPECT_EQ(eval("match (2.5) [ -0.5 [ \"neg\" ] _ [ \"other\" ] ]"), "other");
}

TEST(MatchTest, StringAndBoolLiteralArms) {
    EXPECT_EQ(eval("match (\"b\") [ \"a\" [ 1 ] \"b\" [ 2 ] _ [ 3 ] ]"), "2");
    EXPECT_EQ(eval("match (true) [ false [ \"no\" ] true [ \"yes\" ] ]"), "yes");
}

TEST(MatchTest, FirstArmWins) {
    EXPECT_EQ(eval("match (1) [ _ [ \"first\" ] 1 [ \"second\" ] ]"), "first");
}

TEST(MatchTest, NoneArmMatchesOptional) {
    EXPECT_EQ(eval("let a: optional<int> = None; match (a) [ None [ \"empty\" ] _ [ \"value\" ] ]"), "empty");
    EXPECT_EQ(eval("let a: optional<int> = 5; match (a) [ None [ \"empty\" ] _ [ \"value\" ] ]"), "value");
}

TEST(MatchTest, BindArm) {
    EXPECT_EQ(eval("match (41) [ x [ x + 1 ] ]"), "42");
    EXPECT_EQ(eval("match (\"key\") [ s [ s + \"!\" ] ]"), "key!");
}

TEST(MatchTest, BinderDoesNotLeakPastMatch) {
    EXPECT_THROW(eval("match (2) [ x [ x ] ] x"), std::runtime_error);
}

TEST(MatchTest, BinderShadowsOuterVariableInsideArmOnly) {
    EXPECT_EQ(eval("let x = 1; match (2) [ x [ x ] ]"), "2");
    EXPECT_EQ(eval("let x = 1; let r = match (5) [ x [ x * 2 ] ]; x"), "1");
}

TEST(MatchTest, GuardArm) {
    EXPECT_EQ(eval(
        "match (15) [ n if (n > 10) [ \"big\" ] n [ \"small\" ] ]"), "big");
    EXPECT_EQ(eval(
        "match (5) [ n if (n > 10) [ \"big\" ] n [ \"small\" ] ]"), "small");
}

TEST(MatchTest, GuardOnlyEvaluatedWhenPatternMatches) {
    EXPECT_EQ(eval(
        "let calls = 0; "
        "func bump() { calls++; return true; } "
        "let r = match (1) [ 2 if (bump()) [ \"a\" ] _ [ \"b\" ] ]; "
        "r + \":\" + calls"), "b:0");
}

TEST(MatchTest, LiteralWithGuard) {
    EXPECT_EQ(eval(
        "match (2) [ 1 if (true) [ \"one\" ] 2 [ \"two\" ] _ [ \"many\" ] ]"), "two");
}

TEST(MatchTest, TypeArm) {
    EXPECT_EQ(eval(
        "class Circle { public: r = 2; } "
        "class Text { public: t = \"x\"; } "
        "let c = new Circle(); "
        "match (c) [ Text t [ t.t ] Circle c [ c.r ] _ [ 0 ] ]"), "2");
    EXPECT_EQ(eval(
        "class Circle { public: r = 2; } "
        "let s = \"plain\"; "
        "match (s) [ Circle c [ c.r ] _ [ \"not a shape\" ] ]"), "not a shape");
}

TEST(MatchTest, TypeArmFollowsInheritance) {
    EXPECT_EQ(eval(
        "class Base { public: kind = \"base\"; } "
        "class Child extends Base { public: kind = \"child\"; } "
        "let c = new Child(); "
        "match (c) [ Base b [ b.kind ] _ [ \"other\" ] ]"), "child");
}

TEST(MatchTest, TypeArmUnknownClassIsRejected) {
    EXPECT_THROW(eval("match (1) [ Missing m [ 0 ] _ [ 1 ] ]"), std::runtime_error);
}

TEST(MatchTest, MatchAsStatement) {
    EXPECT_EQ(eval(
        "let log = \"\"; "
        "match (1) [ "
        "    0 [ log += \"zero\" ] "
        "    1 [ log += \"one\" ] "
        "    _ [ log += \"many\" ] "
        "]; "
        "log"), "one");
}

TEST(MatchTest, MatchExpressionUsedInArithmetic) {
    EXPECT_EQ(eval("match (2) [ 1 [ 10 ] 2 [ 20 ] _ [ 30 ] ] + 1"), "21");
    EXPECT_EQ(eval("let a = match (\"x\") [ \"x\" [ 5 ] _ [ 0 ] ]; a * 3"), "15");
}

TEST(MatchTest, MultiStatementArmBody) {
    EXPECT_EQ(eval(
        "let r = match (3) [ "
        "    3 [ let a = 10; let b = 32; a + b ] "
        "    _ [ 0 ] "
        "]; "
        "r"), "42");
}

TEST(MatchTest, NestedMatch) {
    EXPECT_EQ(eval(
        "match (1) [ "
        "    1 [ match (\"in\") [ \"in\" [ 7 ] _ [ 8 ] ] ] "
        "    _ [ 9 ] "
        "]"), "7");
}

TEST(MatchTest, MatchOnVariant) {
    EXPECT_EQ(eval(
        "class Circle { public: r = 3; } "
        "class Square { public: s = 4; } "
        "func area(v: variant<Circle, Square>) -> int {"
        "    return match (v) [ Circle c [ 3 * c.r ] Square s [ s.s * s.s ] v [ 0 ] ]; "
        "} "
        "[area(new Circle()), area(new Square())]"), "[9, 16]");
}

TEST(MatchTest, MatchInsideFunctionWithReturn) {
    EXPECT_EQ(eval(
        "func describe(n: int) -> string {"
        "    match (n) ["
        "        0 [ return \"zero\" ]"
        "        n [ return \"nonzero\" ]"
        "    ];"
        "    return \"unreachable\";"
        "} "
        "[describe(0), describe(1)]"), "[zero, nonzero]");
}

TEST(MatchTest, MatchInsideForIn) {
    EXPECT_EQ(eval(
        "let out = \"\"; "
        "for (n in [1, 2, 3]) [ "
        "    out += match (n) [ 2 [ \"two\" ] _ [ \"x\" ] ]; "
        "]; "
        "out"), "xtwox");
}

TEST(MatchTest, NonExhaustiveMatchIsRejected) {
    EXPECT_THROW(eval("match (1) [ 1 [ \"one\" ] 2 [ \"two\" ] ]"), std::runtime_error);
    EXPECT_THROW(eval("match (1) [ 1 [ \"one\" ] n if (n > 0) [ \"pos\" ] ]"), std::runtime_error);
}

TEST(MatchTest, LiteralPatternTypeMismatchIsRejected) {
    EXPECT_THROW(eval("match (1) [ \"one\" [ 1 ] _ [ 2 ] ]"), std::runtime_error);
    EXPECT_THROW(eval("match (\"s\") [ 1 [ 1 ] _ [ 2 ] ]"), std::runtime_error);
    EXPECT_THROW(eval("match (true) [ \"yes\" [ 1 ] _ [ 2 ] ]"), std::runtime_error);
    EXPECT_THROW(eval("let a = 1; match (a) [ None [ 1 ] _ [ 2 ] ]"), std::runtime_error);
}

TEST(MatchTest, NumericPatternCrossKindIsAllowed) {
    EXPECT_EQ(eval("match (1.0) [ 1 [ \"int one\" ] _ [ \"other\" ] ]"), "int one");
    EXPECT_EQ(eval("match (1) [ 1.0 [ \"float one\" ] _ [ \"other\" ] ]"), "float one");
}

TEST(MatchTest, EmptyMatchIsRejected) {
    EXPECT_THROW(eval("match (1) [ ]"), std::runtime_error);
}

TEST(MatchTest, BreakInsideMatchIsRejected) {
    EXPECT_THROW(eval("match (1) [ _ [ break ] ]"), std::runtime_error);
}
