#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;

namespace {
    std::shared_ptr<ir::MirChunk> compileRaw(const std::string& input) {
        DiagnosticEngine diagnostics;

        Lexer lexer(input, diagnostics);
        Parser parser(lexer, diagnostics);

        auto ast = parser.parse();
        if (diagnostics.hasErrors())
            throw std::runtime_error(diagnostics.getErrorMessage());

        if (ast->getType() == ASTNode::Type::Program) {
            if (!ComponentExpander::expand(static_cast<ProgramNode&>(*ast), diagnostics))
                throw std::runtime_error(diagnostics.getErrorMessage());
            SemanticAnalyzer analyzer(diagnostics);
            analyzer.analyze(static_cast<ProgramNode&>(*ast));
        }

        ir::Compiler compiler(diagnostics);
        auto mir = std::make_shared<ir::MirChunk>(compiler.compile(*ast));
        if (diagnostics.hasErrors())
            throw std::runtime_error(diagnostics.getErrorMessage());

        return mir;
    }

    int countCalls(const ir::MirChunk& chunk) {
        int count = 0;
        for (const auto& instr : chunk.code)
            if (instr.op == ir::MirOp::CALL_METHOD ||
                instr.op == ir::MirOp::CALL_METHOD_VIRTUAL)
                ++count;
        for (const auto& body : chunk.methodBodies)
            count += countCalls(*body);
        return count;
    }
}

TEST(InlineTest, LeafMethodResultPreserved) {
    EXPECT_EQ(eval(R"(
        class Num {
            public: v: int;
            Num(x: int) { this.v = x; }
            func add(a: int, b: int) -> int { return v + a + b; }
            func twice() -> int { return this.add(this.add(v, 1), 2); }
        }
        let n = new Num(10);
        n.twice()
    )"), "33");
}

TEST(InlineTest, FieldMutationThroughLeafMethod) {
    EXPECT_EQ(eval(R"(
        class Counter {
            public: c: int;
            Counter() { this.c = 0; }
            func inc() { this.c = c + 2; }
            func get() -> int { return c; }
        }
        let k = new Counter();
        k.inc();
        k.inc();
        k.get()
    )"), "4");
}

TEST(InlineTest, DiscardedMethodReturn) {
    EXPECT_EQ(eval(R"(
        class P {
            public: v: int;
            P(x: int) { this.v = x; }
            func bump() -> int { return v + 1; }
        }
        let p = new P(1);
        p.bump();
        p.bump();
        p.v
    )"), "1");
}

TEST(InlineTest, CrossReceiverLeafComposition) {
    EXPECT_EQ(eval(R"(
        class Point {
            public: x: int;
            public: y: int;
            Point(a: int, b: int) { this.x = a; this.y = b; }
            func sum() -> int { return x + y; }
            func scale(f: int) -> int { return this.sum() * f; }
        }
        let q = new Point(3, 4);
        q.scale(2)
    )"), "14");
}

TEST(InlineTest, LeafReturnsFreshObject) {
    EXPECT_EQ(eval(R"(
        class Wrap {
            public: v: int;
            Wrap(x: int) { this.v = x; }
            func make(x: int) -> Wrap { return new Wrap(x); }
            func get() -> int { return v; }
        }
        let w = new Wrap(1);
        w.make(99).get()
    )"), "99");
}

TEST(InlineTest, LeafCallsInlinedInMIR) {
    auto mir = compileRaw(R"(
        class Num {
            public: v: int;
            Num(x: int) { this.v = x; }
            func add(a: int, b: int) -> int { return a + b; }
        }
        let n = new Num(10);
        n.add(n.add(1, 2), 3)
    )");

    const int before = countCalls(*mir);

    ir::Optimizer optimizer;
    optimizer.setEnabledPasses(static_cast<unsigned>(ir::Optimizer::Pass::Inline));
    optimizer.optimize(*mir);

    const int after = countCalls(*mir);

    EXPECT_LT(after, before);
}

TEST(InlineTest, InlinedLeafResultCorrect) {
    EXPECT_EQ(eval(R"(
        class Num {
            public: v: int;
            Num(x: int) { this.v = x; }
            func add(a: int, b: int) -> int { return a + b; }
        }
        let n = new Num(10);
        n.add(n.add(1, 2), 3)
    )"), "6");
}