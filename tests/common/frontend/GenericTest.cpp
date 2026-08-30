#include <gtest/gtest.h>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(GenericTest, IdentityReusesAcrossTypes) {
    EXPECT_EQ(eval(R"(
        func id<T>(x: T) -> T { return x; }
        let a = id(5);
        let b = id("hello");
        a
    )"), "5");

    EXPECT_EQ(eval(R"(
        func id<T>(x: T) -> T { return x; }
        id("hello")
    )"), "hello");

    EXPECT_EQ(eval(R"(
        func id<T>(x: T) -> T { return x; }
        id(3.14)
    )"), "3.14");
}

TEST(GenericTest, PairMultipleTypeParams) {
    EXPECT_EQ(eval(R"(
        func pair<A, B>(a: A, b: B) -> A { return a; }
        let p = pair(1, "y");
        p
    )"), "1");

    EXPECT_EQ(eval(R"(
        func pair<A, B>(a: A, b: B) -> B { return b; }
        let p = pair(1, "y");
        p
    )"), "y");
}

TEST(GenericTest, BothParamsConstrainedToSameType) {
    EXPECT_EQ(eval(R"(
        func first<T>(x: T, y: T) -> T { return x; }
        first(3, 7)
    )"), "3");

    EXPECT_THROW(eval(R"(
        func first<T>(x: T, y: T) -> T { return x; }
        first(3, "x")
    )"), std::runtime_error);
}

TEST(GenericTest, GenericReturnValueUsable) {
    EXPECT_EQ(eval(R"(
        func wrap<T>(x: T) -> T { let v = x; return v; }
        let r = wrap(42);
        r + 8
    )"), "50");
}

TEST(GenericTest, DuplicateTypeParamName) {
    EXPECT_THROW(eval(R"(
        func dup<T, T>(x: T) -> T { return x; }
        1
    )"), std::runtime_error);
}

TEST(GenericTest, UndeclaredTypeParam) {
    EXPECT_THROW(eval(R"(
        func f(x: U) -> U { return x; }
        1
    )"), std::runtime_error);
}
