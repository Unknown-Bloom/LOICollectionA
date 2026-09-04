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
            out.chunk = std::make_shared<BytecodeChunk>(compiler.compile(*ast));

            Optimizer optimizer;
            optimizer.setEnabledPasses(mask);
            out.stats = optimizer.optimize(*out.chunk);

            return out;
        }

        OpCode canonicalOp(OpCode op) {
            switch (op) {
                case OpCode::ADD_I: return OpCode::ADD;
                case OpCode::SUB_I: return OpCode::SUB;
                case OpCode::MUL_I: return OpCode::MUL;
                case OpCode::MOD_I: return OpCode::MOD;
                case OpCode::CMP_EQ_I: return OpCode::CMP_EQ;
                case OpCode::CMP_NE_I: return OpCode::CMP_NE;
                case OpCode::CMP_GT_I: return OpCode::CMP_GT;
                case OpCode::CMP_LT_I: return OpCode::CMP_LT;
                case OpCode::CMP_GE_I: return OpCode::CMP_GE;
                case OpCode::CMP_LE_I: return OpCode::CMP_LE;
                case OpCode::NEG_I: return OpCode::NEG;
                case OpCode::ADD_SS: return OpCode::ADD;
                case OpCode::SUB_SS: return OpCode::SUB;
                case OpCode::MUL_SS: return OpCode::MUL;
                case OpCode::MOD_SS: return OpCode::MOD;
                case OpCode::CMP_EQ_SS: return OpCode::CMP_EQ;
                case OpCode::CMP_NE_SS: return OpCode::CMP_NE;
                case OpCode::CMP_GT_SS: return OpCode::CMP_GT;
                case OpCode::CMP_LT_SS: return OpCode::CMP_LT;
                case OpCode::CMP_GE_SS: return OpCode::CMP_GE;
                case OpCode::CMP_LE_SS: return OpCode::CMP_LE;
                default: return op;
            }
        }

        bool containsOp(const BytecodeChunk& chunk, OpCode op) {
            for (const auto& instr : chunk.code)
                if (canonicalOp(instr.op) == op)
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
    [[maybe_unused]] auto result = vm.run(std::make_shared<BytecodeChunk>(std::move(chunk)), {});
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

    EXPECT_FALSE(containsOp(*program.chunk, OpCode::LOAD_VAR));
    EXPECT_FALSE(containsOp(*program.chunk, OpCode::ADD));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "4");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, PropagatesStoredStringConstants) {
    auto program = compileAndOptimize("let s = \"ab\"; let t = s + \"c\"; t");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GE(program.stats.folded, 2u);
    EXPECT_FALSE(containsOp(*program.chunk, OpCode::LOAD_VAR));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "abc");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, PropagationTracksLatestAssignment) {
    auto program = compileAndOptimize("let x = 1; x = 2; x");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_FALSE(containsOp(*program.chunk, OpCode::LOAD_VAR));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "2");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, PropagationDropsSlotForNonScalarValues) {
    auto program = compileAndOptimize("let x = [1, 2]; x = [3, 4]; x");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_TRUE(containsOp(*program.chunk, OpCode::LOAD_VAR));

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
        if (containsOp(*body, OpCode::LOAD_VAR))
            bodyKeepsLoad = true;

    EXPECT_TRUE(bodyKeepsLoad);
}

