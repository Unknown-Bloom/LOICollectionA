#include <gtest/gtest.h>

#include <string>

#include "LOICollectionA/frontend/Lexer.h"
#include "LOICollectionA/frontend/Parser.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/SemanticAnalyzer.h"

#include "LOICollectionA/frontend/ir/Compiler.h"
#include "LOICollectionA/frontend/ir/Optimizer.h"
#include "LOICollectionA/frontend/ir/VM.h"
#include "LOICollectionA/frontend/ir/Lir.h"

using namespace LOICollection::frontend;
using namespace LOICollection::frontend::ir;

namespace {
    struct CompiledProgram {
        std::shared_ptr<Chunk> chunk;
        DiagnosticEngine diagnostics;
        Optimizer::Stats stats;
    };

    CompiledProgram compileAndOptimize(const std::string& source, unsigned mask = Optimizer::allPasses) {
        CompiledProgram out;

        Lexer lexer(source, out.diagnostics);
        Parser parser(lexer, out.diagnostics);

        auto ast = parser.parse();
        if (ast->getType() == ASTNode::Type::Program) {
            SemanticAnalyzer analyzer(out.diagnostics);
            analyzer.analyze(static_cast<ProgramNode&>(*ast));
        }

        Compiler compiler(out.diagnostics);
        auto mir = std::make_shared<Chunk>(compiler.compile(*ast));

        Optimizer optimizer;
        optimizer.setEnabledPasses(mask);
        out.stats = optimizer.optimize(*mir);

        out.chunk = mir;
        return out;
    }

    bool containsOp(const Chunk& chunk, Op op) {
        for (const auto& instr : chunk.code)
            if (instr.op == op)
                return true;
        return false;
    }

    int countOp(const Chunk& chunk, Op op) {
        int total = 0;
        for (const auto& instr : chunk.code)
            if (instr.op == op)
                ++total;
        return total;
    }

    int countOpEverywhere(const Chunk& chunk, Op op) {
        int total = countOp(chunk, op);
        for (const auto& body : chunk.methodBodies)
            total += countOp(*body, op);
        return total;
    }

    bool containsOpMir(const Chunk& chunk, Op op) {
        for (const auto& instr : chunk.code)
            if (instr.op == op)
                return true;
        return false;
    }

    int countOpMir(const Chunk& chunk, Op op) {
        int total = 0;
        for (const auto& instr : chunk.code)
            if (instr.op == op)
                ++total;
        return total;
    }
}

TEST(OptimizerTest, ConstantFolding) {
    auto program = compileAndOptimize("1 + 2 * 3");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GE(program.stats.folded, 2u);

    EXPECT_FALSE(containsOp(*program.chunk, Op::ADD));
    EXPECT_FALSE(containsOp(*program.chunk, Op::MUL));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "7");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, ConstantConditionElimination) {
    auto program = compileAndOptimize("if(true)[1:2]");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GE(program.stats.folded, 1u);

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "1");
}

TEST(OptimizerTest, UnreachableBranchRemoved) {
    auto program = compileAndOptimize("if(false)[1:2]");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GT(program.stats.removed, 0u);

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "2");
}

TEST(OptimizerTest, FoldsInsideFunctionBodies) {
    auto program = compileAndOptimize("func f() -> int { return 1 + 2; } f()");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GE(program.stats.folded, 1u);

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "3");
}

TEST(OptimizerTest, FoldsLogicalConstants) {
    auto program = compileAndOptimize("true && true");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GE(program.stats.folded, 1u);

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "true");
}

TEST(OptimizerTest, ErroneousConstantsAreNotFolded) {
    auto program = compileAndOptimize("10.0 % 3.0");

    EXPECT_FALSE(program.diagnostics.hasErrors());

    VM vm(program.diagnostics);
    [[maybe_unused]] auto result = vm.run(program.chunk, {});
    EXPECT_TRUE(program.diagnostics.hasErrors());
    EXPECT_NE(program.diagnostics.getErrorMessage().find("(at line 1, col "), std::string::npos);
}

TEST(OptimizerTest, RuntimeErrorCarriesSourceLocation) {
    auto program = compileAndOptimize("let a = 1; let b = \"1\"; a == b");
    EXPECT_FALSE(program.diagnostics.hasErrors());

    VM vm(program.diagnostics);
    [[maybe_unused]] auto result = vm.run(program.chunk, {});
    EXPECT_TRUE(program.diagnostics.hasErrors());
    EXPECT_NE(program.diagnostics.getErrorMessage().find("(at line 1, col "), std::string::npos);
}

TEST(OptimizerTest, OptionalUnwrapErrorCarriesSourceLocation) {
    auto program = compileAndOptimize("let b: optional<string> = None; b");
    EXPECT_FALSE(program.diagnostics.hasErrors());

    VM vm(program.diagnostics);
    [[maybe_unused]] auto result = vm.run(program.chunk, {});
    EXPECT_TRUE(program.diagnostics.hasErrors());
    EXPECT_NE(program.diagnostics.getErrorMessage().find("Optional value is empty"), std::string::npos);
    EXPECT_NE(program.diagnostics.getErrorMessage().find("(at line 1, col "), std::string::npos);
}

