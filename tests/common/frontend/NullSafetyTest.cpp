#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(NullSafetyTest, CoalesceOnNone) {
    EXPECT_EQ(eval("a = None; a ?? \"default\""), "default");
    EXPECT_EQ(eval("a = None; a ?? 0"), "0");
}

TEST(NullSafetyTest, CoalesceIgnoresFalsyValues) {
    EXPECT_EQ(eval("a = 0; a ?? \"default\""), "0");
    EXPECT_EQ(eval("a = \"\"; a ?? \"default\""), "");
    EXPECT_EQ(eval("a = false; a ?? \"default\""), "false");
    EXPECT_EQ(eval("a = 0.0; a ?? \"default\""), "0");
}

TEST(NullSafetyTest, CoalesceKeepsValue) {
    EXPECT_EQ(eval("a = \"value\"; a ?? \"default\""), "value");
    EXPECT_EQ(eval("a = 7; a ?? \"default\""), "7");
}

TEST(NullSafetyTest, CoalesceChains) {
    EXPECT_EQ(eval("a = None; b = None; a ?? b ?? \"last\""), "last");
    EXPECT_EQ(eval("a = None; b = \"b\"; a ?? b ?? \"last\""), "b");
}

TEST(NullSafetyTest, SafeFieldAccess) {
    EXPECT_EQ(eval("obj = None; obj?.field"), "None");
    EXPECT_EQ(eval("obj = None; obj?.field ?? \"fallback\""), "fallback");
}

TEST(NullSafetyTest, SafeFieldAccessOnValue) {
    EXPECT_EQ(eval(
        "class Data { public: name = \"inner\"; } "
        "d = new Data(); d?.name"),
        "inner");
}

TEST(NullSafetyTest, SafeChainShortCircuits) {
    EXPECT_EQ(eval("obj = None; obj?.a?.b?.c ?? \"end\""), "end");
}

TEST(NullSafetyTest, SafeIndexAccess) {
    EXPECT_EQ(eval("arr = None; arr?.[0]"), "None");
    EXPECT_EQ(eval("arr = None; arr?.[0] ?? \"empty\""), "empty");
    EXPECT_EQ(eval("arr = [10, 20]; arr?.[1]"), "20");
}

TEST(NullSafetyTest, MixedSafeAccess) {
    EXPECT_EQ(eval("obj = None; obj?.items?.[0] ?? \"none\""), "none");
}

TEST(NullSafetyTest, MixingWithLogicalOperatorsRequiresParens) {
    EXPECT_THROW(eval("a = None; a ?? \"x\" && true"), std::runtime_error);
    EXPECT_THROW(eval("a = None; true || a ?? \"x\""), std::runtime_error);
}

TEST(NullSafetyTest, ParenthesizedMixingAllowed) {
    EXPECT_EQ(eval("a = None; (a ?? \"x\") == \"x\""), "true");
}