TEST(OptimizerTest, PropagationInvalidatedByCalls) {
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

    namespace {
        std::size_t optimizeInvalidationProbe(OpCode op) {
            ir::BytecodeChunk chunk;
            chunk.constants.push_back(3);
            chunk.constants.push_back(std::string("x"));
            chunk.constants.push_back(std::string("hook"));

            chunk.code = {
                { OpCode::PUSH_INT, 0 },
                { OpCode::STORE_VAR, 1 },
                { op, 2 },
                { OpCode::LOAD_VAR, 1 },
                { OpCode::HALT, 0 }
            };

            Optimizer optimizer;
            optimizer.optimize(chunk);

            EXPECT_TRUE(containsOp(chunk, OpCode::LOAD_VAR));

            return chunk.code.size();
        }
}

TEST(OptimizerTest, PropagationInvalidatedByByNameMethodCalls) {
    EXPECT_EQ(optimizeInvalidationProbe(OpCode::CALL_METHOD_BY_NAME), 5u);
}

TEST(OptimizerTest, PropagationInvalidatedByNativeMethodCalls) {
    EXPECT_EQ(optimizeInvalidationProbe(OpCode::CALL_NATIVE_METHOD), 5u);
}

TEST(OptimizerTest, FusesNotIntoBranchOpcode) {
    auto program = compileAndOptimize("let i = 0; while (!(i >= 2)) [ i = i + 1; ]; i");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GE(program.stats.folded, 1u);

    EXPECT_FALSE(containsOp(*program.chunk, OpCode::NOT));
    EXPECT_TRUE(containsOp(*program.chunk, OpCode::JMP_IF_TRUE));
    EXPECT_FALSE(containsOp(*program.chunk, OpCode::JMP_IF_FALSE));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "2");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, FixedPointExposesPropagationAfterBranchRemoval) {
    auto program = compileAndOptimize("let x = 1; if (x == 1) [ let y = 10 : y = 20 ]; y");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GE(program.stats.folded, 3u);
    EXPECT_GE(program.stats.removed, 3u);

    EXPECT_FALSE(containsOp(*program.chunk, OpCode::LOAD_VAR));
    EXPECT_FALSE(containsOp(*program.chunk, OpCode::CMP_EQ));
    EXPECT_FALSE(containsOp(*program.chunk, OpCode::JMP));
    EXPECT_FALSE(containsOp(*program.chunk, OpCode::JMP_IF_FALSE));

    ASSERT_EQ(program.chunk->code.size(), 8u);
    EXPECT_EQ(program.chunk->code[0].op, OpCode::PUSH_INT);
    EXPECT_EQ(program.chunk->code[1].op, OpCode::DUP_STORE);
    EXPECT_EQ(program.chunk->code[2].op, OpCode::POP);
    EXPECT_EQ(program.chunk->code[3].op, OpCode::PUSH_INT);
    EXPECT_EQ(program.chunk->code[4].op, OpCode::DUP_STORE);
    EXPECT_EQ(program.chunk->code[5].op, OpCode::POP);
    EXPECT_EQ(program.chunk->code[6].op, OpCode::PUSH_INT);
    EXPECT_EQ(program.chunk->code[7].op, OpCode::HALT);

    EXPECT_EQ(std::get<int>(program.chunk->constants[program.chunk->code[3].operand]), 10);
    EXPECT_EQ(std::get<int>(program.chunk->constants[program.chunk->code[6].operand]), 10);

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "10");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, EliminatesAlgebraicIdentities) {
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
    EXPECT_EQ(VM::valueToString(vm.run(std::make_shared<BytecodeChunk>(std::move(chunk)), {})), "42");
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
    EXPECT_EQ(VM::valueToString(vm.run(std::make_shared<BytecodeChunk>(std::move(chunk)), {})), "2.5");
    EXPECT_FALSE(diag.hasErrors());
}

TEST(OptimizerTest, FloatAddZeroIsNotIdentityFolded) {
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
    EXPECT_EQ(VM::valueToString(vm.run(std::make_shared<BytecodeChunk>(std::move(chunk)), {})), "-0");
    EXPECT_FALSE(diag.hasErrors());
}

