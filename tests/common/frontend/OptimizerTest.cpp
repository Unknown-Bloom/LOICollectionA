#include <gtest/gtest.h>

#include <string>

#include "LOICollectionA/frontend/Lexer.h"
#include "LOICollectionA/frontend/Parser.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/SemanticAnalyzer.h"

#include "LOICollectionA/frontend/ir/Compiler.h"
#include "LOICollectionA/frontend/ir/Optimizer.h"
#include "LOICollectionA/frontend/ir/VM.h"
#include "LOICollectionA/frontend/ir/Mir.h"

using namespace LOICollection::frontend;
using namespace LOICollection::frontend::ir;

namespace {
    // The bytecode layer was removed; the VM now executes the three-address MIR
    // directly, so the optimizer works on `MirChunk` and the helpers below match
    // `MirOp` (which still carries integer-monomorphic variants such as ADD_I).
    struct CompiledProgram {
        std::shared_ptr<MirChunk> chunk;
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
        auto mir = std::make_shared<MirChunk>(compiler.compile(*ast));

        Optimizer optimizer;
        optimizer.setEnabledPasses(mask);
        out.stats = optimizer.optimize(*mir);

        out.chunk = mir;
        return out;
    }

    // MirOp carries no _SS variants, but integer-monomorphic opcodes (ADD_I, ...)
    // still exist. Match a generic opcode against both its generic and _I form,
    // mirroring the old `canonicalOp` normalization.
    MirOp intVariant(MirOp op) {
        switch (op) {
            case MirOp::ADD: return MirOp::ADD_I;
            case MirOp::SUB: return MirOp::SUB_I;
            case MirOp::MUL: return MirOp::MUL_I;
            case MirOp::MOD: return MirOp::MOD_I;
            case MirOp::CMP_EQ: return MirOp::CMP_EQ_I;
            case MirOp::CMP_NE: return MirOp::CMP_NE_I;
            case MirOp::CMP_GT: return MirOp::CMP_GT_I;
            case MirOp::CMP_LT: return MirOp::CMP_LT_I;
            case MirOp::CMP_GE: return MirOp::CMP_GE_I;
            case MirOp::CMP_LE: return MirOp::CMP_LE_I;
            case MirOp::NEG: return MirOp::NEG_I;
            default: return op;
        }
    }

    bool containsOp(const MirChunk& chunk, MirOp op) {
        const MirOp iv = intVariant(op);
        for (const auto& instr : chunk.code)
            if (instr.op == op || (iv != op && instr.op == iv))
                return true;
        return false;
    }

    int countOp(const MirChunk& chunk, MirOp op) {
        const MirOp iv = intVariant(op);
        int total = 0;
        for (const auto& instr : chunk.code)
            if (instr.op == op || (iv != op && instr.op == iv))
                ++total;
        return total;
    }

    bool containsOpEverywhere(const MirChunk& chunk, MirOp op) {
        if (containsOp(chunk, op))
            return true;
        for (const auto& body : chunk.methodBodies)
            if (containsOp(*body, op))
                return true;
        return false;
    }

    int countOpEverywhere(const MirChunk& chunk, MirOp op) {
        int total = countOp(chunk, op);
        for (const auto& body : chunk.methodBodies)
            total += countOp(*body, op);
        return total;
    }

    // Exact (no variant) helpers, used for hand-constructed chunks where the
    // precise opcode is known.
    bool containsOpMir(const MirChunk& chunk, MirOp op) {
        for (const auto& instr : chunk.code)
            if (instr.op == op)
                return true;
        return false;
    }