TEST(OptimizerTest, FunctionCallErrorCarriesSourceLocation) {
    auto program = compileAndOptimize("nonexistent::foo()");
    EXPECT_FALSE(program.diagnostics.hasErrors());

    VM vm(program.diagnostics);
    [[maybe_unused]] auto result = vm.run(program.chunk, {});
    EXPECT_TRUE(program.diagnostics.hasErrors());
    EXPECT_NE(program.diagnostics.getErrorMessage().find("Function not registered"), std::string::npos);
    EXPECT_NE(program.diagnostics.getErrorMessage().find("(at line 1, col "), std::string::npos);
}

TEST(OptimizerTest, FoldsConstantArrayLiteral) {
    auto program = compileAndOptimize("[1, 2, 3]");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GE(program.stats.folded, 1u);

    EXPECT_FALSE(containsOp(*program.chunk, Op::MAKE_ARRAY));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "[1, 2, 3]");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, FoldsConstantArrayIndex) {
    auto program = compileAndOptimize("[1, 2, 3][1]");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GE(program.stats.folded, 2u);

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "2");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, ConstantArrayIsClonedPerEvaluation) {
    auto program = compileAndOptimize(
        "func make() { return [1, 2]; } "
        "let a = make(); "
        "let b = make(); "
        "a[0] = 9; "
        "b[0]");

    EXPECT_FALSE(program.diagnostics.hasErrors());

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "1");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, NestedConstantArrayIsClonedDeeply) {
    auto program = compileAndOptimize(
        "func make() { return [[1]]; } "
        "let a = make(); "
        "let b = make(); "
        "a[0][0] = 9; "
        "b[0][0]");

    EXPECT_FALSE(program.diagnostics.hasErrors());

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "1");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, FoldsTypeIntrospectionOpcodes) {
    ir::Chunk chunk;
    chunk.slotCount = 6;
    chunk.constants.push_back(42);
    chunk.constants.push_back(std::monostate{});

    chunk.code = {
        { Op::LOAD_CONST, 0, 0 },
        { Op::TYPE_OF, 0, 1, 0 },
        { Op::LOAD_CONST, 1, 2 },
        { Op::HAS_VALUE, 0, 3, 2 },
        { Op::LOAD_CONST, 0, 4 },
        { Op::UNWRAP, 0, 5, 4 },
        { Op::HALT, 0 }
    };

    Optimizer optimizer;
    auto stats = optimizer.optimize(chunk);

    EXPECT_GE(stats.folded, 3u);

    EXPECT_FALSE(containsOpMir(chunk, Op::TYPE_OF));
    EXPECT_FALSE(containsOpMir(chunk, Op::HAS_VALUE));
    EXPECT_FALSE(containsOpMir(chunk, Op::UNWRAP));

    DiagnosticEngine diag;
    VM vm(diag);
    EXPECT_EQ(VM::valueToString(vm.run(std::make_shared<Chunk>(std::move(chunk)), {})), "42");
    EXPECT_FALSE(diag.hasErrors());
}

TEST(OptimizerTest, DoesNotFoldUnwrapOfEmptyOptional) {
    ir::Chunk chunk;
    chunk.slotCount = 2;
    chunk.constants.push_back(std::monostate{});

    chunk.code = {
        { Op::LOAD_CONST, 0, 0 },
        { Op::UNWRAP, 0, 1, 0 },
        { Op::HALT, 0 }
    };

    Optimizer optimizer;
    auto stats = optimizer.optimize(chunk);

    EXPECT_EQ(stats.folded, 0u);
    EXPECT_TRUE(containsOpMir(chunk, Op::UNWRAP));

    DiagnosticEngine diag;
    VM vm(diag);
    [[maybe_unused]] auto result = vm.run(std::make_shared<Chunk>(std::move(chunk)), {});
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_NE(diag.getErrorMessage().find("Optional value is empty"), std::string::npos);
}

