#include <gtest/gtest.h>

#include <string>

#include "LOICollectionA/frontend/Lexer.h"
#include "LOICollectionA/frontend/Parser.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/SemanticAnalyzer.h"

#include "LOICollectionA/frontend/ir/Compiler.h"
#include "LOICollectionA/frontend/ir/Optimizer.h"
#include "LOICollectionA/frontend/ir/VM.h"

using namespace LOICollection::frontend;
using namespace LOICollection::frontend::ir;

namespace {
    struct CompiledProgram {
        std::shared_ptr<BytecodeChunk> chunk;
        DiagnosticEngine diagnostics;
        Optimizer::Stats stats;
    };

    CompiledProgram compileAndOptimize(const std::string& source) {
        CompiledProgram out;

        Lexer lexer(source, out.diagnostics);
        Parser parser(lexer, out.diagnostics);

        auto ast = parser.parse();
        if (ast->getType() == ASTNode::Type::Program) {
            SemanticAnalyzer analyzer(out.diagnostics);
            analyzer.analyze(static_cast<ProgramNode&>(*ast));
        }

        Compiler compiler(out.diagnostics);
        out.chunk = std::make_shared<BytecodeChunk>(compiler.compile(*ast));

        Optimizer optimizer;
        out.stats = optimizer.optimize(*out.chunk);

        return out;
    }

    bool containsOp(const BytecodeChunk& chunk, OpCode op) {
        for (const auto& instr : chunk.code)
            if (instr.op == op)
                return true;

        return false;
    }
}

TEST(OptimizerTest, ConstantFolding) {
    auto program = compileAndOptimize("1 + 2 * 3");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GE(program.stats.folded, 2u);

    ASSERT_EQ(program.chunk->code.size(), 2u);
    EXPECT_EQ(program.chunk->code[0].op, OpCode::PUSH_INT);
    EXPECT_EQ(program.chunk->code[1].op, OpCode::HALT);
    EXPECT_EQ(std::get<int>(program.chunk->constants[program.chunk->code[0].operand]), 7);

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
    auto program = compileAndOptimize("a = 1; b = \"1\"; a == b");
    EXPECT_FALSE(program.diagnostics.hasErrors());

    VM vm(program.diagnostics);
    [[maybe_unused]] auto result = vm.run(program.chunk, {});
    EXPECT_TRUE(program.diagnostics.hasErrors());
    EXPECT_NE(program.diagnostics.getErrorMessage().find("(at line 1, col "), std::string::npos);
}

TEST(OptimizerTest, OptionalUnwrapErrorCarriesSourceLocation) {
    auto program = compileAndOptimize("b: optional<string> = None; b");
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

    ASSERT_EQ(program.chunk->code.size(), 2u);
    EXPECT_EQ(program.chunk->code[0].op, OpCode::PUSH_INT);
    EXPECT_EQ(program.chunk->code[1].op, OpCode::HALT);

    const auto& value = program.chunk->constants[program.chunk->code[0].operand];
    ASSERT_TRUE(std::holds_alternative<ArrayRef>(value));
    EXPECT_EQ(std::get<ArrayRef>(value)->elements.size(), 3u);

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "[1, 2, 3]");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, FoldsConstantArrayIndex) {
    auto program = compileAndOptimize("[1, 2, 3][1]");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GE(program.stats.folded, 2u);

    ASSERT_EQ(program.chunk->code.size(), 2u);
    EXPECT_EQ(program.chunk->code[0].op, OpCode::PUSH_INT);
    EXPECT_EQ(std::get<int>(program.chunk->constants[program.chunk->code[0].operand]), 2);

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "2");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, ConstantArrayIsClonedPerEvaluation) {
    auto program = compileAndOptimize(
        "func make() { return [1, 2]; } "
        "a = make(); "
        "b = make(); "
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
        "a = make(); "
        "b = make(); "
        "a[0][0] = 9; "
        "b[0][0]");

    EXPECT_FALSE(program.diagnostics.hasErrors());

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "1");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, FoldsTypeIntrospectionOpcodes) {
    ir::BytecodeChunk chunk;
    chunk.constants.push_back(42);
    chunk.constants.push_back(std::monostate{});

    chunk.code = {
        { OpCode::PUSH_INT, 0 },
        { OpCode::TYPE_OF, 0 },
        { OpCode::PUSH_NONE, 1 },
        { OpCode::HAS_VALUE, 0 },
        { OpCode::PUSH_INT, 0 },
        { OpCode::UNWRAP, 0 },
        { OpCode::HALT, 0 }
    };

    Optimizer optimizer;
    auto stats = optimizer.optimize(chunk);

    EXPECT_GE(stats.folded, 3u);

    ASSERT_EQ(chunk.code.size(), 4u);
    EXPECT_EQ(chunk.code[0].op, OpCode::PUSH_STR);
    EXPECT_EQ(chunk.code[1].op, OpCode::PUSH_BOOL);
    EXPECT_EQ(chunk.code[2].op, OpCode::PUSH_INT);
    EXPECT_EQ(chunk.code[3].op, OpCode::HALT);

    EXPECT_EQ(std::get<std::string>(chunk.constants[chunk.code[0].operand]), "int");
    EXPECT_EQ(std::get<bool>(chunk.constants[chunk.code[1].operand]), false);
    EXPECT_EQ(std::get<int>(chunk.constants[chunk.code[2].operand]), 42);
}

