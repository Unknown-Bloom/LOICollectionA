#include <gtest/gtest.h>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(VariableTest, Evaluation) { 
    EXPECT_EQ(eval("let a = 5; a"), "5");
    EXPECT_EQ(eval("let a = 5; let b = a; b"), "5");
    EXPECT_EQ(eval("let a = \"test\"; a"), "test");
    EXPECT_EQ(eval("let a = \"test\"; let b = a + \"1\"; b"), "test1");
    EXPECT_EQ(eval("let a = true; a"), "true");
    EXPECT_EQ(eval("let a = 3.1400; a"), "3.14");
}

TEST(VariableTest, Calculate) { 
    EXPECT_EQ(eval("let a = 5; let b = 10; a + b"), "15");
    EXPECT_EQ(eval("let a = 5; let b = 10; a - b"), "-5");
    EXPECT_EQ(eval("let x = 5; let y = x + 5; y"), "10");
}