TEST(OptimizerTest, PropagatesSignedZeroCorrectly) {
    auto program = compileAndOptimize("let x = -0.0; x");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GE(program.stats.folded, 1u);
    EXPECT_FALSE(containsOp(*program.chunk, OpCode::NEG));
    EXPECT_FALSE(containsOp(*program.chunk, OpCode::LOAD_VAR));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "-0");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, FoldsFloatAddZeroWithCorrectSignedZero) {
    auto program = compileAndOptimize("let x = -0.0; let y = x + 0.0; y");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_FALSE(containsOp(*program.chunk, OpCode::ADD));
    EXPECT_FALSE(containsOp(*program.chunk, OpCode::LOAD_VAR));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "0");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

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
    EXPECT_EQ(VM::valueToString(vm.run(std::make_shared<BytecodeChunk>(std::move(chunk)), {})), "false");
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
    EXPECT_EQ(chunk.code[0].operand, 0);
    EXPECT_EQ(chunk.constants.size(), 2u);
    EXPECT_EQ(std::get<int>(chunk.constants[chunk.code[0].operand]), 5);

    DiagnosticEngine diag;
    VM vm(diag);
    EXPECT_EQ(VM::valueToString(vm.run(std::make_shared<BytecodeChunk>(std::move(chunk)), {})), "5");
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
        std::string runChunk(const std::shared_ptr<BytecodeChunk>& chunk, DiagnosticEngine& diag) {
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
    EXPECT_FALSE(containsOp(*full.chunk, OpCode::ADD));
    EXPECT_TRUE(containsOp(*noFold.chunk, OpCode::ADD));
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

    namespace {
        int countOp(const BytecodeChunk& chunk, OpCode op) {
            int total = 0;
            for (const auto& instr : chunk.code)
                if (canonicalOp(instr.op) == op)
                    ++total;

            return total;
        }
}

TEST(OptimizerTest, RemovesStoreOverwrittenBeforeAnyRead) {
    auto program = compileAndOptimize("let x = 1; x = 2; x");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_EQ(countOp(*program.chunk, OpCode::STORE_VAR) + countOp(*program.chunk, OpCode::DUP_STORE), 1);
    EXPECT_EQ(countOp(*program.chunk, OpCode::POP), 1);

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "2");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, RemovesDeadSlotStoreInStraightLineCode) {
    ir::BytecodeChunk chunk;
    chunk.slotCount = 1;
    chunk.constants.push_back(1);
    chunk.constants.push_back(2);

    chunk.code = {
        { OpCode::PUSH_INT, 0 },
        { OpCode::STORE_SLOT, 0 },
        { OpCode::PUSH_INT, 1 },
        { OpCode::STORE_SLOT, 0 },
        { OpCode::LOAD_SLOT, 0 },
        { OpCode::HALT, 0 }
    };

    Optimizer optimizer;
    optimizer.optimize(chunk);

    EXPECT_EQ(countOp(chunk, OpCode::STORE_SLOT), 1);
    EXPECT_EQ(countOp(chunk, OpCode::POP), 0);

    DiagnosticEngine diag;
    VM vm(diag);
    EXPECT_EQ(VM::valueToString(vm.run(std::make_shared<BytecodeChunk>(std::move(chunk)), {})), "2");
    EXPECT_FALSE(diag.hasErrors());
}

TEST(OptimizerTest, KeepsSlotStoreThatAReadObserves) {
    ir::BytecodeChunk chunk;
    chunk.slotCount = 2;

    chunk.code = {
        { OpCode::LOAD_SLOT, 1 },
        { OpCode::STORE_SLOT, 0 },
        { OpCode::LOAD_SLOT, 0 },
        { OpCode::POP, 0 },
        { OpCode::LOAD_SLOT, 1 },
        { OpCode::STORE_SLOT, 0 },
        { OpCode::LOAD_SLOT, 0 },
        { OpCode::HALT, 0 }
    };

    Optimizer optimizer;
    optimizer.optimize(chunk);

    EXPECT_EQ(countOp(chunk, OpCode::STORE_SLOT), 2);
}

TEST(OptimizerTest, KeepsStoreThatPrecedesACall) {
    ir::BytecodeChunk chunk;
    chunk.slotCount = 1;
    chunk.constants.push_back(1);
    chunk.constants.push_back(2);
    chunk.constants.push_back(std::string("hook"));

    chunk.code = {
        { OpCode::PUSH_INT, 0 },
        { OpCode::STORE_SLOT, 0 },
        { OpCode::CALL, 2 },
        { OpCode::PUSH_INT, 1 },
        { OpCode::STORE_SLOT, 0 },
        { OpCode::LOAD_SLOT, 0 },
        { OpCode::HALT, 0 }
    };

    Optimizer optimizer;
    optimizer.optimize(chunk);

    EXPECT_EQ(countOp(chunk, OpCode::STORE_SLOT), 2);
}