TEST(OptimizerTest, WhileFalseBodyEliminated) {
    auto program = compileAndOptimize("let x = 0; while (false) [ x = x + 1; ]; x");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GT(program.stats.removed, 0u);

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "0");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, ForFalseConditionBodyEliminated) {
    auto program = compileAndOptimize("let x = 0; for (let i = 0; false; i = i + 1) [ x = x + 1; ]; x");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GT(program.stats.removed, 0u);

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "0");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, WhileTrueWithBreakSurvivesOptimization) {
    auto program = compileAndOptimize("let i = 0; while (true) [ i = i + 1; if (i == 3) [ break; ] ]; i");

    EXPECT_FALSE(program.diagnostics.hasErrors());

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "3");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, ForLoopSurvivesOptimization) {
    auto program = compileAndOptimize("let s = 0; for (let i = 0; i < 10; i = i + 1) [ s = s + i; ]; s");

    EXPECT_FALSE(program.diagnostics.hasErrors());

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "45");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, LoopWithContinueSurvivesOptimization) {
    auto program = compileAndOptimize(
        "let s = 0; "
        "for (let i = 0; i < 10; i = i + 1) [ "
        "    if (i % 2 == 0) [ continue; ]; "
        "    s = s + i; "
        "]; "
        "s");

    EXPECT_FALSE(program.diagnostics.hasErrors());

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "25");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, PropagatesStoredScalarIntoArithmetic) {
    auto program = compileAndOptimize("let x = 4; x += 0; x");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GE(program.stats.folded, 2u);

    EXPECT_FALSE(containsOp(*program.chunk, Op::LOAD_VAR));
    EXPECT_FALSE(containsOp(*program.chunk, Op::ADD));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "4");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, PropagatesStoredStringConstants) {
    auto program = compileAndOptimize("let s = \"ab\"; let t = s + \"c\"; t");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GE(program.stats.folded, 2u);
    EXPECT_FALSE(containsOp(*program.chunk, Op::LOAD_VAR));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "abc");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, PropagationTracksLatestAssignment) {
    auto program = compileAndOptimize("let x = 1; x = 2; x");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_FALSE(containsOp(*program.chunk, Op::LOAD_VAR));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "2");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, PropagationDropsSlotForNonScalarValues) {
    auto program = compileAndOptimize("let x = [1, 2]; x = [3, 4]; x");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_TRUE(containsOp(*program.chunk, Op::LOAD_VAR));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "[3, 4]");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, PropagationInvalidatedAtBranchMerge) {
    const std::string source =
        "func f(a: int) -> int { if (a == 1) [ let r = 10 : r = 20 ]; return r; } ";

    auto taken = compileAndOptimize(source + "f(1)");
    EXPECT_FALSE(taken.diagnostics.hasErrors());

    VM vmTaken(taken.diagnostics);
    EXPECT_EQ(VM::valueToString(vmTaken.run(taken.chunk, {})), "10");
    EXPECT_FALSE(taken.diagnostics.hasErrors());

    auto untaken = compileAndOptimize(source + "f(2)");
    EXPECT_FALSE(untaken.diagnostics.hasErrors());

    VM vmUntaken(untaken.diagnostics);
    EXPECT_EQ(VM::valueToString(vmUntaken.run(untaken.chunk, {})), "20");
    EXPECT_FALSE(untaken.diagnostics.hasErrors());

    bool bodyKeepsLoad = false;
    for (const auto& body : untaken.chunk->methodBodies)
        if (containsOp(*body, Op::LOAD_SLOT))
            bodyKeepsLoad = true;

    EXPECT_TRUE(bodyKeepsLoad);
}

TEST(OptimizerTest, PropagationInvalidatedByCalls) {
    ir::Chunk chunk;
    chunk.slotCount = 3;
    chunk.constants.push_back(3);
    chunk.constants.push_back(std::string("x"));
    chunk.constants.push_back(std::string("hook"));

    chunk.code = {
        { Op::LOAD_CONST, 0, 0 },
        { Op::STORE_VAR, 1, 1, 0 },
        { Op::CALL, 2, 2, 2 },
        { Op::LOAD_VAR, 1, 3, 1 },
        { Op::HALT, 0 }
    };

    Optimizer optimizer;
    optimizer.optimize(chunk);

    EXPECT_TRUE(containsOpMir(chunk, Op::LOAD_VAR));
    EXPECT_EQ(chunk.code.size(), 5u);
}

namespace {
    std::size_t optimizeInvalidationProbe(Op op) {
        ir::Chunk chunk;
        chunk.slotCount = 3;
        chunk.constants.push_back(3);
        chunk.constants.push_back(std::string("x"));
        chunk.constants.push_back(std::string("hook"));

        chunk.code = {
            { Op::LOAD_CONST, 0, 0 },
            { Op::STORE_VAR, 1, 1, 0 },
            { op, 2, 2, 2 },
            { Op::LOAD_VAR, 1, 3, 1 },
            { Op::HALT, 0 }
        };

        Optimizer optimizer;
        optimizer.optimize(chunk);

        EXPECT_TRUE(containsOpMir(chunk, Op::LOAD_VAR));

        return chunk.code.size();
    }
}

TEST(OptimizerTest, PropagationInvalidatedByByNameMethodCalls) {
    EXPECT_EQ(optimizeInvalidationProbe(Op::CALL_METHOD_BY_NAME), 5u);
}

TEST(OptimizerTest, PropagationInvalidatedByNativeMethodCalls) {
    EXPECT_EQ(optimizeInvalidationProbe(Op::CALL_NATIVE_METHOD), 5u);
}