TEST(OptimizerTest, DoesNotFoldUnwrapOfEmptyOptional) {
    ir::BytecodeChunk chunk;
    chunk.constants.push_back(std::monostate{});

    chunk.code = {
        { OpCode::PUSH_NONE, 0 },
        { OpCode::UNWRAP, 0 },
        { OpCode::HALT, 0 }
    };

    Optimizer optimizer;
    auto stats = optimizer.optimize(chunk);

    EXPECT_EQ(stats.folded, 0u);
    ASSERT_EQ(chunk.code.size(), 3u);
    EXPECT_EQ(chunk.code[1].op, OpCode::UNWRAP);

    DiagnosticEngine diag;
    VM vm(diag);
    [[maybe_unused]] auto result = vm.run(std::make_shared<BytecodeChunk>(chunk), {});
    EXPECT_TRUE(diag.hasErrors());
    EXPECT_NE(diag.getErrorMessage().find("Optional value is empty"), std::string::npos);
}

TEST(OptimizerTest, WhileFalseBodyEliminated) {
    auto program = compileAndOptimize("x = 0; while (false) [ x = x + 1; ]; x");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GT(program.stats.removed, 0u);

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "0");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, ForFalseConditionBodyEliminated) {
    auto program = compileAndOptimize("x = 0; for (i = 0; false; i = i + 1) [ x = x + 1; ]; x");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GT(program.stats.removed, 0u);

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "0");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, WhileTrueWithBreakSurvivesOptimization) {
    auto program = compileAndOptimize("i = 0; while (true) [ i = i + 1; if (i == 3) [ break; ] ]; i");

    EXPECT_FALSE(program.diagnostics.hasErrors());

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "3");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, ForLoopSurvivesOptimization) {
    auto program = compileAndOptimize("s = 0; for (i = 0; i < 10; i = i + 1) [ s = s + i; ]; s");

    EXPECT_FALSE(program.diagnostics.hasErrors());

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "45");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, LoopWithContinueSurvivesOptimization) {
    auto program = compileAndOptimize(
        "s = 0; "
        "for (i = 0; i < 10; i = i + 1) [ "
        "    if (i % 2 == 0) [ continue; ]; "
        "    s = s + i; "
        "]; "
        "s");

    EXPECT_FALSE(program.diagnostics.hasErrors());

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "25");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

// ---- local constant propagation ------------------------------------------------

TEST(OptimizerTest, PropagatesStoredScalarIntoArithmetic) {
    auto program = compileAndOptimize("x = 4; x += 0; x");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GE(program.stats.folded, 2u);

    EXPECT_FALSE(containsOp(*program.chunk, OpCode::LOAD_VAR));
    EXPECT_FALSE(containsOp(*program.chunk, OpCode::ADD));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "4");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, PropagatesStoredStringConstants) {
    auto program = compileAndOptimize("s = \"ab\"; t = s + \"c\"; t");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GE(program.stats.folded, 2u);
    EXPECT_FALSE(containsOp(*program.chunk, OpCode::LOAD_VAR));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "abc");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, PropagationTracksLatestAssignment) {
    auto program = compileAndOptimize("x = 1; x = 2; x");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_FALSE(containsOp(*program.chunk, OpCode::LOAD_VAR));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "2");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, PropagationDropsSlotForNonScalarValues) {
    // Forwarding an array reference would alias the pool constant on every load,
    // so storing a non-scalar must retire the slot and keep the LOAD_VAR.
    auto program = compileAndOptimize("x = 5; x = [1, 2]; x");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_TRUE(containsOp(*program.chunk, OpCode::LOAD_VAR));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "[1, 2]");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, PropagationInvalidatedAtBranchMerge) {
    const std::string source =
        "func f(a: int) -> int { if (a == 1) [ r = 10 : r = 20 ]; return r; } ";

    auto taken = compileAndOptimize(source + "f(1)");
    EXPECT_FALSE(taken.diagnostics.hasErrors());

    VM vmTaken(taken.diagnostics);
    EXPECT_EQ(VM::valueToString(vmTaken.run(taken.chunk, {})), "10");
    EXPECT_FALSE(taken.diagnostics.hasErrors());

    // If the then-branch's r = 10 leaked past the merge point, the untaken
    // branch would wrongly observe 10.
    auto untaken = compileAndOptimize(source + "f(2)");
    EXPECT_FALSE(untaken.diagnostics.hasErrors());

    VM vmUntaken(untaken.diagnostics);
    EXPECT_EQ(VM::valueToString(vmUntaken.run(untaken.chunk, {})), "20");
    EXPECT_FALSE(untaken.diagnostics.hasErrors());

    bool bodyKeepsLoad = false;
    for (const auto& body : untaken.chunk->methodBodies)
        if (containsOp(body, OpCode::LOAD_VAR))
            bodyKeepsLoad = true;

    EXPECT_TRUE(bodyKeepsLoad);
}

