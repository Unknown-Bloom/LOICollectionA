#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(EscapeTest, CommonEscapes) {
    EXPECT_EQ(eval("\"a\\nb\""), "a\nb");
    EXPECT_EQ(eval("\"a\\tb\""), "a\tb");
    EXPECT_EQ(eval("\"a\\rb\""), "a\rb");
}

TEST(EscapeTest, QuoteEscapes) {
    EXPECT_EQ(eval("\"say \\\"hi\\\"\""), "say \"hi\"");
    EXPECT_EQ(eval("'say \\'hi\\''"), "say 'hi'");
}

TEST(EscapeTest, BackslashEscape) {
    EXPECT_EQ(eval("\"a\\\\b\""), "a\\b");
}

TEST(EscapeTest, UnknownEscapeStaysLiteral) {
    EXPECT_EQ(eval("\"a\\qb\""), "a\\qb");
    EXPECT_EQ(eval("\"x\\1y\""), "x\\1y");
}

TEST(EscapeTest, EscapeInsideLargerProgram) {
    EXPECT_EQ(eval("s = \"line1\\nline2\"; s.length()"), "11");
}
