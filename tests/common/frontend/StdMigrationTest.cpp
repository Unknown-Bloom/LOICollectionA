#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

namespace {
    /* Imported std modules are merged into the importing script as AST nodes,
     * so evaluating a module's contents followed by a body exercises the same
     * code path the loader produces. */
    std::string readStdModule(const std::string& name) {
        std::ifstream in(std::string(LOICOLLECTION_A_STD_DIR) + "/" + name);
        std::ostringstream out;
        out << in.rdbuf();
        return out.str();
    }

    std::string withResult(const std::string& body) {
        return readStdModule("result.lcui") + "\n" + body;
    }
}

/* P5: the std Result is a native tagged union, not a class with a flag. */
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

/* P4 + P5: combinators take a closure directly, no Fn1 subclass needed. */
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

/* P2 + P5: `match` destructures the std Result. */
TEST(StdMigrationTest, ResultDestructuresWithMatch) {
    EXPECT_EQ(eval(withResult(
        "let r = Ok(5)\n"
        " match (r) [ Ok(v) [ \"ok:\" + v ] Err(e) [ \"err:\" + e ] ]")), "ok:5");
    EXPECT_EQ(eval(withResult(
        "let r = Err(\"down\")\n"
        " match (r) [ Ok(v) [ \"ok:\" + v ] Err(e) [ \"err:\" + e ] ]")), "err:down");
}