TEST(OptimizerTest, PassMaskTurnsOffDeadStoreElimination) {
    const std::string source = "let x = 1; x = 2; x";

    auto full = compileAndOptimize(source);
    auto noDeadStore = compileAndOptimize(
        source,
        static_cast<unsigned>(Optimizer::Pass::ConstantFold) | static_cast<unsigned>(Optimizer::Pass::DeadCode));

    EXPECT_LT(
        countOp(*full.chunk, OpCode::STORE_VAR) + countOp(*full.chunk, OpCode::DUP_STORE),
        countOp(*noDeadStore.chunk, OpCode::STORE_VAR) + countOp(*noDeadStore.chunk, OpCode::DUP_STORE));

    EXPECT_FALSE(full.diagnostics.hasErrors());
    EXPECT_FALSE(noDeadStore.diagnostics.hasErrors());
    EXPECT_EQ(runChunk(noDeadStore.chunk, noDeadStore.diagnostics), runChunk(full.chunk, full.diagnostics));
}

TEST(OptimizerTest, RejectedFoldKeepsItsOperandsOnTheStack) {
    auto program = compileAndOptimize("10.0 % 3.0");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_EQ(countOp(*program.chunk, OpCode::MOD), 1);
    EXPECT_GE(countOp(*program.chunk, OpCode::PUSH_FLOAT), 2);

    VM vm(program.diagnostics);
    [[maybe_unused]] auto result = vm.run(program.chunk, {});

    EXPECT_TRUE(program.diagnostics.hasErrors());
    EXPECT_EQ(program.diagnostics.getErrorMessage().find("Stack underflow"), std::string::npos);
    EXPECT_NE(program.diagnostics.getErrorMessage().find("Modulo requires integral types"), std::string::npos);
}

    namespace {
        int countOpEverywhere(const BytecodeChunk& chunk, OpCode op) {
            int total = countOp(chunk, op);
            for (const auto& body : chunk.methodBodies)
                total += countOp(*body, op);
            return total;
        }

        bool containsOpEverywhere(const BytecodeChunk& chunk, OpCode op) {
            if (containsOp(chunk, op))
                return true;
            for (const auto& body : chunk.methodBodies)
                if (containsOp(*body, op))
                    return true;
            return false;
        }
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

    EXPECT_LT(countOpEverywhere(*withCse.chunk, OpCode::ADD),
        countOpEverywhere(*withoutCse.chunk, OpCode::ADD));
    EXPECT_TRUE(containsOpEverywhere(*withCse.chunk, OpCode::DUP_STORE_SLOT));

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
    EXPECT_TRUE(containsOpEverywhere(*program.chunk, OpCode::DUP_STORE_SLOT));

    VM vm(program.diagnostics);
    auto result = vm.run(program.chunk, {});

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_EQ(VM::valueToString(result), "150");
}

TEST(OptimizerTest, FusesSlotArithmetic) {
    const std::string source =
        "func f(a: int, b: int) -> int { let t = a + b; return t * 2; }\n"
        "f(2, 3)";

    auto withFuse = compileAndOptimize(source);
    auto withoutFuse = compileAndOptimize(
        source, Optimizer::allPasses & ~static_cast<unsigned>(Optimizer::Pass::Fuse));

    EXPECT_FALSE(withFuse.diagnostics.hasErrors());
    EXPECT_FALSE(withoutFuse.diagnostics.hasErrors());

    auto hasRaw = [](const BytecodeChunk& chunk, OpCode op) {
        for (const auto& instr : chunk.code)
            if (instr.op == op)
                return true;
        for (const auto& body : chunk.methodBodies)
            for (const auto& instr : body->code)
                if (instr.op == op)
                    return true;
        return false;
    };

    EXPECT_TRUE(hasRaw(*withFuse.chunk, OpCode::ADD_SS));
    EXPECT_FALSE(hasRaw(*withoutFuse.chunk, OpCode::ADD_SS));

    VM vm(withFuse.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(withFuse.chunk, {})), "10");

    VM vmOff(withoutFuse.diagnostics);
    EXPECT_EQ(VM::valueToString(vmOff.run(withoutFuse.chunk, {})), "10");
}
