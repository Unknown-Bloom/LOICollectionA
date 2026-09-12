#include <gtest/gtest.h>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(GenericClassTest, ConstructorTypeInference) {
    EXPECT_EQ(eval(R"(
        class Box<T> {
            public: value: T;
            Box(v: T) { this.value = v; }
            func get() -> T { return value; }
        }
        let b = new Box(42);
        b.get()
    )"), "42");

    EXPECT_EQ(eval(R"(
        class Box<T> {
            public: value: T;
            Box(v: T) { this.value = v; }
            func get() -> T { return value; }
        }
        let b = new Box("hi");
        b.get()
    )"), "hi");
}

TEST(GenericClassTest, ExplicitTypeArguments) {
    EXPECT_EQ(eval(R"(
        class Box<T> {
            public: value: T;
            Box(v: T) { this.value = v; }
            func get() -> T { return value; }
        }
        let b = new Box<string>("hi");
        b.get()
    )"), "hi");
}

TEST(GenericClassTest, GenericMethodReturnSubstituted) {
    EXPECT_EQ(eval(R"(
        class Box<T> {
            public: value: T;
            Box(v: T) { this.value = v; }
            func get() -> T { return value; }
            func set(v: T) { this.value = v; }
        }
        let b = new Box(21);
        b.set(7);
        b.get() + 1
    )"), "8");
}

TEST(GenericClassTest, GenericFieldAccessSubstituted) {
    EXPECT_EQ(eval(R"(
        class Box<T> {
            public: value: T;
            Box(v: T) { this.value = v; }
        }
        let b = new Box(21);
        b.value + 21
    )"), "42");
}

TEST(GenericClassTest, MultipleTypeParameters) {
    EXPECT_EQ(eval(R"(
        class Pair<A, B> {
            public: first: A;
            public: second: B;
            Pair(f: A, s: B) { this.first = f; this.second = s; }
        }
        let p = new Pair("x", 5);
        p.second
    )"), "5");
}

TEST(GenericClassTest, GenericImplBlock) {
    EXPECT_EQ(eval(R"(
        class Box<T> {
            public: value: T;
            Box(v: T) { this.value = v; }
            func get() -> T { return value; }
        }
        impl<T> Box<T> {
            func twice() -> T { return value; }
        }
        let b = new Box(21);
        b.twice()
    )"), "21");
}

TEST(GenericClassTest, TypeMismatchTraitBound) {
    EXPECT_THROW(eval(R"(
        class Box<T> {
            public: value: T;
            Box(v: T) { this.value = v; }
            func set(v: T) { this.value = v; }
        }
        let b = new Box(21);
        b.set("s");
        1
    )"), std::runtime_error);
}

TEST(GenericClassTest, MissingTypeArgumentsError) {
    EXPECT_THROW(eval(R"(
        class Box<T> {
            public: value: T;
            Box(v: T) { this.value = v; }
        }
        let b = new Box();
        1
    )"), std::runtime_error);
}

TEST(GenericClassTest, TypeArgumentsOnPlainClassError) {
    EXPECT_THROW(eval(R"(
        class Foo {
            public: x: int;
            Foo() { this.x = 1; }
        }
        new Foo<int>()
    )"), std::runtime_error);
}

TEST(GenericClassTest, ImplTypeParamMismatchError) {
    EXPECT_THROW(eval(R"(
        class Box<T> {
            public: value: T;
            Box(v: T) { this.value = v; }
        }
        impl<U> Box<U> {
            func extra() -> U { return value; }
        }
        1
    )"), std::runtime_error);
}