TEST(OptimizerTest, PropagationInvalidatedByCalls) {
    // Calls can execute script code that rebinds the variable, so the load
    // after CALL must not be forwarded even though x was just stored.
    ir::BytecodeChunk chunk;
    chunk.constants.push_back(3);
    chunk.constants.push_back(std::string("x"));
    chunk.constants.push_back(std::string("hook"));

    chunk.code = {
        { OpCode::PUSH_INT, 0 },
        { OpCode::STORE_VAR, 1 },
        { OpCode::CALL, 2 },
        { OpCode::LOAD_VAR, 1 },
        { OpCode::HALT, 0 }
    };

    Optimizer optimizer;
    optimizer.optimize(chunk);

    EXPECT_TRUE(containsOp(chunk, OpCode::LOAD_VAR));
    EXPECT_EQ(chunk.code.size(), 5u);
}

// ---- NOT / branch fusion -------------------------------------------------------

TEST(OptimizerTest, FusesNotIntoBranchOpcode) {
    // The loop head is a merge point, so `i >= 2` stays dynamic: the leading
    // NOT must fuse into the branch instead of evaluating at runtime.
    auto program = compileAndOptimize("i = 0; while (!(i >= 2)) [ i = i + 1; ]; i");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GE(program.stats.folded, 1u);

    EXPECT_FALSE(containsOp(*program.chunk, OpCode::NOT));
    EXPECT_TRUE(containsOp(*program.chunk, OpCode::JMP_IF_TRUE));
    EXPECT_FALSE(containsOp(*program.chunk, OpCode::JMP_IF_FALSE));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "2");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

// ---- fixed-point iteration -----------------------------------------------------

TEST(OptimizerTest, FixedPointExposesPropagationAfterBranchRemoval) {
    // Pass one folds x == 1 and deletes the else branch; only once the jump
    // over it is gone does the final LOAD_VAR stop being a merge target and
    // become forwardable in the next pass.
    auto program = compileAndOptimize("x = 1; if (x == 1) [ y = 10 : y = 20 ]; y");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GE(program.stats.folded, 3u);
    EXPECT_GE(program.stats.removed, 3u);

    EXPECT_FALSE(containsOp(*program.chunk, OpCode::LOAD_VAR));
    EXPECT_FALSE(containsOp(*program.chunk, OpCode::CMP_EQ));
    EXPECT_FALSE(containsOp(*program.chunk, OpCode::JMP));
    EXPECT_FALSE(containsOp(*program.chunk, OpCode::JMP_IF_FALSE));

    ASSERT_EQ(program.chunk->code.size(), 10u);
    EXPECT_EQ(program.chunk->code[0].op, OpCode::PUSH_INT);
    EXPECT_EQ(program.chunk->code[1].op, OpCode::DUP);
    EXPECT_EQ(program.chunk->code[2].op, OpCode::STORE_VAR);
    EXPECT_EQ(program.chunk->code[3].op, OpCode::POP);
    EXPECT_EQ(program.chunk->code[4].op, OpCode::PUSH_INT);
    EXPECT_EQ(program.chunk->code[5].op, OpCode::DUP);
    EXPECT_EQ(program.chunk->code[6].op, OpCode::STORE_VAR);
    EXPECT_EQ(program.chunk->code[7].op, OpCode::POP);
    EXPECT_EQ(program.chunk->code[8].op, OpCode::PUSH_INT);
    EXPECT_EQ(program.chunk->code[9].op, OpCode::HALT);

    EXPECT_EQ(std::get<int>(program.chunk->constants[program.chunk->code[4].operand]), 10);
    EXPECT_EQ(std::get<int>(program.chunk->constants[program.chunk->code[8].operand]), 10);

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "10");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