TEST(OptimizerTest, NegatedWhileConditionPreservesNot) {
    auto program = compileAndOptimize("let i = 0; while (!(i >= 2)) [ i = i + 1; ]; i");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_TRUE(containsOp(*program.chunk, Op::NOT));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "2");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, FixedPointExposesPropagationAfterBranchRemoval) {
    auto program = compileAndOptimize("let x = 1; if (x == 1) [ let y = 10 : y = 20 ]; y");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GE(program.stats.folded, 3u);
    EXPECT_GE(program.stats.removed, 3u);

    EXPECT_FALSE(containsOp(*program.chunk, Op::LOAD_VAR));
    EXPECT_FALSE(containsOp(*program.chunk, Op::CMP_EQ));
    EXPECT_FALSE(containsOp(*program.chunk, Op::JMP));
    EXPECT_FALSE(containsOp(*program.chunk, Op::JMP_IF_FALSE));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "10");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, EliminatesAlgebraicIdentities) {
    ir::Chunk chunk;
    chunk.slotCount = 5;
    chunk.constants.push_back(42);
    chunk.constants.push_back(0);
    chunk.constants.push_back(1);

    chunk.code = {
        { Op::LOAD_CONST, 0, 0 },
        { Op::LOAD_CONST, 1, 1 },
        { Op::ADD, 0, 2, 0, 1 },
        { Op::LOAD_CONST, 2, 3 },
        { Op::MUL, 0, 4, 2, 3 },
        { Op::HALT, 0 }
    };

    Optimizer optimizer;
    auto stats = optimizer.optimize(chunk);

    EXPECT_EQ(stats.folded, 2u);
    EXPECT_FALSE(containsOpMir(chunk, Op::ADD));

    DiagnosticEngine diag;
    VM vm(diag);
    EXPECT_EQ(VM::valueToString(vm.run(std::make_shared<Chunk>(std::move(chunk)), {})), "42");
    EXPECT_FALSE(diag.hasErrors());
}

TEST(OptimizerTest, EliminatesFloatMulDivPowIdentities) {
    ir::Chunk chunk;
    chunk.slotCount = 7;
    chunk.constants.push_back(2.5f);
    chunk.constants.push_back(1);
    chunk.constants.push_back(1);
    chunk.constants.push_back(1);

    chunk.code = {
        { Op::LOAD_CONST, 0, 0 },
        { Op::LOAD_CONST, 1, 1 },
        { Op::MUL, 0, 2, 0, 1 },
        { Op::LOAD_CONST, 2, 3 },
        { Op::DIV, 0, 4, 2, 3 },
        { Op::LOAD_CONST, 3, 5 },
        { Op::POW, 0, 6, 4, 5 },
        { Op::HALT, 0 }
    };

    Optimizer optimizer;
    auto stats = optimizer.optimize(chunk);

    EXPECT_EQ(stats.folded, 3u);

    DiagnosticEngine diag;
    VM vm(diag);
    EXPECT_EQ(VM::valueToString(vm.run(std::make_shared<Chunk>(std::move(chunk)), {})), "2.5");
    EXPECT_FALSE(diag.hasErrors());
}

TEST(OptimizerTest, FloatAddZeroIsNotIdentityFolded) {
    ir::Chunk chunk;
    chunk.slotCount = 3;
    chunk.constants.push_back(-0.0f);
    chunk.constants.push_back(0.0f);

    chunk.code = {
        { Op::LOAD_CONST, 0, 0 },
        { Op::LOAD_CONST, 1, 1 },
        { Op::ADD, 0, 2, 0, 1 },
        { Op::HALT, 0 }
    };

    Optimizer optimizer;
    auto stats = optimizer.optimize(chunk);

    EXPECT_GE(stats.folded, 1u);
    EXPECT_FALSE(containsOpMir(chunk, Op::ADD));

    DiagnosticEngine diag;
    VM vm(diag);
    EXPECT_EQ(VM::valueToString(vm.run(std::make_shared<Chunk>(std::move(chunk)), {})), "-0");
    EXPECT_FALSE(diag.hasErrors());
}

TEST(OptimizerTest, PropagatesSignedZeroCorrectly) {
    auto program = compileAndOptimize("let x = -0.0; x");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GE(program.stats.folded, 1u);
    EXPECT_FALSE(containsOp(*program.chunk, Op::NEG));
    EXPECT_FALSE(containsOp(*program.chunk, Op::LOAD_VAR));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "-0");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, FoldsFloatAddZeroWithCorrectSignedZero) {
    auto program = compileAndOptimize("let x = -0.0; let y = x + 0.0; y");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_FALSE(containsOp(*program.chunk, Op::ADD));
    EXPECT_FALSE(containsOp(*program.chunk, Op::LOAD_VAR));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "0");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, FoldsIsNoneOpcode) {
    ir::Chunk chunk;
    chunk.slotCount = 4;
    chunk.constants.push_back(std::monostate{});
    chunk.constants.push_back(42);

    chunk.code = {
        { Op::LOAD_CONST, 0, 0 },
        { Op::IS_NONE, 0, 1, 0 },
        { Op::LOAD_CONST, 1, 2 },
        { Op::IS_NONE, 0, 3, 2 },
        { Op::HALT, 0 }
    };

    Optimizer optimizer;
    auto stats = optimizer.optimize(chunk);

    EXPECT_EQ(stats.folded, 2u);
    EXPECT_FALSE(containsOpMir(chunk, Op::IS_NONE));
}