    int countOpMir(const MirChunk& chunk, MirOp op) {
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

    EXPECT_FALSE(containsOp(*program.chunk, MirOp::ADD));
    EXPECT_FALSE(containsOp(*program.chunk, MirOp::MUL));

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

    EXPECT_FALSE(containsOp(*program.chunk, MirOp::MAKE_ARRAY));

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
    ir::MirChunk chunk;
    chunk.slotCount = 6;
    chunk.constants.push_back(42);
    chunk.constants.push_back(std::monostate{});

    chunk.code = {
        { MirOp::LOAD_CONST, 0, 0 },
        { MirOp::TYPE_OF, 0, 1, 0 },
        { MirOp::LOAD_CONST, 1, 2 },
        { MirOp::HAS_VALUE, 0, 3, 2 },
        { MirOp::LOAD_CONST, 0, 4 },
        { MirOp::UNWRAP, 0, 5, 4 },
        { MirOp::HALT, 0 }
    };

    Optimizer optimizer;
    auto stats = optimizer.optimize(chunk);

    EXPECT_GE(stats.folded, 3u);

    EXPECT_FALSE(containsOpMir(chunk, MirOp::TYPE_OF));
    EXPECT_FALSE(containsOpMir(chunk, MirOp::HAS_VALUE));
    EXPECT_FALSE(containsOpMir(chunk, MirOp::UNWRAP));

    DiagnosticEngine diag;
    VM vm(diag);
    EXPECT_EQ(VM::valueToString(vm.run(std::make_shared<MirChunk>(chunk), {})), "42");
    EXPECT_FALSE(diag.hasErrors());
}

TEST(OptimizerTest, DoesNotFoldUnwrapOfEmptyOptional) {
    ir::MirChunk chunk;
    chunk.slotCount = 2;
    chunk.constants.push_back(std::monostate{});

    chunk.code = {
        { MirOp::LOAD_CONST, 0, 0 },
        { MirOp::UNWRAP, 0, 1, 0 },
        { MirOp::HALT, 0 }
    };

    Optimizer optimizer;
    auto stats = optimizer.optimize(chunk);

    EXPECT_EQ(stats.folded, 0u);
    EXPECT_TRUE(containsOpMir(chunk, MirOp::UNWRAP));

    DiagnosticEngine diag;
    VM vm(diag);
    [[maybe_unused]] auto result = vm.run(std::make_shared<MirChunk>(chunk), {});
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

    EXPECT_FALSE(containsOp(*program.chunk, MirOp::LOAD_VAR));
    EXPECT_FALSE(containsOp(*program.chunk, MirOp::ADD));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "4");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, PropagatesStoredStringConstants) {
    auto program = compileAndOptimize("let s = \"ab\"; let t = s + \"c\"; t");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GE(program.stats.folded, 2u);
    EXPECT_FALSE(containsOp(*program.chunk, MirOp::LOAD_VAR));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "abc");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, PropagationTracksLatestAssignment) {
    auto program = compileAndOptimize("let x = 1; x = 2; x");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_FALSE(containsOp(*program.chunk, MirOp::LOAD_VAR));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "2");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, PropagationDropsSlotForNonScalarValues) {
    auto program = compileAndOptimize("let x = [1, 2]; x = [3, 4]; x");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_TRUE(containsOp(*program.chunk, MirOp::LOAD_VAR));

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
        if (containsOp(*body, MirOp::LOAD_VAR))
            bodyKeepsLoad = true;

    EXPECT_TRUE(bodyKeepsLoad);
}

TEST(OptimizerTest, PropagationInvalidatedByCalls) {
    ir::MirChunk chunk;
    chunk.slotCount = 3;
    chunk.constants.push_back(3);
    chunk.constants.push_back(std::string("x"));
    chunk.constants.push_back(std::string("hook"));

    chunk.code = {
        { MirOp::LOAD_CONST, 0, 0 },
        { MirOp::STORE_VAR, 1, 1, 0 },
        { MirOp::CALL, 2, 2, 2 },
        { MirOp::LOAD_VAR, 1, 3, 1 },
        { MirOp::HALT, 0 }
    };

    Optimizer optimizer;
    optimizer.optimize(chunk);

    EXPECT_TRUE(containsOpMir(chunk, MirOp::LOAD_VAR));
    EXPECT_EQ(chunk.code.size(), 5u);
}

namespace {
    std::size_t optimizeInvalidationProbe(MirOp op) {
        ir::MirChunk chunk;
        chunk.slotCount = 3;
        chunk.constants.push_back(3);
        chunk.constants.push_back(std::string("x"));
        chunk.constants.push_back(std::string("hook"));

        chunk.code = {
            { MirOp::LOAD_CONST, 0, 0 },
            { MirOp::STORE_VAR, 1, 1, 0 },
            { op, 2, 2, 2 },
            { MirOp::LOAD_VAR, 1, 3, 1 },
            { MirOp::HALT, 0 }
        };

        Optimizer optimizer;
        optimizer.optimize(chunk);

        EXPECT_TRUE(containsOpMir(chunk, MirOp::LOAD_VAR));

        return chunk.code.size();
    }
}