// ---- algebraic identity elimination --------------------------------------------

TEST(OptimizerTest, EliminatesAlgebraicIdentities) {
    // The kept operand is duplicated, so it is known but not removable: the
    // identities 42 + 0 and 42 * 1 must drop their operand instead of folding.
    ir::BytecodeChunk chunk;
    chunk.constants.push_back(42);
    chunk.constants.push_back(0);
    chunk.constants.push_back(1);

    chunk.code = {
        { OpCode::PUSH_INT, 0 },
        { OpCode::DUP, 0 },
        { OpCode::PUSH_INT, 1 },
        { OpCode::ADD, 0 },
        { OpCode::PUSH_INT, 2 },
        { OpCode::MUL, 0 },
        { OpCode::POP, 0 },
        { OpCode::HALT, 0 }
    };

    Optimizer optimizer;
    auto stats = optimizer.optimize(chunk);

    EXPECT_EQ(stats.folded, 2u);

    ASSERT_EQ(chunk.code.size(), 4u);
    EXPECT_EQ(chunk.code[0].op, OpCode::PUSH_INT);
    EXPECT_EQ(chunk.code[1].op, OpCode::DUP);
    EXPECT_EQ(chunk.code[2].op, OpCode::POP);
    EXPECT_EQ(chunk.code[3].op, OpCode::HALT);

    DiagnosticEngine diag;
    VM vm(diag);
    EXPECT_EQ(VM::valueToString(vm.run(std::make_shared<BytecodeChunk>(chunk), {})), "42");
    EXPECT_FALSE(diag.hasErrors());
}

TEST(OptimizerTest, EliminatesFloatMulDivPowIdentities) {
    ir::BytecodeChunk chunk;
    chunk.constants.push_back(2.5f);
    chunk.constants.push_back(1);

    chunk.code = {
        { OpCode::PUSH_FLOAT, 0 },
        { OpCode::DUP, 0 },
        { OpCode::PUSH_INT, 1 },
        { OpCode::MUL, 0 },
        { OpCode::PUSH_INT, 1 },
        { OpCode::DIV, 0 },
        { OpCode::PUSH_INT, 1 },
        { OpCode::POW, 0 },
        { OpCode::POP, 0 },
        { OpCode::HALT, 0 }
    };

    Optimizer optimizer;
    auto stats = optimizer.optimize(chunk);

    EXPECT_EQ(stats.folded, 3u);

    ASSERT_EQ(chunk.code.size(), 4u);
    EXPECT_EQ(chunk.code[0].op, OpCode::PUSH_FLOAT);
    EXPECT_EQ(chunk.code[1].op, OpCode::DUP);
    EXPECT_EQ(chunk.code[2].op, OpCode::POP);
    EXPECT_EQ(chunk.code[3].op, OpCode::HALT);

    DiagnosticEngine diag;
    VM vm(diag);
    EXPECT_EQ(VM::valueToString(vm.run(std::make_shared<BytecodeChunk>(chunk), {})), "2.5");
    EXPECT_FALSE(diag.hasErrors());
}

TEST(OptimizerTest, FloatAddZeroIsNotIdentityFolded) {
    // IEEE754: -0.0 + 0.0 == +0.0, so dropping a float `+ 0.0` would flip the
    // sign of negative zero. The ADD has to run.
    ir::BytecodeChunk chunk;
    chunk.constants.push_back(-0.0f);
    chunk.constants.push_back(0.0f);

    chunk.code = {
        { OpCode::PUSH_FLOAT, 0 },
        { OpCode::DUP, 0 },
        { OpCode::PUSH_FLOAT, 1 },
        { OpCode::ADD, 0 },
        { OpCode::POP, 0 },
        { OpCode::HALT, 0 }
    };

    Optimizer optimizer;
    auto stats = optimizer.optimize(chunk);

    EXPECT_EQ(stats.folded, 0u);
    EXPECT_TRUE(containsOp(chunk, OpCode::ADD));

    DiagnosticEngine diag;
    VM vm(diag);
    EXPECT_EQ(VM::valueToString(vm.run(std::make_shared<BytecodeChunk>(chunk), {})), "-0");
    EXPECT_FALSE(diag.hasErrors());
}

// ---- signed-zero semantics -----------------------------------------------------

