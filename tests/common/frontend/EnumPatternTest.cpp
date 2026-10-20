#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "common/frontend/CommonTest.h"

#include "LOICollectionA/frontend/DiagnosticEngine.h"

using namespace LOICollection::frontend;

namespace {
    const std::string kOptionSource = R"(
        enum Option {
            Some(int)
            Nothing
        }
    )";

    const std::string kPairSource = R"(
        enum Pair {
            Pt(int, int)
        }
    )";
}

/* P2: destructuring a payload-carrying variant binds the payload to the
 * name written inside the pattern. */
TEST(EnumPatternTest, MatchBindsPayload) {
    EXPECT_EQ(eval(kOptionSource + "\n let v = Some(42)\n"
                   " match (v) [ Some(x) [ x ] Nothing() [ 0 ] ]"), "42");
}

/* A payload-free variant is written with empty parentheses in the pattern. */
TEST(EnumPatternTest, MatchPayloadFreeVariant) {
    EXPECT_EQ(eval(kOptionSource + "\n let v = Nothing()\n"
                   " match (v) [ Some(x) [ x ] Nothing() [ -1 ] ]"), "-1");
}

/* An arm whose tag does not match must be skipped entirely; this is what
 * JMP_IF_TAG_NE is emitted for. */
TEST(EnumPatternTest, MatchSkipsMismatchedArm) {
    EXPECT_EQ(eval(kOptionSource + "\n let v = Some(7)\n"
                   " match (v) [ Nothing() [ \"nothing\" ] Some(x) [ \"some\" ] ]"),
              "some");
}

/* Two payload slots bind in declaration order. */
TEST(EnumPatternTest, MatchBindsMultiplePayloads) {
    EXPECT_EQ(eval(kPairSource + "\n let p = Pt(3, 4)\n"
                   " match (p) [ Pt(x, y) [ x + y ] ]"), "7");
}

/* P3: `if let` binds for the true branch. */
TEST(EnumPatternTest, IfLetBindsPayload) {
    EXPECT_EQ(eval(kOptionSource + "\n let v = Some(7)\n"
                   " if (let Some(x) = v) [ x ]"), "7");
}

/* When the tag does not match the guarded branch is skipped entirely. (`else`
 * is written as `[ true : false ]`, but a leading identifier there would be
 * read as a type annotation, so assert through a variable instead.) */
TEST(EnumPatternTest, IfLetSkipsBranchOnMismatch) {
    EXPECT_EQ(eval(kOptionSource + "\n let v = Nothing()\n let r = -1\n"
                   " if (let Some(x) = v) [ r = x ]\n r"), "-1");
    EXPECT_EQ(eval(kOptionSource + "\n let v = Some(5)\n let r = -1\n"
                   " if (let Some(x) = v) [ r = x ]\n r"), "5");
}

/* The destructured name belongs to the guarded branch only. */
TEST(EnumPatternTest, IfLetBindingDoesNotLeak) {
    EXPECT_THROW(eval(kOptionSource + "\n let v = Some(1)\n"
                      " if (let Some(x) = v) [ x ]\n x"), std::runtime_error);
}

/* P3: `while let` re-evaluates the value every iteration and stops at the
 * first mismatch. */
TEST(EnumPatternTest, WhileLetIteratesUntilMismatch) {
    EXPECT_EQ(eval(kOptionSource + R"(
        func pick(i) {
            if (i < 3) [ return Some(i); ]
            return Nothing();
        }
        let i = 0
        let total = 0
        while (let Some(x) = pick(i)) [ total = total + x; i = i + 1; ]
        total
    )"), "3");
}

/* A variant belonging to a different enum cannot match this target. */
TEST(EnumPatternTest, ForeignVariantIsRejected) {
    DiagnosticEngine diagnostics;
    auto mir = compile(
        "enum Option { Some(int) Nothing }\n"
        "enum Other { Yeah }\n"
        "let v = Some(1)\n"
        "match (v) [ Yeah() [ 1 ] _ [ 0 ] ]", diagnostics);

    EXPECT_EQ(mir, nullptr);
    EXPECT_TRUE(diagnostics.hasErrors());
}

/* The number of bound names must match the variant's payload width. */
TEST(EnumPatternTest, ArityMismatchIsRejected) {
    DiagnosticEngine diagnostics;
    auto mir = compile(
        kOptionSource + "\n let v = Some(1)\n"
        " match (v) [ Some(x, y) [ x ] Nothing() [ 0 ] ]", diagnostics);

    EXPECT_EQ(mir, nullptr);
    EXPECT_TRUE(diagnostics.hasErrors());
}