TEST(OptimizerTest, PropagationInvalidatedByByNameMethodCalls) {
    EXPECT_EQ(optimizeInvalidationProbe(MirOp::CALL_METHOD_BY_NAME), 5u);
}

TEST(OptimizerTest, PropagationInvalidatedByNativeMethodCalls) {
    EXPECT_EQ(optimizeInvalidationProbe(MirOp::CALL_NATIVE_METHOD), 5u);
}

TEST(OptimizerTest, NegatedWhileConditionPreservesNot) {
    // FusePass (which fused `NOT` into the branch opcode) was removed together
    // with the bytecode layer; the VM still executes `NOT` + `JMP_IF_FALSE`
    // correctly, so a negated loop condition must keep producing the right
    // result and the `NOT` opcode must survive optimization.
    auto program = compileAndOptimize("let i = 0; while (!(i >= 2)) [ i = i + 1; ]; i");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_TRUE(containsOp(*program.chunk, MirOp::NOT));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "2");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, FixedPointExposesPropagationAfterBranchRemoval) {
    auto program = compileAndOptimize("let x = 1; if (x == 1) [ let y = 10 : y = 20 ]; y");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GE(program.stats.folded, 3u);
    EXPECT_GE(program.stats.removed, 3u);

    EXPECT_FALSE(containsOp(*program.chunk, MirOp::LOAD_VAR));
    EXPECT_FALSE(containsOp(*program.chunk, MirOp::CMP_EQ));
    EXPECT_FALSE(containsOp(*program.chunk, MirOp::JMP));
    EXPECT_FALSE(containsOp(*program.chunk, MirOp::JMP_IF_FALSE));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "10");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, EliminatesAlgebraicIdentities) {
    ir::MirChunk chunk;
    chunk.slotCount = 5;
    chunk.constants.push_back(42);
    chunk.constants.push_back(0);
    chunk.constants.push_back(1);

    chunk.code = {
        { MirOp::LOAD_CONST, 0, 0 },
        { MirOp::LOAD_CONST, 1, 1 },
        { MirOp::ADD, 0, 2, 0, 1 },
        { MirOp::LOAD_CONST, 2, 3 },
        { MirOp::MUL, 0, 4, 2, 3 },
        { MirOp::HALT, 0 }
    };

    Optimizer optimizer;
    auto stats = optimizer.optimize(chunk);

    EXPECT_EQ(stats.folded, 2u);
    EXPECT_FALSE(containsOpMir(chunk, MirOp::ADD));

    DiagnosticEngine diag;
    VM vm(diag);
    EXPECT_EQ(VM::valueToString(vm.run(std::make_shared<MirChunk>(chunk), {})), "42");
    EXPECT_FALSE(diag.hasErrors());
}

TEST(OptimizerTest, EliminatesFloatMulDivPowIdentities) {
    ir::MirChunk chunk;
    chunk.slotCount = 7;
    chunk.constants.push_back(2.5f);
    chunk.constants.push_back(1);
    chunk.constants.push_back(1);
    chunk.constants.push_back(1);

    chunk.code = {
        { MirOp::LOAD_CONST, 0, 0 },
        { MirOp::LOAD_CONST, 1, 1 },
        { MirOp::MUL, 0, 2, 0, 1 },
        { MirOp::LOAD_CONST, 2, 3 },
        { MirOp::DIV, 0, 4, 2, 3 },
        { MirOp::LOAD_CONST, 3, 5 },
        { MirOp::POW, 0, 6, 4, 5 },
        { MirOp::HALT, 0 }
    };

    Optimizer optimizer;
    auto stats = optimizer.optimize(chunk);

    EXPECT_EQ(stats.folded, 3u);

    DiagnosticEngine diag;
    VM vm(diag);
    EXPECT_EQ(VM::valueToString(vm.run(std::make_shared<MirChunk>(chunk), {})), "2.5");
    EXPECT_FALSE(diag.hasErrors());
}

