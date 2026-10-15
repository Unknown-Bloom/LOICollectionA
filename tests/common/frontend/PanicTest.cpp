#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(PanicTest, PanicHaltsExecution) {
    try {
        eval(R"(
            panic("boom");
            42
        )");
        FAIL() << "expected panic to abort execution";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("boom"), std::string::npos);
    }
}

TEST(PanicTest, PanicDefaultMessage) {
    try {
        eval(R"(
            panic;
            42
        )");
        FAIL() << "expected panic to abort execution";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("panic"), std::string::npos);
    }
}

TEST(PanicTest, PanicSkipsLaterStatements) {
    try {
        eval(R"(
            let x = 1;
            panic("stop here");
            x = x + 1;
            x
        )");
        FAIL() << "expected panic to abort execution";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("stop here"), std::string::npos);
    }
}

TEST(PanicTest, PanicInsideFunction) {
    try {
        eval(R"(
            func inner() -> int {
                panic("from inner");
                return 1;
            }
            let r = inner();
            r
        )");
        FAIL() << "expected panic to abort execution";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("from inner"), std::string::npos);
    }
}

TEST(PanicTest, PanicInsideLoop) {
    try {
        eval(R"(
            let total = 0;
            for (i in 0..10) [
                if (i == 3) [ panic("third"); ]
                total = total + i;
            ]
            total
        )");
        FAIL() << "expected panic to abort execution";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("third"), std::string::npos);
    }
}

TEST(PanicTest, PanicWithComputedMessage) {
    try {
        eval(R"(
            let code = 7;
            panic("code=" + code);
            42
        )");
        FAIL() << "expected panic to abort execution";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("code=7"), std::string::npos);
    }
}