TEST(OptimizerTest, ReusesScalarConstantsWhenFolding) {
    ir::Chunk chunk;
    chunk.slotCount = 3;
    chunk.constants.push_back(5);
    chunk.constants.push_back(0);

    chunk.code = {
        { Op::LOAD_CONST, 0, 0 },
        { Op::LOAD_CONST, 1, 1 },
        { Op::ADD, 0, 2, 0, 1 },
        { Op::HALT, 0 }
    };

    Optimizer optimizer;
    optimizer.optimize(chunk);

    EXPECT_FALSE(containsOpMir(chunk, Op::ADD));

    DiagnosticEngine diag;
    VM vm(diag);
    EXPECT_EQ(VM::valueToString(vm.run(std::make_shared<Chunk>(std::move(chunk)), {})), "5");
    EXPECT_FALSE(diag.hasErrors());
}

TEST(OptimizerTest, CoalesceKeepsDuplicatedOperand) {
    auto noneCase = compileAndOptimize("let x = None; x ?? \"d\"");

    EXPECT_FALSE(noneCase.diagnostics.hasErrors());

    VM vmNone(noneCase.diagnostics);
    EXPECT_EQ(VM::valueToString(vmNone.run(noneCase.chunk, {})), "d");
    EXPECT_FALSE(noneCase.diagnostics.hasErrors());

    auto valueCase = compileAndOptimize("let x = \"v\"; x ?? \"d\"");

    EXPECT_FALSE(valueCase.diagnostics.hasErrors());

    VM vmValue(valueCase.diagnostics);
    EXPECT_EQ(VM::valueToString(vmValue.run(valueCase.chunk, {})), "v");
    EXPECT_FALSE(valueCase.diagnostics.hasErrors());
}

namespace {
    std::string runChunk(const std::shared_ptr<Chunk>& chunk, DiagnosticEngine& diag) {
        VM vm(diag);
        return VM::valueToString(vm.run(chunk, {}));
    }
}

TEST(OptimizerTest, PassMaskTurnsOffConstantFolding) {
    const std::string source = "let a = 1 + 2; let b = 3 * 4; a + b";

    auto full = compileAndOptimize(source);
    auto noFold = compileAndOptimize(source, static_cast<unsigned>(Optimizer::Pass::DeadCode));

    EXPECT_GT(full.stats.folded, 0u);
    EXPECT_EQ(noFold.stats.folded, 0u);
    EXPECT_FALSE(containsOp(*full.chunk, Op::ADD));
    EXPECT_TRUE(containsOp(*noFold.chunk, Op::ADD));
    EXPECT_GT(noFold.chunk->code.size(), full.chunk->code.size());

    EXPECT_FALSE(full.diagnostics.hasErrors());
    EXPECT_FALSE(noFold.diagnostics.hasErrors());
    EXPECT_EQ(runChunk(noFold.chunk, noFold.diagnostics), runChunk(full.chunk, full.diagnostics));
}

TEST(OptimizerTest, PassMaskTurnsOffDeadCodeElimination) {
    const std::string source = "let x = 0; while (false) [ x = x + 1; ]; x";

    auto full = compileAndOptimize(source);
    auto noDeadCode = compileAndOptimize(source, static_cast<unsigned>(Optimizer::Pass::ConstantFold));

    EXPECT_GT(full.stats.removed, 0u);
    EXPECT_LT(full.chunk->code.size(), noDeadCode.chunk->code.size());

    EXPECT_FALSE(full.diagnostics.hasErrors());
    EXPECT_FALSE(noDeadCode.diagnostics.hasErrors());
    EXPECT_EQ(runChunk(noDeadCode.chunk, noDeadCode.diagnostics), runChunk(full.chunk, full.diagnostics));
}

TEST(OptimizerTest, PassMaskDefaultsToEveryPassEnabled) {
    Optimizer optimizer;

    EXPECT_EQ(optimizer.enabledPasses(), Optimizer::allPasses);

    optimizer.setEnabledPasses(static_cast<unsigned>(Optimizer::Pass::ConstantFold));
    EXPECT_EQ(optimizer.enabledPasses(), static_cast<unsigned>(Optimizer::Pass::ConstantFold));

    optimizer.setEnabledPasses(Optimizer::allPasses);
    EXPECT_EQ(optimizer.enabledPasses(), Optimizer::allPasses);
}

