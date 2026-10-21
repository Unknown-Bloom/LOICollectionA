#include <gtest/gtest.h>

#include <string>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

namespace {
    std::string withResult(const std::string& body) {
        return "import \"/result.lcui\";\n" + body;
    }
}

TEST(StdMigrationTest, ResultIsNativeEnum) {
    EXPECT_EQ(eval(withResult("let r = Ok(42)\n r")), "enum#0[0](42)");
    EXPECT_EQ(eval(withResult("let r = Err(\"boom\")\n r")), "enum#0[1](boom)");
}

TEST(StdMigrationTest, ResultHelpers) {
    EXPECT_EQ(eval(withResult("resultIsOk(Ok(1))")), "true");
    EXPECT_EQ(eval(withResult("resultIsOk(Err(\"x\"))")), "false");
    EXPECT_EQ(eval(withResult("resultIsErr(Err(\"x\"))")), "true");
    EXPECT_EQ(eval(withResult("resultUnwrap(Ok(7))")), "7");
    EXPECT_EQ(eval(withResult("resultUnwrapOr(Err(\"x\"), 9)")), "9");
    EXPECT_EQ(eval(withResult("resultExpect(Ok(3), \"nope\")")), "3");
    EXPECT_EQ(eval(withResult("resultErrOr(Err(\"boom\"), \"none\")")), "boom");
}

TEST(StdMigrationTest, ResultMapTakesClosure) {
    EXPECT_EQ(eval(withResult(
        "resultUnwrap(resultMap(Ok(21), func (x) { return x * 2; }))")), "42");
}

TEST(StdMigrationTest, ResultMapErrAndAndThenTakeClosures) {
    EXPECT_EQ(eval(withResult(
        "resultErrOr(resultMapErr(Err(\"boom\"), func (e) { return e + \"!\"; }), \"\")")), "boom!");
    EXPECT_EQ(eval(withResult(
        "resultUnwrap(resultAndThen(Ok(20), func (x) { return Ok(x + 2); }))")), "22");
}

TEST(StdMigrationTest, ResultDestructuresWithMatch) {
    EXPECT_EQ(eval(withResult(
        "let r = Ok(5)\n"
        " match (r) [ Ok(v) [ \"ok:\" + v ] Err(e) [ \"err:\" + e ] ]")), "ok:5");
    EXPECT_EQ(eval(withResult(
        "let r = Err(\"down\")\n"
        " match (r) [ Ok(v) [ \"ok:\" + v ] Err(e) [ \"err:\" + e ] ]")), "err:down");
}
