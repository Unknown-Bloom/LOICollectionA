#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(LoopTest, WhileLoop) {
    EXPECT_EQ(eval("let i = 0; while (i < 5) [ i = i + 1; ]; i"), "5");
}

TEST(LoopTest, WhileLoopWithStep) {
    EXPECT_EQ(eval("let i = 0; while (i < 5) [ i = i + 2; ]; i"), "6");
}

TEST(LoopTest, WhileFalseNeverRuns) {
    EXPECT_EQ(eval("let x = 0; while (false) [ x = x + 1; ]; x"), "0");
}

TEST(LoopTest, WhileTrueWithBreak) {
    EXPECT_EQ(eval("let i = 0; while (true) [ i = i + 1; if (i == 3) [ break; ] ]; i"), "3");
}

TEST(LoopTest, WhileContinueSkipsRest) {
    EXPECT_EQ(eval(
        "let s = 0; "
        "let i = 0; "
        "while (i < 10) [ "
        "    i = i + 1; "
        "    if (i % 2 == 0) [ continue; ]; "
        "    s = s + i; "
        "]; "
        "s"), "25");
}

TEST(LoopTest, ForLoop) {
    EXPECT_EQ(eval("let s = 0; for (let i = 0; i < 10; i = i + 1) [ s = s + i; ]; s"), "45");
}

TEST(LoopTest, ForContinueSkipsRest) {
    EXPECT_EQ(eval(
        "let s = 0; "
        "for (let i = 0; i < 10; i = i + 1) [ "
        "    if (i % 2 == 0) [ continue; ]; "
        "    s = s + i; "
        "]; "
        "s"), "25");
}

TEST(LoopTest, ForBreak) {
    EXPECT_EQ(eval(
        "let s = 0; "
        "for (let i = 0; i < 10; i = i + 1) [ "
        "    if (i == 5) [ break; ]; "
        "    s = s + i; "
        "]; "
        "s"), "10");
}

TEST(LoopTest, ForWithoutClauses) {
    EXPECT_EQ(eval(
        "let i = 0; "
        "for (;;) [ "
        "    i = i + 1; "
        "    if (i == 5) [ break; ] "
        "]; "
        "i"), "5");
}

TEST(LoopTest, NestedForLoops) {
    EXPECT_EQ(eval(
        "let s = 0; "
        "for (let i = 0; i < 3; i = i + 1) [ "
        "    for (let j = 0; j < 3; j = j + 1) [ "
        "        s = s + 1; "
        "    ] "
        "]; "
        "s"), "9");
}

TEST(LoopTest, NestedBreakAffectsInnerOnly) {
    EXPECT_EQ(eval(
        "let s = 0; "
        "for (let i = 0; i < 3; i = i + 1) [ "
        "    for (let j = 0; j < 3; j = j + 1) [ "
        "        if (j == 1) [ break; ]; "
        "        s = s + 1; "
        "    ] "
        "]; "
        "s"), "3");
}

TEST(LoopTest, NestedContinueAffectsInnerOnly) {
    EXPECT_EQ(eval(
        "let s = 0; "
        "for (let i = 0; i < 2; i = i + 1) [ "
        "    for (let j = 0; j < 3; j = j + 1) [ "
        "        if (j == 1) [ continue; ]; "
        "        s = s + 1; "
        "    ] "
        "]; "
        "s"), "4");
}

TEST(LoopTest, LoopInsideFunction) {
    EXPECT_EQ(eval(
        "func sum(n: int) -> int { "
        "    let s = 0; "
        "    let i = 0; "
        "    while (i < n) [ "
        "        i = i + 1; "
        "        s = s + i; "
        "    ]; "
        "    return s; "
        "} "
        "sum(5)"), "15");
}

TEST(LoopTest, BreakOutsideLoopFails) {
    EXPECT_THROW(eval("break;"), std::runtime_error);
}

TEST(LoopTest, ContinueOutsideLoopFails) {
    EXPECT_THROW(eval("continue;"), std::runtime_error);
}

TEST(LoopTest, BreakInsideAnonFunctionFails) {
    EXPECT_THROW(eval(
        "for (let i = 0; i < 3; i = i + 1) [ "
        "    let f = func () { break; }; "
        "]"), std::runtime_error);
}

TEST(LoopTest, ContinueInsideAnonFunctionFails) {
    EXPECT_THROW(eval(
        "while (true) [ "
        "    let f = func () { continue; }; "
        "    break; "
        "]"), std::runtime_error);
}

TEST(LoopTest, BreakInsideNamedFunctionFails) {
    EXPECT_THROW(eval(
        "while (true) [ "
        "    func inner() { break; }; "
        "    break; "
        "]"), std::runtime_error);
}

TEST(LoopTest, LoopInsideAnonFunctionIsIsolated) {
    EXPECT_EQ(eval(
        "let f = func () -> int { "
        "    let s = 0; "
        "    for (let j = 0; j < 10; j = j + 1) [ "
        "        if (j == 3) [ break; ]; "
        "        s = s + j; "
        "    ]; "
        "    return s; "
        "}; "
        "f()"), "3");
}

TEST(LoopTest, LoopInsideClassMethodIsIsolated) {
    EXPECT_EQ(eval(
        "class Counter { "
        "public: "
        "func sum() -> int { "
        "    let s = 0; "
        "    let i = 0; "
        "    while (i < 10) [ "
        "        i = i + 1; "
        "        if (i == 4) [ break; ]; "
        "        s = s + i; "
        "    ]; "
        "    return s; "
        "} "
        "} "
        "new Counter().sum()"), "6");
}
