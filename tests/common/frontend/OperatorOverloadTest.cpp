#include <gtest/gtest.h>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(OperatorOverloadTest, AddBinary) {
    EXPECT_EQ(eval(R"(
        class Vec2 {
            public:
            x = 0;
            y = 0;
        }
        impl Add for Vec2 {
            func op_add(rhs: Vec2) -> Vec2 {
                let r = new Vec2();
                r.x = this.x + rhs.x;
                r.y = this.y + rhs.y;
                return r;
            }
        }
        let a = new Vec2();
        a.x = 3;
        a.y = 4;
        let b = new Vec2();
        b.x = 10;
        b.y = 20;
        let c = a + b;
        c.x + c.y
    )"), "37");
}

TEST(OperatorOverloadTest, EqBinary) {
    EXPECT_EQ(eval(R"(
        class Money {
            public:
            cents = 0;
        }
        impl Eq for Money {
            func op_eq(rhs: Money) -> bool { return this.cents == rhs.cents; }
        }
        let a = new Money();
        a.cents = 100;
        let b = new Money();
        b.cents = 100;
        let c = new Money();
        c.cents = 200;
        (a == b) && !(a == c)
    )"), "true");
}

TEST(OperatorOverloadTest, NegUnary) {
    EXPECT_EQ(eval(R"(
        class Point {
            public:
            v = 0;
        }
        impl Neg for Point {
            func op_neg() -> Point {
                let r = new Point();
                r.v = -this.v;
                return r;
            }
        }
        let p = new Point();
        p.v = 5;
        let n = -p;
        n.v
    )"), "-5");
}

TEST(OperatorOverloadTest, LtBinary) {
    EXPECT_EQ(eval(R"(
        class Point {
            public:
            v = 0;
        }
        impl Lt for Point {
            func op_lt(rhs: Point) -> bool { return this.v < rhs.v; }
        }
        let a = new Point();
        a.v = 1;
        let b = new Point();
        b.v = 2;
        a < b
    )"), "true");
}

TEST(OperatorOverloadTest, PrimitiveIntUnaffected) {
    EXPECT_EQ(eval("let x = 1 + 2 * 3;\nx"), "7");
}

TEST(OperatorOverloadTest, NativeStringConcatUnaffected) {
    EXPECT_EQ(eval(R"(
        let s = "ab" + "cd";
        s
    )"), "abcd");
}