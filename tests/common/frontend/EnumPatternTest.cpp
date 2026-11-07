#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "common/frontend/CommonTest.h"

#include "LOICollectionA/frontend/DiagnosticEngine.h"

using namespace LOICollection::frontend;

namespace {
    const std::string kPatternSource = R"(
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

TEST(EnumPatternTest, MatchBindsPayload) {
    EXPECT_EQ(eval(kPatternSource + "\n let v = Some(42)\n"
                   " match (v) [ Some(x) [ x ] Nothing() [ 0 ] ]"), "42");
}

TEST(EnumPatternTest, MatchPayloadFreeVariant) {
    EXPECT_EQ(eval(kPatternSource + "\n let v = Nothing()\n"
                   " match (v) [ Some(x) [ x ] Nothing() [ -1 ] ]"), "-1");
}

TEST(EnumPatternTest, MatchSkipsMismatchedArm) {
    EXPECT_EQ(eval(kPatternSource + "\n let v = Some(7)\n"
                   " match (v) [ Nothing() [ \"nothing\" ] Some(x) [ \"some\" ] ]"),
              "some");
}

TEST(EnumPatternTest, MatchBindsMultiplePayloads) {
    EXPECT_EQ(eval(kPairSource + "\n let p = Pt(3, 4)\n"
                   " match (p) [ Pt(x, y) [ x + y ] ]"), "7");
}

TEST(EnumPatternTest, IfLetBindsPayload) {
    EXPECT_EQ(eval(kPatternSource + "\n let v = Some(7)\n"
                   " if (let Some(x) = v) [ x ]"), "7");
}

TEST(EnumPatternTest, IfLetSkipsBranchOnMismatch) {
    EXPECT_EQ(eval(kPatternSource + "\n let v = Nothing()\n let r = -1\n"
                   " if (let Some(x) = v) [ r = x ]\n r"), "-1");
    EXPECT_EQ(eval(kPatternSource + "\n let v = Some(5)\n let r = -1\n"
                   " if (let Some(x) = v) [ r = x ]\n r"), "5");
}

TEST(EnumPatternTest, IfLetBindingDoesNotLeak) {
    EXPECT_THROW(eval(kPatternSource + "\n let v = Some(1)\n"
                      " if (let Some(x) = v) [ x ]\n x"), std::runtime_error);
}

TEST(EnumPatternTest, WhileLetIteratesUntilMismatch) {
    EXPECT_EQ(eval(kPatternSource + R"(
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

TEST(EnumPatternTest, ArityMismatchIsRejected) {
    DiagnosticEngine diagnostics;
    auto mir = compile(
        kPatternSource + "\n let v = Some(1)\n"
        " match (v) [ Some(x, y) [ x ] Nothing() [ 0 ] ]", diagnostics);

    EXPECT_EQ(mir, nullptr);
    EXPECT_TRUE(diagnostics.hasErrors());
}
