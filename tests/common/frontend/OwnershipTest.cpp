#include <gtest/gtest.h>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

namespace {
    const std::string kPoint =
        "class Point { "
        "public: "
        "x = 0; "
        "Point(x) { this.x = x; } "
        "} ";

    const std::string kTake =
        "func take(p: own<Point>) -> int { return p.x; } ";

    const std::string kPeek =
        "func peek(p: ref<Point>) -> int { return p.x; } ";
}

TEST(OwnershipTest, MoveThroughParameter) {
    EXPECT_EQ(eval(kPoint + kTake +
        "let p: own<Point> = new Point(4); take(p)"), "4");
}

TEST(OwnershipTest, BorrowKeepsOwnerUsable) {
    EXPECT_EQ(eval(kPoint + kPeek +
        "let p: own<Point> = new Point(5); "
        "let r: ref<Point> = &p; "
        "peek(r) + peek(&p) + p.x"), "15");
}

TEST(OwnershipTest, OwnedReturnAdoptsTemporary) {
    EXPECT_EQ(eval(kPoint +
        "func make() -> own<Point> { return new Point(3); } "
        "let p: own<Point> = make(); p.x"), "3");
}

TEST(OwnershipTest, ReassignmentReinitializesMovedValue) {
    EXPECT_EQ(eval(kPoint + kTake +
        "func run() -> int { "
        "let p: own<Point> = new Point(1); "
        "let a = take(p); "
        "p = new Point(2); "
        "return a + p.x; "
        "} run()"), "3");
}

TEST(OwnershipTest, UseAfterMoveRejected) {
    EXPECT_THROW(eval(kPoint + kTake +
        "let p: own<Point> = new Point(4); take(p); take(p)"),
        std::runtime_error);
}

TEST(OwnershipTest, MoveWhileBorrowedRejected) {
    EXPECT_THROW(eval(kPoint + kTake +
        "let p: own<Point> = new Point(4); "
        "let r: ref<Point> = &p; take(p)"),
        std::runtime_error);
}

TEST(OwnershipTest, AssignWhileBorrowedRejected) {
    EXPECT_THROW(eval(kPoint +
        "let p: own<Point> = new Point(4); "
        "let r: ref<Point> = &p; "
        "p = new Point(5);"),
        std::runtime_error);
}

TEST(OwnershipTest, CopyOwnedRejected) {
    EXPECT_THROW(eval(kPoint +
        "let p: own<Point> = new Point(4); let q: Point = p;"),
        std::runtime_error);
}

TEST(OwnershipTest, PlainValueToOwnedParameterRejected) {
    EXPECT_THROW(eval(kPoint + kTake +
        "let p: Point = new Point(4); take(p)"),
        std::runtime_error);
}

TEST(OwnershipTest, RefReturnRejected) {
    EXPECT_THROW(eval(kPoint +
        "func bad() -> ref<Point> { return new Point(1); }"),
        std::runtime_error);
}

TEST(OwnershipTest, RefParameterRequiresBorrow) {
    EXPECT_THROW(eval(kPoint + kPeek +
        "let p = new Point(4); peek(p)"),
        std::runtime_error);
}

TEST(OwnershipTest, MoveInsideLoopRejected) {
    EXPECT_THROW(eval(kPoint + kTake +
        "let p: own<Point> = new Point(4); "
        "let i = 0; "
        "while (i < 2) [ take(p); i = i + 1; ]"),
        std::runtime_error);
}

TEST(OwnershipTest, MoveInOneBranchRejected) {
    EXPECT_THROW(eval(kPoint + kTake +
        "let p: own<Point> = new Point(4); "
        "if (p.x > 0) [ take(p); ] "
        "p.x"),
        std::runtime_error);
}

TEST(OwnershipTest, ClosureCaptureRejected) {
    EXPECT_THROW(eval(kPoint +
        "func run() -> int { "
        "let p: own<Point> = new Point(4); "
        "let f = func () -> int { return p.x; }; "
        "return f(); "
        "} run()"),
        std::runtime_error);
}

TEST(OwnershipTest, GlobalMoveInsideFunctionRejected) {
    EXPECT_THROW(eval(kPoint + kTake +
        "let p: own<Point> = new Point(4); "
        "func run() -> int { return take(p); } run()"),
        std::runtime_error);
}

TEST(OwnershipTest, OwnedMemberRejected) {
    EXPECT_THROW(eval(kPoint +
        "class Holder { public: p: own<Point> = new Point(1); }"),
        std::runtime_error);
}

TEST(OwnershipTest, ArrayLiteralRejected) {
    EXPECT_THROW(eval(kPoint +
        "let p: own<Point> = new Point(1); let all = [p];"),
        std::runtime_error);
}

TEST(OwnershipTest, NestedQualifierRejected) {
    EXPECT_THROW(eval(kPoint + "let p: own<own<Point>> = new Point(1);"),
        std::runtime_error);
}

TEST(OwnershipTest, ValueTypeQualifierRejected) {
    EXPECT_THROW(eval("let x: own<int> = 1;"), std::runtime_error);
    EXPECT_THROW(eval(kPoint + "let x: optional<own<Point>> = None;"),
        std::runtime_error);
}

TEST(OwnershipTest, SelfMoveRejected) {
    EXPECT_THROW(eval(kPoint +
        "let p: own<Point> = new Point(1); p = p;"),
        std::runtime_error);
}