TEST(OptimizerTest, FloatAddZeroIsNotIdentityFolded) {
    ir::MirChunk chunk;
    chunk.slotCount = 3;
    chunk.constants.push_back(-0.0f);
    chunk.constants.push_back(0.0f);

    chunk.code = {
        { MirOp::LOAD_CONST, 0, 0 },
        { MirOp::LOAD_CONST, 1, 1 },
        { MirOp::ADD, 0, 2, 0, 1 },
        { MirOp::HALT, 0 }
    };

    Optimizer optimizer;
    auto stats = optimizer.optimize(chunk);

    EXPECT_GE(stats.folded, 1u);
    EXPECT_FALSE(containsOpMir(chunk, MirOp::ADD));

    DiagnosticEngine diag;
    VM vm(diag);
    EXPECT_EQ(VM::valueToString(vm.run(std::make_shared<MirChunk>(chunk), {})), "-0");
    EXPECT_FALSE(diag.hasErrors());
}

TEST(OptimizerTest, PropagatesSignedZeroCorrectly) {
    auto program = compileAndOptimize("let x = -0.0; x");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_GE(program.stats.folded, 1u);
    EXPECT_FALSE(containsOp(*program.chunk, MirOp::NEG));
    EXPECT_FALSE(containsOp(*program.chunk, MirOp::LOAD_VAR));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "-0");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, FoldsFloatAddZeroWithCorrectSignedZero) {
    auto program = compileAndOptimize("let x = -0.0; let y = x + 0.0; y");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_FALSE(containsOp(*program.chunk, MirOp::ADD));
    EXPECT_FALSE(containsOp(*program.chunk, MirOp::LOAD_VAR));

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "0");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, FoldsIsNoneOpcode) {
    ir::MirChunk chunk;
    chunk.slotCount = 4;
    chunk.constants.push_back(std::monostate{});
    chunk.constants.push_back(42);

    chunk.code = {
        { MirOp::LOAD_CONST, 0, 0 },
        { MirOp::IS_NONE, 0, 1, 0 },
        { MirOp::LOAD_CONST, 1, 2 },
        { MirOp::IS_NONE, 0, 3, 2 },
        { MirOp::HALT, 0 }
    };

    Optimizer optimizer;
    auto stats = optimizer.optimize(chunk);

    EXPECT_EQ(stats.folded, 2u);
    EXPECT_FALSE(containsOpMir(chunk, MirOp::IS_NONE));
}