TEST(OptimizerTest, RemovesStoreOverwrittenBeforeAnyRead) {
    auto program = compileAndOptimize("let x = 1; x = 2; x");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_EQ(countOp(*program.chunk, Op::STORE_VAR) + countOp(*program.chunk, Op::STORE_SLOT), 1);

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "2");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, RemovesDeadSlotStoreInStraightLineCode) {
    ir::Chunk chunk;
    chunk.slotCount = 4;
    chunk.constants.push_back(1);
    chunk.constants.push_back(2);

    chunk.code = {
        { Op::LOAD_CONST, 0, 1 },
        { Op::STORE_SLOT, 0, -1, 1 },
        { Op::LOAD_CONST, 1, 2 },
        { Op::STORE_SLOT, 0, -1, 2 },
        { Op::LOAD_SLOT, 0, 3, -1 },
        { Op::HALT, 0 }
    };

    Optimizer optimizer;
    optimizer.optimize(chunk);

    EXPECT_EQ(countOpMir(chunk, Op::STORE_SLOT), 1);

    DiagnosticEngine diag;
    VM vm(diag);
    EXPECT_EQ(VM::valueToString(vm.run(std::make_shared<Chunk>(std::move(chunk)), {})), "2");
    EXPECT_FALSE(diag.hasErrors());
}

TEST(OptimizerTest, KeepsSlotStoreThatAReadObserves) {
    ir::Chunk chunk;
    chunk.slotCount = 2;

    chunk.code = {
        { Op::LOAD_SLOT, 1, 0, 1 },
        { Op::STORE_SLOT, 0, -1, 0 },
        { Op::LOAD_SLOT, 0, 1, 0 },
        { Op::LOAD_SLOT, 1, 2, 1 },
        { Op::STORE_SLOT, 0, -1, 1 },
        { Op::LOAD_SLOT, 0, 3, 0 },
        { Op::HALT, 0 }
    };

    Optimizer optimizer;
    optimizer.optimize(chunk);

    EXPECT_EQ(countOpMir(chunk, Op::STORE_SLOT), 2);
}

TEST(OptimizerTest, KeepsStoreThatPrecedesACall) {
    ir::Chunk chunk;
    chunk.slotCount = 1;
    chunk.constants.push_back(1);
    chunk.constants.push_back(2);
    chunk.constants.push_back(std::string("hook"));

    chunk.code = {
        { Op::LOAD_CONST, 0, 0 },
        { Op::STORE_SLOT, 0, -1, 0 },
        { Op::CALL, 2, 1, 2 },
        { Op::LOAD_CONST, 1, 1 },
        { Op::STORE_SLOT, 0, -1, 1 },
        { Op::LOAD_SLOT, 0, 2, 0 },
        { Op::HALT, 0 }
    };

    Optimizer optimizer;
    optimizer.optimize(chunk);

    EXPECT_EQ(countOpMir(chunk, Op::STORE_SLOT), 2);
}

TEST(OptimizerTest, PassMaskTurnsOffDeadStoreElimination) {
    const std::string source = "let x = 1; x = 2; x";

    auto full = compileAndOptimize(source);
    auto noDeadStore = compileAndOptimize(
        source,
        static_cast<unsigned>(Optimizer::Pass::ConstantFold) | static_cast<unsigned>(Optimizer::Pass::DeadCode));

    EXPECT_LT(
        countOp(*full.chunk, Op::STORE_VAR) + countOp(*full.chunk, Op::STORE_SLOT),
        countOp(*noDeadStore.chunk, Op::STORE_VAR) + countOp(*noDeadStore.chunk, Op::STORE_SLOT));

    EXPECT_FALSE(full.diagnostics.hasErrors());
    EXPECT_FALSE(noDeadStore.diagnostics.hasErrors());
    EXPECT_EQ(runChunk(noDeadStore.chunk, noDeadStore.diagnostics), runChunk(full.chunk, full.diagnostics));
}

TEST(OptimizerTest, RejectedFoldKeepsItsOperandsOnTheStack) {
    auto program = compileAndOptimize("10.0 % 3.0");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_EQ(countOp(*program.chunk, Op::MOD), 1);
    EXPECT_GE(countOp(*program.chunk, Op::LOAD_CONST), 2);

    VM vm(program.diagnostics);
    [[maybe_unused]] auto result = vm.run(program.chunk, {});

    EXPECT_TRUE(program.diagnostics.hasErrors());
    EXPECT_EQ(program.diagnostics.getErrorMessage().find("Stack underflow"), std::string::npos);
    EXPECT_NE(program.diagnostics.getErrorMessage().find("Modulo requires integral types"), std::string::npos);
}

TEST(OptimizerTest, EliminatesRepeatedSubexpression) {
    const std::string source =
        "func f(a: int, b: int) -> int { return (a + b) * (a + b) + (a + b); }\n"
        "f(2, 3)";

    auto withCse = compileAndOptimize(source);
    auto withoutCse = compileAndOptimize(
        source, Optimizer::allPasses & ~static_cast<unsigned>(Optimizer::Pass::CSE));

    EXPECT_FALSE(withCse.diagnostics.hasErrors());
    EXPECT_FALSE(withoutCse.diagnostics.hasErrors());

    EXPECT_LT(countOpEverywhere(*withCse.chunk, Op::ADD),
        countOpEverywhere(*withoutCse.chunk, Op::ADD));

    VM vm(withCse.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(withCse.chunk, {})), "30");
}

