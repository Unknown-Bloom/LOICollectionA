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
