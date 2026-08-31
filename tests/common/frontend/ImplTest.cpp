#include <gtest/gtest.h>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(ImplTest, InherentMethod) {
    EXPECT_EQ(eval(R"(
        class Point {
            public:
            x = 0;
            y = 0;
        }
        impl Point {
            func manhattan() -> int { return this.x + this.y; }
        }
        let p = new Point();
        p.x = 3;
        p.y = 4;
        p.manhattan()
    )"), "7");
}

TEST(ImplTest, InherentAssociatedFunction) {
    EXPECT_EQ(eval(R"(
        class Math { }
        impl Math {
            static func sq(n: int) -> int { return n * n; }
        }
        Math.sq(5)
    )"), "25");
}

TEST(ImplTest, InherentAssociatedConstant) {
    EXPECT_EQ(eval(R"(
        class Physics { }
        impl Physics {
            const G = 9.8;
        }
        Physics.G
    )"), "9.8");
}

TEST(ImplTest, InherentMethodsCallEachOther) {
    EXPECT_EQ(eval(R"(
        class Counter { public: n = 0; }
        impl Counter {
            func inc() { this.n = this.n + 1; }
            func value() -> int { return this.n; }
        }
        let c = new Counter();
        c.inc();
        c.inc();
        c.value()
    )"), "2");
}

TEST(ImplTest, InherentConstantUsedInMethod) {
    EXPECT_EQ(eval(R"(
        class Config { }
        impl Config {
            const BASE = 10;
            static func scaled(n: int) -> int { return Config.BASE * n; }
        }
        Config.scaled(3)
    )"), "30");
}

TEST(ImplTest, TraitImplNoArgs) {
    EXPECT_EQ(eval(R"(
        trait Show { func show() -> string; }
        class Point {
            public:
            x = 0;
        }
        impl Show for Point {
            func show() -> string { return "point"; }
        }
        func print_it<T: Show>(x: T) -> string { return x.show(); }
        let p = new Point();
        print_it(p)
    )"), "point");
}

TEST(ImplTest, TraitImplGenericBound) {
    EXPECT_EQ(eval(R"(
        trait Show { func show() -> string; }
        class Point { public: x = 0; }
        impl Show for Point { func show() -> string { return "impl-point"; } }
        func print_it<T: Show>(x: T) -> string { return x.show(); }
        let p = new Point();
        print_it(p)
    )"), "impl-point");
}

TEST(ImplTest, TraitImplParameterizedMethod) {
    EXPECT_EQ(eval(R"(
        trait Addable { func combine(o: int) -> int; }
        class Wallet { public: balance = 0; }
        impl Addable for Wallet {
            func combine(o: int) -> int { return this.balance + o; }
        }
        func total<T: Addable>(x: T, v: int) -> int { return x.combine(v); }
        let w = new Wallet();
        w.balance = 100;
        total(w, 25)
    )"), "125");
}

TEST(ImplTest, TraitImplMissingRequiredMethod) {
    EXPECT_THROW(eval(R"(
        trait Show { func show() -> string; }
        class Point { public: x = 0; }
        impl Show for Point {
            func other() -> int { return 1; }
        }
        1
    )"), std::runtime_error);
}

TEST(ImplTest, ImplTargetNotAClass) {
    EXPECT_THROW(eval(R"(
        impl NotAClass {
            func f() -> int { return 1; }
        }
        1
    )"), std::runtime_error);
}

TEST(ImplTest, ImplDuplicateMethod) {
    EXPECT_THROW(eval(R"(
        class A { public: func f() -> int { return 1; } }
        impl A {
            func f() -> int { return 2; }
        }
        1
    )"), std::runtime_error);
}

TEST(ImplTest, ImplUnknownTrait) {
    EXPECT_THROW(eval(R"(
        class A { }
        impl NoSuchTrait for A {
            func f() -> int { return 1; }
        }
        1
    )"), std::runtime_error);
}