TEST(OptimizerTest, CSEStaysCorrectAcrossCalls) {
    const std::string source =
        "func g(x: int) -> int { return x + 1; }\n"
        "func f(a: int, b: int) -> int {\n"
        "  let s = (a + b) + g(a);\n"
        "  return (a + b) + g(a);\n"
        "}\n"
        "f(2, 3)";

    auto program = compileAndOptimize(source);

    EXPECT_FALSE(program.diagnostics.hasErrors());

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "8");
}

TEST(OptimizerTest, CSEInsideLoopKeepsJumpOffsets) {
    const std::string source =
        "func f(a: int, b: int, n: int) -> int {\n"
        "  let s = 0;\n"
        "  let i = 0;\n"
        "  while (i < n) [\n"
        "    s = s + (a + b) * (a + b) + (a + b);\n"
        "    i = i + 1;\n"
        "  ]\n"
        "  return s;\n"
        "}\n"
        "f(2, 3, 5)";

    auto program = compileAndOptimize(source);

    EXPECT_FALSE(program.diagnostics.hasErrors());

    VM vm(program.diagnostics);
    auto result = vm.run(program.chunk, {});

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_EQ(VM::valueToString(result), "150");
}

TEST(OptimizerTest, LICMHoistsInvariantAndMatchesBaseline) {
    const std::string source =
        "func f(a: int, b: int, n: int) -> int {\n"
        "  let s = 0;\n"
        "  let i = 0;\n"
        "  while (i < n) {\n"
        "    s = s + (a * b);\n"
        "    i = i + 1;\n"
        "  }\n"
        "  return s;\n"
        "}\n"
        "f(3, 4, 20000)";

    const unsigned licmOff =
        Optimizer::allPasses & ~static_cast<unsigned>(Optimizer::Pass::LICM);

    auto withLICM = compileAndOptimize(source, Optimizer::allPasses);
    auto withoutLICM = compileAndOptimize(source, licmOff);

    VM vmOn(withLICM.diagnostics);
    VM vmOff(withoutLICM.diagnostics);

    const std::string on = VM::valueToString(vmOn.run(withLICM.chunk, {}));
    const std::string off = VM::valueToString(vmOff.run(withoutLICM.chunk, {}));

    EXPECT_FALSE(withLICM.diagnostics.hasErrors());
    EXPECT_FALSE(withoutLICM.diagnostics.hasErrors());
    EXPECT_EQ(on, "240000");
    EXPECT_EQ(off, "240000");
    EXPECT_EQ(on, off);
}

TEST(OptimizerTest, LICMDoesNotHoistFromConditionalArm) {
    const std::string source =
        "let out = \"\"; "
        "for (n in [1, 2, 3]) [ "
        "    out += match (n) [ 2 [ \"two\" ] _ [ \"x\" ] ]; "
        "]; "
        "out";

    auto program = compileAndOptimize(source, Optimizer::allPasses);

    EXPECT_FALSE(program.diagnostics.hasErrors());

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "xtwox");
}

TEST(OptimizerTest, EliminatesDeadPureArithmeticValue) {
    const std::string source =
        "let s = 0;\n"
        "let i = 0;\n"
        "while (i < 5) [\n"
        "    i * 7 + 3;\n"
        "    s = s + i;\n"
        "    i = i + 1;\n"
        "]\n"
        "s";

    auto withDeadValue =
        compileAndOptimize(source, Optimizer::allPasses);
    auto withoutDeadValue = compileAndOptimize(
        source,
        Optimizer::allPasses & ~static_cast<unsigned>(Optimizer::Pass::DeadCode));

    EXPECT_FALSE(withDeadValue.diagnostics.hasErrors());
    EXPECT_FALSE(withoutDeadValue.diagnostics.hasErrors());

    EXPECT_EQ(countOpEverywhere(*withDeadValue.chunk, Op::MUL), 0);

    EXPECT_GT(countOpEverywhere(*withoutDeadValue.chunk, Op::MUL), 0);

    VM vm(withDeadValue.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(withDeadValue.chunk, {})), "10");
    EXPECT_EQ(
        runChunk(withoutDeadValue.chunk, withoutDeadValue.diagnostics),
        runChunk(withDeadValue.chunk, withDeadValue.diagnostics));
}

TEST(OptimizerTest, EliminatesDeadArithmeticValueInChunk) {
    ir::Chunk chunk;
    chunk.slotCount = 4;
    chunk.constants.push_back(7);
    chunk.constants.push_back(3);

    chunk.code = {
        { Op::LOAD_SLOT, 0, 1, 0 },
        { Op::LOAD_CONST, 0, 2 },
        { Op::MUL, 0, 3, 1, 2 },
        { Op::LOAD_CONST, 1, 2 },
        { Op::ADD, 0, 4, 3, 2 },
        { Op::LOAD_SLOT, 0, 0, 0 },
        { Op::HALT, 0 }
    };

    Optimizer optimizer;
    optimizer.optimize(chunk);

    EXPECT_FALSE(containsOpMir(chunk, Op::MUL));
    EXPECT_FALSE(containsOpMir(chunk, Op::ADD));
    EXPECT_TRUE(containsOpMir(chunk, Op::LOAD_SLOT));
}

