#include <gtest/gtest.h>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(GenericInferenceTest, NestedClassArgument) {
    EXPECT_EQ(eval(R"(
        class Box<T> { public: value: T; Box(v: T) { this.value = v; } }
        func unbox<T>(b: Box<T>) -> T { return b.value; }
        unbox(new Box(42)) + 1
    )"), "43");
}

TEST(GenericInferenceTest, NestedOptionalArgument) {
    EXPECT_EQ(eval(R"(
        func pick<T>(x: optional<T>) -> T { return x.value; }
        pick(5)
    )"), "5");
}

TEST(GenericInferenceTest, TypeArgumentsAreInvariant) {
    EXPECT_THROW(eval(R"(
        class Box<T> { public: value: T; Box(v: T) { this.value = v; } }
        let a: Box<int> = new Box(1);
        let b: Box<string> = a;
        1
    )"), std::runtime_error);
}

TEST(GenericInferenceTest, ExplicitTypeArguments) {
    EXPECT_EQ(eval(R"(
        func id<T>(x: T) -> T { return x; }
        id<int>(5) + id::<int>(1)
    )"), "6");
}

TEST(GenericInferenceTest, ExplicitTypeArgumentsMustMatchArity) {
    EXPECT_THROW(eval(R"(
        func id<T>(x: T) -> T { return x; }
        id<int, string>(5)
    )"), std::runtime_error);
}

TEST(GenericInferenceTest, GenericClassRequiresTypeArguments) {
    EXPECT_THROW(eval(R"(
        class Box<T> { public: value: T; Box(v: T) { this.value = v; } }
        let b: Box = new Box(1);
        1
    )"), std::runtime_error);
}

TEST(GenericMethodTest, Inference) {
    EXPECT_EQ(eval(R"(
        class Box<T> {
            public: value: T;
            Box(v: T) { this.value = v; }
            func map<U>(f) -> U { return f(this.value); }
        }
        new Box(42).map(func (x) { return "v=" + x; })
    )"), "v=42");
}

TEST(GenericMethodTest, ExplicitTypeArgument) {
    EXPECT_EQ(eval(R"(
        class Box<T> {
            public: value: T;
            Box(v: T) { this.value = v; }
            func map<U>(f) -> U { return f(this.value); }
        }
        new Box(42).map<string>(func (x) { return "v=" + x; })
    )"), "v=42");
}

TEST(GenericMethodTest, NestedReceiverAndArgumentInference) {
    EXPECT_EQ(eval(R"(
        class Pair<A, B> {
            public: first: A;
            public: second: B;
            Pair(a: A, b: B) { this.first = a; this.second = b; }
        }
        class Box<T> {
            public: value: T;
            Box(v: T) { this.value = v; }
            func zip<U>(other: Box<U>) -> Pair<T, U> { return new Pair(this.value, other.value); }
        }
        let p = new Box(1).zip(new Box("s"));
        p.first + p.second.length()
    )"), "2");
}

TEST(GenericMethodTest, MethodBoundChecked) {
    EXPECT_THROW(eval(R"(
        trait Show { func show() -> string; }
        class Plain { }
        class Holder {
            func show<U: Show>(u: U) -> string { return u.show(); }
        }
        new Holder().show(new Plain())
    )"), std::runtime_error);
}

TEST(GenericMethodTest, StaticMethodTypeArgument) {
    EXPECT_EQ(eval(R"(
        class Factory {
            static func make<T>(v: T) -> T { return v; }
        }
        Factory::make(5) + Factory::make::<int>(1)
    )"), "6");
}

TEST(GenericEnumTest, ConstructionInferenceAndMatch) {
    EXPECT_EQ(eval(R"(
        enum Option<T> { Some(T) Nothing }
        func unwrapOr<T>(v: Option<T>, fallback: T) -> T {
            match (v) [
                Some(x) [ return x; ]
                Nothing() [ return fallback; ]
            ]
        }
        unwrapOr(Some(7), 0) + unwrapOr(Nothing<int>(), 1)
    )"), "8");
}

TEST(GenericEnumTest, PayloadTypeChecked) {
    EXPECT_THROW(eval(R"(
        enum Option<T> { Some(T) Nothing }
        let v: Option<int> = Some("x");
        1
    )"), std::runtime_error);
}

TEST(GenericEnumTest, TypeArgumentsRequired) {
    EXPECT_THROW(eval(R"(
        enum Option<T> { Some(T) Nothing }
        let v: Option = Some(1);
        1
    )"), std::runtime_error);
}

TEST(GenericBoundTest, ClassBoundAtConstruction) {
    EXPECT_THROW(eval(R"(
        trait Show { func show() -> string; }
        class Box<T: Show> { public: value: T; Box(v: T) { this.value = v; } }
        new Box(1)
    )"), std::runtime_error);
}

TEST(GenericBoundTest, OperatorNeedsBound) {
    EXPECT_THROW(eval(R"(
        func add<T>(a: T, b: T) -> T { return a + b; }
        1
    )"), std::runtime_error);
}

TEST(GenericBoundTest, OperatorBoundSatisfiedByPrimitive) {
    EXPECT_EQ(eval(R"(
        func add<T: Add>(a: T, b: T) -> T { return a + b; }
        add(2, 3)
    )"), "5");
}

TEST(GenericBoundTest, BoundPropagates) {
    EXPECT_EQ(eval(R"(
        trait Show { func show() -> string; }
        class Point { public: x = 0; func show() -> string { return "p"; } }
        func describe<T: Show>(v: T) -> string { return v.show(); }
        func indirect<T: Show>(v: T) -> string { return describe(v); }
        indirect(new Point())
    )"), "p");
}

TEST(GenericBoundTest, ImplBoundAllowsSatisfiedMethod) {
    EXPECT_EQ(eval(R"(
        trait Show { func show() -> string; }
        class Point { public: x = 0; func show() -> string { return "p"; } }
        class Box<T> { public: value: T; Box(v: T) { this.value = v; } }
        impl<T: Show> Box<T> { func render() -> string { return this.value.show(); } }
        new Box(new Point()).render()
    )"), "p");
}

TEST(GenericBoundTest, ImplBoundRejectsUnsatisfiedMethod) {
    EXPECT_THROW(eval(R"(
        trait Show { func show() -> string; }
        class Box<T> { public: value: T; Box(v: T) { this.value = v; } }
        impl<T: Show> Box<T> { func render() -> string { return this.value.show(); } }
        new Box(1).render()
    )"), std::runtime_error);
}

TEST(GenericAliasTest, GenericAlias) {
    EXPECT_EQ(eval(R"(
        class Box<T> { public: value: T; Box(v: T) { this.value = v; } }
        using Twice<T> = Box<Box<T>>;
        let b: Twice<int> = new Box(new Box(9));
        b.value.value
    )"), "9");
}

TEST(GenericAliasTest, GenericAliasArity) {
    EXPECT_THROW(eval(R"(
        class Box<T> { public: value: T; Box(v: T) { this.value = v; } }
        using Twice<T> = Box<Box<T>>;
        let b: Twice = new Box(1);
        1
    )"), std::runtime_error);
}

TEST(GenericAliasTest, GenericAliasBound) {
    EXPECT_THROW(eval(R"(
        trait Show { func show() -> string; }
        class Box<T> { public: value: T; Box(v: T) { this.value = v; } }
        using ShowBox<T: Show> = Box<T>;
        let b: ShowBox<int> = new Box(1);
        1
    )"), std::runtime_error);
}

TEST(GenericInheritanceTest, GenericBaseFieldsAndMethods) {
    EXPECT_EQ(eval(R"(
        class Base<T> {
            public: value: T;
            Base(v: T) { this.value = v; }
            func get() -> T { return this.value; }
        }
        class Child<T> extends Base<T> {
            Child(v: T) { super(v); }
            func twice() -> T { return super.get(); }
        }
        new Child(21).twice() + new Child(1).get()
    )"), "22");
}

TEST(GenericInheritanceTest, ConcreteBaseInstantiation) {
    EXPECT_EQ(eval(R"(
        class Base<T> { public: value: T; Base(v: T) { this.value = v; } }
        class Child extends Base<int> { Child(v: int) { super(v); } }
        new Child(8).value
    )"), "8");
}
