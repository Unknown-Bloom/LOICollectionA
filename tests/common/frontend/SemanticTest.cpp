#include <gtest/gtest.h>

#include "LOICollectionA/frontend/Lexer.h"
#include "LOICollectionA/frontend/Parser.h"
#include "LOICollectionA/frontend/SemanticAnalyzer.h"

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

TEST(SemanticTest, AnalyzeIsIdempotent) {
    DiagnosticEngine diagnostics;
    Lexer lexer(
        "class A { "
        "public: "
        "x = 1; "
        "func get() -> int { return x; } "
        "} "
        "let a = new A(); "
        "a.get()",
        diagnostics);
    Parser parser(lexer, diagnostics);

    auto ast = parser.parse();
    ASSERT_FALSE(diagnostics.hasErrors());

    SemanticAnalyzer analyzer(diagnostics);
    analyzer.analyze(static_cast<ProgramNode&>(*ast));
    ASSERT_FALSE(diagnostics.hasErrors());

    analyzer.analyze(static_cast<ProgramNode&>(*ast));
    EXPECT_FALSE(diagnostics.hasErrors());
}

TEST(SemanticTest, UntypedConstructorIsDynamic) {
    EXPECT_EQ(eval(
        "class A { "
        "A(x) { this.x = x; } "
        "public: "
        "x; "
        "} "
        "let a = new A(1); "
        "let b = new A(\"s\"); "
        "a.x + b.x"),
        "1s");
}