TEST(OptimizerTest, ReusesScalarConstantsWhenFolding) {
    ir::MirChunk chunk;
    chunk.slotCount = 3;
    chunk.constants.push_back(5);
    chunk.constants.push_back(0);

    chunk.code = {
        { MirOp::LOAD_CONST, 0, 0 },
        { MirOp::LOAD_CONST, 1, 1 },
        { MirOp::ADD, 0, 2, 0, 1 },
        { MirOp::HALT, 0 }
    };

    Optimizer optimizer;
    optimizer.optimize(chunk);

    EXPECT_FALSE(containsOpMir(chunk, MirOp::ADD));

    DiagnosticEngine diag;
    VM vm(diag);
    EXPECT_EQ(VM::valueToString(vm.run(std::make_shared<MirChunk>(chunk), {})), "5");
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
    std::string runChunk(const std::shared_ptr<MirChunk>& chunk, DiagnosticEngine& diag) {
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
    EXPECT_FALSE(containsOp(*full.chunk, MirOp::ADD));
    EXPECT_TRUE(containsOp(*noFold.chunk, MirOp::ADD));
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
    EXPECT_EQ(countOp(*program.chunk, MirOp::STORE_VAR) + countOp(*program.chunk, MirOp::STORE_SLOT), 1);

    VM vm(program.diagnostics);
    EXPECT_EQ(VM::valueToString(vm.run(program.chunk, {})), "2");
    EXPECT_FALSE(program.diagnostics.hasErrors());
}

TEST(OptimizerTest, RemovesDeadSlotStoreInStraightLineCode) {
    ir::MirChunk chunk;
    chunk.slotCount = 1;
    chunk.constants.push_back(1);
    chunk.constants.push_back(2);

    chunk.code = {
        { MirOp::LOAD_CONST, 0, 0 },
        { MirOp::STORE_SLOT, 0, -1, 0 },
        { MirOp::LOAD_CONST, 1, 1 },
        { MirOp::STORE_SLOT, 0, -1, 1 },
        { MirOp::LOAD_SLOT, 0, 2, 0 },
        { MirOp::HALT, 0 }
    };

    Optimizer optimizer;
    optimizer.optimize(chunk);

    EXPECT_EQ(countOpMir(chunk, MirOp::STORE_SLOT), 1);

    DiagnosticEngine diag;
    VM vm(diag);
    EXPECT_EQ(VM::valueToString(vm.run(std::make_shared<MirChunk>(chunk), {})), "2");
    EXPECT_FALSE(diag.hasErrors());
}

TEST(OptimizerTest, KeepsSlotStoreThatAReadObserves) {
    ir::MirChunk chunk;
    chunk.slotCount = 2;

    chunk.code = {
        { MirOp::LOAD_SLOT, 1, 0, 1 },
        { MirOp::STORE_SLOT, 0, -1, 0 },
        { MirOp::LOAD_SLOT, 0, 1, 0 },
        { MirOp::LOAD_SLOT, 1, 2, 1 },
        { MirOp::STORE_SLOT, 0, -1, 1 },
        { MirOp::LOAD_SLOT, 0, 3, 0 },
        { MirOp::HALT, 0 }
    };

    Optimizer optimizer;
    optimizer.optimize(chunk);

    EXPECT_EQ(countOpMir(chunk, MirOp::STORE_SLOT), 2);
}

TEST(OptimizerTest, KeepsStoreThatPrecedesACall) {
    ir::MirChunk chunk;
    chunk.slotCount = 1;
    chunk.constants.push_back(1);
    chunk.constants.push_back(2);
    chunk.constants.push_back(std::string("hook"));

    chunk.code = {
        { MirOp::LOAD_CONST, 0, 0 },
        { MirOp::STORE_SLOT, 0, -1, 0 },
        { MirOp::CALL, 2, 1, 2 },
        { MirOp::LOAD_CONST, 1, 1 },
        { MirOp::STORE_SLOT, 0, -1, 1 },
        { MirOp::LOAD_SLOT, 0, 2, 0 },
        { MirOp::HALT, 0 }
    };

    Optimizer optimizer;
    optimizer.optimize(chunk);

    EXPECT_EQ(countOpMir(chunk, MirOp::STORE_SLOT), 2);
}

TEST(OptimizerTest, PassMaskTurnsOffDeadStoreElimination) {
    const std::string source = "let x = 1; x = 2; x";

    auto full = compileAndOptimize(source);
    auto noDeadStore = compileAndOptimize(
        source,
        static_cast<unsigned>(Optimizer::Pass::ConstantFold) | static_cast<unsigned>(Optimizer::Pass::DeadCode));

    EXPECT_LT(
        countOp(*full.chunk, MirOp::STORE_VAR) + countOp(*full.chunk, MirOp::STORE_SLOT),
        countOp(*noDeadStore.chunk, MirOp::STORE_VAR) + countOp(*noDeadStore.chunk, MirOp::STORE_SLOT));

    EXPECT_FALSE(full.diagnostics.hasErrors());
    EXPECT_FALSE(noDeadStore.diagnostics.hasErrors());
    EXPECT_EQ(runChunk(noDeadStore.chunk, noDeadStore.diagnostics), runChunk(full.chunk, full.diagnostics));
}

TEST(OptimizerTest, RejectedFoldKeepsItsOperandsOnTheStack) {
    auto program = compileAndOptimize("10.0 % 3.0");

    EXPECT_FALSE(program.diagnostics.hasErrors());
    EXPECT_EQ(countOp(*program.chunk, MirOp::MOD), 1);
    EXPECT_GE(countOp(*program.chunk, MirOp::LOAD_CONST), 2);

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

    EXPECT_LT(countOpEverywhere(*withCse.chunk, MirOp::ADD),
        countOpEverywhere(*withoutCse.chunk, MirOp::ADD));

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
