#include <gtest/gtest.h>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(LengthTest, ArrayLength) {
    EXPECT_EQ(eval("[].length()"), "0");
    EXPECT_EQ(eval("[1, 2, 3].length()"), "3");
    EXPECT_EQ(eval("let a = [1]; a[1] = 2; a.length()"), "2");
    EXPECT_EQ(eval("let arr = [1, 2]; arr.push(3); arr.length()"), "3");
}

TEST(LengthTest, StringLength) {
    EXPECT_EQ(eval("\"hello\".length()"), "5");
    EXPECT_EQ(eval("\"line1\\nline2\".length()"), "11");
}

TEST(LengthTest, MapLength) {
    EXPECT_EQ(eval("let m = new Map(); m.length()"), "0");
    EXPECT_EQ(eval("let m = new Map(); m.set(\"a\", 1); m.set(\"b\", 2); m.set(\"a\", 9); m.length()"), "2");
}

TEST(LengthTest, ForInStillIteratesArrays) {
    EXPECT_EQ(eval("let s = 0; for (x in [1, 2, 3]) [ s = s + x; ]; s"), "6");
}
