#include <gtest/gtest.h>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(TraitTest, BoundSatisfiedByClass) {
    EXPECT_EQ(eval(R"(
        trait Show { func show() -> string; }
        class Point {
            public:
            x = 0;
            y = 0;
            func show() -> string { return "point"; }
        }
        func print_it<T: Show>(x: T) -> string { return x.show(); }
        let p = new Point();
        print_it(p)
    )"), "point");
}

TEST(TraitTest, BoundNotSatisfiedByPrimitive) {
    EXPECT_THROW(eval(R"(
        trait Show { func show() -> string; }
        func print_it<T: Show>(x: T) -> string { return x.show(); }
        print_it(5)
    )"), std::runtime_error);
}

TEST(TraitTest, MethodCallResolvesThroughBound) {
    EXPECT_EQ(eval(R"(
        trait Named { func name() -> string; }
        class Widget {
            public:
            label = "w";
            func name() -> string { return this.label; }
        }
        func describe<T: Named>(x: T) -> string { return x.name(); }
        let w = new Widget();
        describe(w)
    )"), "w");
}

TEST(TraitTest, DuplicateTrait) {
    EXPECT_THROW(eval(R"(
        trait Show { func show() -> string; }
        trait Show { func show() -> string; }
        1
    )"), std::runtime_error);
}

TEST(TraitTest, UnknownTraitBound) {
    EXPECT_THROW(eval(R"(
        func f<T: Nonexistent>(x: T) -> T { return x; }
        1
    )"), std::runtime_error);
}
