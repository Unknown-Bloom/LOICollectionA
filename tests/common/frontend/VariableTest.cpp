#include <gtest/gtest.h>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(VariableTest, Evaluation) {
    EXPECT_EQ(eval("let a = 5; a"), "5");
    EXPECT_EQ(eval("let a = 5; let b = a; b"), "5");
    EXPECT_EQ(eval("let a = \"test\"; a"), "test");
    EXPECT_EQ(eval("let a = \"test\"; let b = a + \"1\"; b"), "test1");
    EXPECT_EQ(eval("let a = true; a"), "true");
    EXPECT_EQ(eval("let a = 3.1400; a"), "3.14");
}

TEST(VariableTest, Calculate) {
    EXPECT_EQ(eval("let a = 5; let b = 10; a + b"), "15");
    EXPECT_EQ(eval("let a = 5; let b = 10; a - b"), "-5");
    EXPECT_EQ(eval("let x = 5; let y = x + 5; y"), "10");
}

TEST(VariableTest, LoopLocalSurvivesRecursiveCall) {
    EXPECT_EQ(eval(R"(
func f(d: int) -> string {
    let out = "";
    let i = 0;

    while (i < 1) [
        let key = "k" + d;
        if (d > 0) [ let inner = f(d - 1); ]
        out = out + key;
        i = i + 1;
    ]

    return out;
}

f(1))"), "k1");

    EXPECT_EQ(eval(R"(
func fib(n: int) -> int {
    if (n < 2) [ return n; ]
    let a = fib(n - 1);
    let b = fib(n - 2);
    return a + b;
}

fib(10))"), "55");
}

TEST(VariableTest, MethodLocalSurvivesRecursiveCall) {
    EXPECT_EQ(eval(R"(
class P {
    public:
        func f(d: int) -> string {
            let out = "";
            let i = 0;
            while (i < 1) [
                let key = "k" + d;
                if (d > 0) [ let inner = this.f(d - 1); ]
                out = out + key;
                i = i + 1;
            ]
            return out;
        }
}

new P().f(1))"), "k1");
}

TEST(VariableTest, CompileTimeConstants) {
    EXPECT_EQ(eval("const A = 1; A"), "1");
    EXPECT_EQ(eval("const A = 2; const B: int = A * 3 + 1; B"), "7");
    EXPECT_EQ(eval("const A = \"ab\"; const B = A + \"cd\"; B"), "abcd");
    EXPECT_EQ(eval("const A = -5; A"), "-5");
    EXPECT_EQ(eval("const ARR = [1, 2, 3]; ARR[1]"), "2");
    EXPECT_EQ(eval("const A = 1; if (true) [ const B = A + 10; ] B"), "11");
    EXPECT_EQ(eval("const L = [\"a\", \"b\"]; func f() -> string { const P = \"id:\"; return P + L[0]; } f()"), "id:a");
    EXPECT_EQ(eval("const A = 1; func f() -> int { const A = 9; return A; } f() + A"), "10");
}

TEST(VariableTest, CompileTimeConstantsRejectInvalidUse) {
    EXPECT_THROW(eval("const A = 1; A = 2; A"), std::runtime_error);
    EXPECT_THROW(eval("const A = 1; A += 1; A"), std::runtime_error);
    EXPECT_THROW(eval("let x = 1; const A = x; A"), std::runtime_error);
    EXPECT_THROW(eval("const A; A"), std::runtime_error);
    EXPECT_THROW(eval("const A = 1; const A = 2; A"), std::runtime_error);
    EXPECT_THROW(eval("const A = 1 / 0; A"), std::runtime_error);
    EXPECT_THROW(eval("const A = 2147483647 + 1; A"), std::runtime_error);
}
