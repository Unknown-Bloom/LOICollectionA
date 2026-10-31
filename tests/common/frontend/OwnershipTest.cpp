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
        "func take(p: Point) -> int { return p.x; } ";

    const std::string kPeek =
        "func peek(p: ref<Point>) -> int { return p.x; } ";
}

TEST(OwnershipTest, MoveThroughParameter) {
    EXPECT_EQ(eval(kPoint + kTake + "let p = new Point(4); take(p)"), "4");
}

TEST(OwnershipTest, BorrowKeepsOwnerUsable) {
    EXPECT_EQ(eval(kPoint + kPeek +
        "let p = new Point(5); "
        "let r: ref<Point> = &p; "
        "peek(r) + peek(&p) + p.x"), "15");
}

TEST(OwnershipTest, ReturnAdoptsTemporary) {
    EXPECT_EQ(eval(kPoint +
        "func make() -> Point { return new Point(3); } "
        "let p = make(); p.x"), "3");
}

TEST(OwnershipTest, ReturnMovesLocal) {
    EXPECT_EQ(eval(kPoint +
        "func make() -> Point { let p = new Point(6); return p; } "
        "make().x"), "6");
}

TEST(OwnershipTest, AssignmentMovesLocal) {
    EXPECT_EQ(eval(kPoint +
        "let a = new Point(1); let b = a; b.x"), "1");
}

TEST(OwnershipTest, CopyKeepsValueTypes) {
    EXPECT_EQ(eval("let a = 1; let b = a; a + b"), "2");
}

TEST(OwnershipTest, ReassignmentReinitializesMovedValue) {
    EXPECT_EQ(eval(kPoint + kTake +
        "func run() -> int { "
        "let p = new Point(1); "
        "let a = take(p); "
        "p = new Point(2); "
        "return a + p.x; "
        "} run()"), "3");
}

TEST(OwnershipTest, UseAfterMoveRejected) {
    EXPECT_THROW(eval(kPoint + kTake +
        "let p = new Point(4); take(p); take(p)"),
        std::runtime_error);
}

TEST(OwnershipTest, MoveAfterAliasMoveRejected) {
    EXPECT_THROW(eval(kPoint +
        "let a = new Point(1); let b = a; a.x"),
        std::runtime_error);
}

TEST(OwnershipTest, MoveWhileBorrowedRejected) {
    EXPECT_THROW(eval(kPoint + kTake +
        "let p = new Point(4); "
        "let r: ref<Point> = &p; take(p)"),
        std::runtime_error);
}

TEST(OwnershipTest, AssignWhileBorrowedRejected) {
    EXPECT_THROW(eval(kPoint +
        "let p = new Point(4); "
        "let r: ref<Point> = &p; "
        "p = new Point(5);"),
        std::runtime_error);
}

TEST(OwnershipTest, MoveOutOfRefRejected) {
    EXPECT_THROW(eval(kPoint + kTake +
        "let p = new Point(4); take(&p); "
        "func steal(r: ref<Point>) -> int { return take(r); }"),
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
        "let p = new Point(4); "
        "let i = 0; "
        "while (i < 2) [ take(p); i = i + 1; ]"),
        std::runtime_error);
}

TEST(OwnershipTest, MoveInOneBranchRejected) {
    EXPECT_THROW(eval(kPoint + kTake +
        "let p = new Point(4); "
        "if (p.x > 0) [ take(p); ] "
        "p.x"),
        std::runtime_error);
}

TEST(OwnershipTest, ClosureCaptureBorrows) {
    EXPECT_THROW(eval(kPoint + kTake +
        "func run() -> int { "
        "let p = new Point(4); "
        "let f = func () -> int { return p.x; }; "
        "take(p); "
        "return f(); "
        "} run()"),
        std::runtime_error);
}

TEST(OwnershipTest, ClosureCaptureKeepsOwnerUsable) {
    EXPECT_EQ(eval(kPoint +
        "func run() -> int { "
        "let p = new Point(4); "
        "let f = func () -> int { return p.x; }; "
        "return f() + p.x; "
        "} run()"), "8");
}

TEST(OwnershipTest, GlobalMoveInsideFunctionRejected) {
    EXPECT_THROW(eval(kPoint + kTake +
        "let p = new Point(4); "
        "func run() -> int { return take(p); } run()"),
        std::runtime_error);
}

TEST(OwnershipTest, GlobalAssignmentInsideFunctionAllowed) {
    EXPECT_EQ(eval(kPoint +
        "let cache = new Point(1); "
        "func reset() -> int { cache = new Point(2); return cache.x; } "
        "reset() + cache.x"), "4");
}

TEST(OwnershipTest, GlobalRefAssignmentInsideFunctionRejected) {
    EXPECT_THROW(eval(kPoint + kPeek +
        "let p = new Point(1); "
        "let g: ref<Point> = &p; "
        "func steal(r: ref<Point>) -> int { g = r; return 0; } "
        "steal(&p)"),
        std::runtime_error);
}

TEST(OwnershipTest, MemberIsOwnedByDefault) {
    EXPECT_EQ(eval(kPoint +
        "class Holder { public: p = 0; Holder(v: Point) { this.p = v; } } "
        "let h = new Holder(new Point(7)); h.p.x"), "7");
}

TEST(OwnershipTest, FieldStoreMovesLocal) {
    EXPECT_THROW(eval(kPoint +
        "class Holder { public: p = 0; } "
        "let h = new Holder(); "
        "let q = new Point(1); "
        "h.p = q; "
        "q.x"),
        std::runtime_error);
}

TEST(OwnershipTest, ArrayLiteralMovesLocal) {
    EXPECT_THROW(eval(kPoint +
        "let p = new Point(1); let all = [p]; p.x"),
        std::runtime_error);
}

TEST(OwnershipTest, ArrayElementReadAliases) {
    EXPECT_EQ(eval(kPoint +
        "let p = new Point(1); let all = [p]; all[0].x"), "1");
}

TEST(OwnershipTest, RefInFieldRejected) {
    EXPECT_THROW(eval(kPoint +
        "class Holder { public: p: ref<Point> = new Point(1); }"),
        std::runtime_error);
}

TEST(OwnershipTest, RefInArrayLiteralRejected) {
    EXPECT_THROW(eval(kPoint +
        "let p = new Point(1); let all = [&p];"),
        std::runtime_error);
}

TEST(OwnershipTest, NestedQualifierRejected) {
    EXPECT_THROW(eval(kPoint + "let p: ref<ref<Point>> = new Point(1);"),
        std::runtime_error);
}

TEST(OwnershipTest, ValueTypeQualifierRejected) {
    EXPECT_THROW(eval("let x: ref<int> = 1;"), std::runtime_error);
    EXPECT_THROW(eval(kPoint + "let x: optional<ref<Point>> = None;"),
        std::runtime_error);
}

TEST(OwnershipTest, SelfMoveRejected) {
    EXPECT_THROW(eval(kPoint +
        "let p = new Point(1); p = p;"),
        std::runtime_error);
}
