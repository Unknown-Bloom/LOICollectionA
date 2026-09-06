#include <gtest/gtest.h>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(LambdaTest, AssignAndCall) {
    EXPECT_EQ(eval("let f = func (a: int) -> int { return a + 1; }; f(1)"), "2");
}

TEST(LambdaTest, PassClassObject) {
    EXPECT_EQ(eval(
        "class Point { "
        "public: "
        "x = 0; "
        "y = 0; "
        "Point(x, y) { this.x = x; this.y = y; } "
        "} "
        "let f = func (p: Point) -> int { return p.x + p.y; }; "
        "f(new Point(3, 4))"),
        "7");
}

TEST(LambdaTest, PassClassObjectUntyped) {
    EXPECT_EQ(eval(
        "class Point { "
        "public: "
        "x = 0; "
        "Point(x) { this.x = x; } "
        "} "
        "let f = func (p) { return p.x; }; "
        "f(new Point(9))"),
        "9");
}

TEST(LambdaTest, PassAsArgument) {
    EXPECT_EQ(eval(
        "func apply(f, v: int) -> int { return f(v); } "
        "apply(func (x: int) -> int { return x * 2; }, 5)"),
        "10");
}

TEST(LambdaTest, CaptureOuterParameter) {
    EXPECT_EQ(eval(
        "func outer(a: int) -> int { "
        "let f = func (b: int) -> int { return a + b; }; "
        "return f(10); "
        "} "
        "outer(5)"),
        "15");
}

TEST(LambdaTest, CaptureThis) {
    EXPECT_EQ(eval(
        "class A { "
        "public: "
        "x = 3; "
        "func make() -> int { "
        "let f = func () -> int { return this.x; }; "
        "return f(); "
        "} "
        "} "
        "let a = new A(); "
        "a.make()"),
        "3");
}

TEST(LambdaTest, CapturedObjectOutlivesCreatingFrame) {
    EXPECT_EQ(eval(
        "class A { "
        "public: "
        "x = 3; "
        "} "
        "let cb = func () -> int { return 0; }; "
        "func make(c) -> int { "
        "cb = func () -> int { return c.x; }; "
        "return 0; "
        "} "
        "make(new A()); "
        "cb()"),
        "3");
}

TEST(LambdaTest, CapturedThisOutlivesCreatingFrame) {
    EXPECT_EQ(eval(
        "let cb = func () -> int { return 0; }; "
        "class A { "
        "public: "
        "x = 3; "
        "func make() -> int { "
        "cb = func () -> int { return this.x; }; "
        "return 0; "
        "} "
        "} "
        "let a = new A(); "
        "a.make(); "
        "cb()"),
        "3");
}

TEST(LambdaTest, WrongArgumentCount) {
    EXPECT_THROW(eval("let f = func (a: int) -> int { return a; }; f()"), std::runtime_error);
}

TEST(LambdaTest, CallFunctionRefFromNative) {
    DiagnosticEngine diagnostics;

    Lexer lexer("let f = func (a: int) -> int { return a + 1; }; f", diagnostics);
    Parser parser(lexer, diagnostics);
    auto ast = parser.parse();
    ASSERT_FALSE(diagnostics.hasErrors());

    if (ast->getType() == ASTNode::Type::Program) {
        SemanticAnalyzer analyzer(diagnostics);
        analyzer.analyze(static_cast<ProgramNode&>(*ast));
    }
    ASSERT_FALSE(diagnostics.hasErrors());

    ir::Compiler compiler(diagnostics);
    auto chunk = std::make_shared<ir::MirChunk>(compiler.compile(*ast));
    ASSERT_FALSE(diagnostics.hasErrors());

    ir::VM vm(diagnostics);
    auto result = vm.run(chunk, {});
    ASSERT_FALSE(diagnostics.hasErrors());
    ASSERT_TRUE(std::holds_alternative<FunctionRefPtr>(result));

    auto func = std::get<FunctionRefPtr>(result);
    auto called = ir::VM::callFunctionRef(func, CallbackTypeValues{ 2 }, {}, diagnostics);
    ASSERT_FALSE(diagnostics.hasErrors());
    EXPECT_EQ(ir::VM::valueToString(called), "3");
}

TEST(LambdaTest, CallFunctionRefKeepsCaptures) {
    DiagnosticEngine diagnostics;

    Lexer lexer("let a = 5; let f = func (b: int) -> int { return a + b; }; f", diagnostics);
    Parser parser(lexer, diagnostics);
    auto ast = parser.parse();
    ASSERT_FALSE(diagnostics.hasErrors());

    if (ast->getType() == ASTNode::Type::Program) {
        SemanticAnalyzer analyzer(diagnostics);
        analyzer.analyze(static_cast<ProgramNode&>(*ast));
    }
    ASSERT_FALSE(diagnostics.hasErrors());

    ir::Compiler compiler(diagnostics);
    auto chunk = std::make_shared<ir::MirChunk>(compiler.compile(*ast));
    ASSERT_FALSE(diagnostics.hasErrors());

    ir::VM vm(diagnostics);
    auto result = vm.run(chunk, {});
    ASSERT_FALSE(diagnostics.hasErrors());
    ASSERT_TRUE(std::holds_alternative<FunctionRefPtr>(result));

    auto func = std::get<FunctionRefPtr>(result);
    auto called = ir::VM::callFunctionRef(func, CallbackTypeValues{ 10 }, {}, diagnostics);
    ASSERT_FALSE(diagnostics.hasErrors());
    EXPECT_EQ(ir::VM::valueToString(called), "15");
}