TEST(OptimizerTest, KeepsLiveArithmeticValue) {
    ir::Chunk chunk;
    chunk.slotCount = 4;

    chunk.code = {
        { Op::LOAD_SLOT, 1, 1, 1 },
        { Op::LOAD_SLOT, 2, 2, 2 },
        { Op::MUL, 0, 0, 1, 2 },
        { Op::HALT, 0 }
    };

    Optimizer optimizer;
    optimizer.optimize(chunk);

    EXPECT_TRUE(containsOpMir(chunk, Op::MUL));
}

TEST(OptimizerTest, PropagatesConstantsAcrossJoinBlocks) {
    const auto makeChunk = [] {
        auto chunk = std::make_unique<Chunk>();
        chunk->slotCount = 4;
        chunk->constants.push_back(true);
        chunk->constants.push_back(std::string("flag"));
        chunk->constants.push_back(10);
        chunk->constants.push_back(20);

        chunk->code = {
            { Op::LOAD_SLOT, 0, 1 },
            { Op::JMP_IF_FALSE, 3, -1, 1 },
            { Op::LOAD_CONST, 2, 2 },
            { Op::STORE_VAR, 1, -1, 2 },
            { Op::JMP, 2 },
            { Op::LOAD_CONST, 2, 2 },
            { Op::STORE_VAR, 1, -1, 2 },
            { Op::LOAD_VAR, 1, 3 },
            { Op::MOVE, 0, 0, 3 },
            { Op::HALT, 0 },
        };

        return chunk;
    };

    auto withPropagation = makeChunk();
    Optimizer on;
    on.optimize(*withPropagation);

    EXPECT_FALSE(containsOpMir(*withPropagation, Op::LOAD_VAR));

    DiagnosticEngine diagnostics;
    VM vm(diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(std::shared_ptr<Chunk>(withPropagation.release()), {})), "10");
    EXPECT_FALSE(diagnostics.hasErrors());

    auto withoutPropagation = makeChunk();
    Optimizer off;
    off.setEnabledPasses(Optimizer::allPasses & ~static_cast<unsigned>(Optimizer::Pass::Propagation));
    off.optimize(*withoutPropagation);

    EXPECT_TRUE(containsOpMir(*withoutPropagation, Op::LOAD_VAR));
}

TEST(OptimizerTest, ReducesNumericIdentitiesWithUnknownOperands) {
    const std::string source =
        "func f(x: int) -> int {\n"
        "  let a = x + 0;\n"
        "  let b = a * 1;\n"
        "  return b;\n"
        "}\n"
        "f(7)";

    const unsigned mask = Optimizer::allPasses & ~static_cast<unsigned>(Optimizer::Pass::Inline);
    auto program = compileAndOptimize(source, mask);

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_EQ(countOpEverywhere(*program.chunk, Op::ADD), 0);
    EXPECT_EQ(countOpEverywhere(*program.chunk, Op::MUL), 0);

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "7");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, LICMKeepsHoistedSlotReadableAcrossCalls) {
    const std::string source =
        "func id(x: string) -> string { return x; }\n"
        "func f(prefix: string) -> string {\n"
        "  let out = \"\";\n"
        "  let i = 0;\n"
        "  while (i < 2) [ out = out + id(prefix); i = i + 1; ]\n"
        "  return out;\n"
        "}\n"
        "f(\"ab\")";

    auto program = compileAndOptimize(source, Optimizer::allPasses);

    EXPECT_FALSE(program.diagnostics.hasErrors());

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "abab");
}

TEST(OptimizerTest, LICMKeepsNestedForInElementsDistinct) {
    const std::string source =
        "let out = [];\n"
        "for (a in [[1, 2], [3]]) [ for (b in a) [ out.push(b); ] ]\n"
        "out.join(\",\")";

    auto program = compileAndOptimize(source, Optimizer::allPasses);

    EXPECT_FALSE(program.diagnostics.hasErrors());

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "1,2,3");
}

TEST(OptimizerTest, ConstantsAreInlinedWithoutGlobalStorage) {
    const std::string source =
        "const A = 7;\n"
        "const B: int = A * 2 + 1;\n"
        "let x = A + B;\n"
        "x";

    auto program = compileAndOptimize(source, Optimizer::allPasses);

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_TRUE(containsOp(*program.chunk, Op::LOAD_CONST));
    for (const auto& constant : program.chunk->constants) {
        if (!std::holds_alternative<std::string>(constant))
            continue;

        EXPECT_NE(std::get<std::string>(constant), "A");
        EXPECT_NE(std::get<std::string>(constant), "B");
    }

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "22");
}