TEST(OptimizerTest, PropagatesSignedZeroCorrectly) {
    // Pool dedup must not conflate -0.0 with +0.0 even though they compare
    // equal: the forwarded load has to reproduce the stored negative zero.
    auto program = compileAndOptimize("x = -0.0; x");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GE(program.stats.folded, 1u);
    EXPECT_FALSE(containsOp(*program.chunk, OpCode::NEG));
    EXPECT_FALSE(containsOp(*program.chunk, OpCode::LOAD_VAR));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "-0");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, FoldsFloatAddZeroWithCorrectSignedZero) {
    // Full folding computes the IEEE754 result: -0.0 + 0.0 is +0.0.
    auto program = compileAndOptimize("x = -0.0; y = x + 0.0; y");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_FALSE(containsOp(*program.chunk, OpCode::ADD));
    EXPECT_FALSE(containsOp(*program.chunk, OpCode::LOAD_VAR));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "0");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

// ---- IS_NONE folding and constant-pool dedup -----------------------------------

TEST(OptimizerTest, FoldsIsNoneOpcode) {
    ir::BytecodeChunk chunk;
    chunk.constants.push_back(std::monostate{});
    chunk.constants.push_back(42);

    chunk.code = {
        { OpCode::PUSH_NONE, 0 },
        { OpCode::IS_NONE, 0 },
        { OpCode::PUSH_INT, 1 },
        { OpCode::IS_NONE, 0 },
        { OpCode::HALT, 0 }
    };

    Optimizer optimizer;
    auto stats = optimizer.optimize(chunk);

    EXPECT_EQ(stats.folded, 2u);

    ASSERT_EQ(chunk.code.size(), 3u);
    EXPECT_EQ(chunk.code[0].op, OpCode::PUSH_BOOL);
    EXPECT_EQ(chunk.code[1].op, OpCode::PUSH_BOOL);
    EXPECT_EQ(chunk.code[2].op, OpCode::HALT);
    EXPECT_EQ(std::get<bool>(chunk.constants[chunk.code[0].operand]), true);
    EXPECT_EQ(std::get<bool>(chunk.constants[chunk.code[1].operand]), false);

    DiagnosticEngine diag;
    VM vm(diag);
    EXPECT_EQ(VM::valueToString(vm.run(std::make_shared<BytecodeChunk>(chunk), {})), "false");
    EXPECT_FALSE(diag.hasErrors());
}

TEST(OptimizerTest, ReusesScalarConstantsWhenFolding) {
    ir::BytecodeChunk chunk;
    chunk.constants.push_back(5);
    chunk.constants.push_back(0);

    chunk.code = {
        { OpCode::PUSH_INT, 0 },
        { OpCode::PUSH_INT, 1 },
        { OpCode::ADD, 0 },
        { OpCode::HALT, 0 }
    };

    Optimizer optimizer;
    optimizer.optimize(chunk);

    ASSERT_EQ(chunk.code.size(), 2u);
    EXPECT_EQ(chunk.code[0].op, OpCode::PUSH_INT);
    EXPECT_EQ(chunk.code[1].op, OpCode::HALT);
    // the folded 5 reuses the existing pool slot instead of appending one
    EXPECT_EQ(chunk.code[0].operand, 0);
    EXPECT_EQ(chunk.constants.size(), 2u);
    EXPECT_EQ(std::get<int>(chunk.constants[chunk.code[0].operand]), 5);

    DiagnosticEngine diag;
    VM vm(diag);
    EXPECT_EQ(VM::valueToString(vm.run(std::make_shared<BytecodeChunk>(chunk), {})), "5");
    EXPECT_FALSE(diag.hasErrors());
}

// ---- coalesce keeps duplicated operands ----------------------------------------

TEST(OptimizerTest, CoalesceKeepsDuplicatedOperand) {
    // ?? duplicates the left operand before IS_NONE; the duplicate protects
    // the original push from being dropped while only the copy is consumed.
    auto noneCase = compileAndOptimize("x = None; x ?? \"d\"");

    EXPECT_FALSE(noneCase.diagnostics.hasErrors());

    VM vmNone(noneCase.diagnostics);
    EXPECT_EQ(VM::valueToString(vmNone.run(noneCase.chunk, {})), "d");
    EXPECT_FALSE(noneCase.diagnostics.hasErrors());

    auto valueCase = compileAndOptimize("x = \"v\"; x ?? \"d\"");

    EXPECT_FALSE(valueCase.diagnostics.hasErrors());

    VM vmValue(valueCase.diagnostics);
    EXPECT_EQ(VM::valueToString(vmValue.run(valueCase.chunk, {})), "v");
    EXPECT_FALSE(valueCase.diagnostics.hasErrors());
}
