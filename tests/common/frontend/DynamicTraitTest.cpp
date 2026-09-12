#include <gtest/gtest.h>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(DynamicTraitTest, DispatchOnRuntimeType) {
    EXPECT_EQ(eval(R"(
        trait Shape { func area() -> int; }
        class Circle { public: r = 2; }
        impl Shape for Circle { func area() -> int { return 3 * this.r * this.r; } }
        class Square { public: s = 3; }
        impl Shape for Square { func area() -> int { return this.s * this.s; } }
        func largest(a: dyn Shape, b: dyn Shape) -> int {
            let x = a.area();
            let y = b.area();
            if (x >= y) [ return x; ]
            return y;
        }
        [largest(new Circle(), new Square()), largest(new Square(), new Circle())]
    )"), "[12, 12]");
}

TEST(DynamicTraitTest, MethodWithArguments) {
    EXPECT_EQ(eval(R"(
        trait Calc { func add(a: int, b: int) -> int; }
        class Plus {}
        impl Calc for Plus { func add(a: int, b: int) -> int { return a + b; } }
        class Times {}
        impl Calc for Times { func add(a: int, b: int) -> int { return a * b; } }
        func calc(c: dyn Calc) -> int { return c.add(3, 4); }
        [calc(new Plus()), calc(new Times())]
    )"), "[7, 12]");
}

TEST(DynamicTraitTest, DynFieldAndInstanceof) {
    EXPECT_EQ(eval(R"(
        trait Shape { func area() -> int; }
        class Circle { public: r = 2; }
        impl Shape for Circle { func area() -> int { return 3 * this.r * this.r; } }
        class Square { public: s = 4; }
        impl Shape for Square { func area() -> int { return this.s * this.s; } }
        class ShapeHolder {
            public: item: dyn Shape;
            ShapeHolder(i: dyn Shape) { this.item = i; }
        }
        func total2() -> int {
            let h1 = new ShapeHolder(new Circle());
            let h2 = new ShapeHolder(new Square());
            let s: dyn Shape = h1.item;
            if (h1.item instanceof Circle) [ return s.area() + h2.item.area(); ]
            return 0;
        }
        total2()
    )"), "28");
}

TEST(DynamicTraitTest, NonSatisfyingClassRejected) {
    EXPECT_THROW(eval(R"(
        trait Shape { func area() -> int; }
        class Circle { public: r = 2; }
        impl Shape for Circle { func area() -> int { return 3 * this.r * this.r; } }
        class Text { public: t = "x"; }
        func bad(a: dyn Shape) -> int { return a.area(); }
        bad(new Text())
    )"), std::runtime_error);
}

TEST(DynamicTraitTest, UnknownTraitRejected) {
    EXPECT_THROW(eval(R"(
        func f(a: dyn Missing) -> int { return 0; }
        f(new Object())
    )"), std::runtime_error);
}

TEST(DynamicTraitTest, InvalidTraitTraitArgsRejected) {
    EXPECT_THROW(eval(R"(
        trait Shape { func area() -> int; }
        func f(a: dyn Shape<int>) -> int { return 0; }
        1
    )"), std::runtime_error);
}