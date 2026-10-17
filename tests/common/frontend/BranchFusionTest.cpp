#include <gtest/gtest.h>

#include <string>

#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/ir/Mir.h"

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;
using namespace LOICollection::frontend::ir;

namespace branch_fusion_test_support {
    template <class Visitor>
    void forEachChunk(const MirChunk& chunk, Visitor&& visit) {
        visit(chunk);

        for (const auto& body : chunk.methodBodies)
            if (body)
                forEachChunk(*body, visit);
    }

    bool hasOp(const MirChunk& chunk, MirOp op) {
        bool found = false;
        forEachChunk(chunk, [&found, op](const MirChunk& current) {
            for (const auto& instr : current.code)
                if (instr.op == op)
                    found = true;
        });

        return found;
    }
}

TEST(BranchFusionTest, FusesLoopConditions) {
    DiagnosticEngine diagnostics;
    auto chunk = compile(
        "func count(n: int) -> int {\n"
        "    let i = 0;\n"
        "    while (i < n) [ i = i + 1; ]\n"
        "    return i;\n"
        "}\n"
        "count(3)\n",
        diagnostics
    );
    ASSERT_TRUE(chunk) << diagnostics.getErrorMessage();

    EXPECT_TRUE(branch_fusion_test_support::hasOp(*chunk, MirOp::JMP_IF_CMP_FALSE));
    EXPECT_FALSE(branch_fusion_test_support::hasOp(*chunk, MirOp::CMP_LT));
}

TEST(BranchFusionTest, KeepsComparisonsThatAreUsedAsValues) {
    DiagnosticEngine diagnostics;
    auto chunk = compile(
        "func pick(a: int, b: int) -> int {\n"
        "    let lower = a < b;\n"
        "    if (lower) [ return 1; ]\n"
        "    return 0;\n"
        "}\n"
        "pick(1, 2)\n",
        diagnostics
    );
    ASSERT_TRUE(chunk) << diagnostics.getErrorMessage();

    EXPECT_TRUE(branch_fusion_test_support::hasOp(*chunk, MirOp::CMP_LT));
    EXPECT_FALSE(branch_fusion_test_support::hasOp(*chunk, MirOp::JMP_IF_CMP_FALSE));
}

TEST(BranchFusionTest, KeepsBehavior) {
    EXPECT_EQ(eval(R"(
        func classify(n: int) -> string {
            if (n < 0) [ return "negative"; ]
            if (n == 0) [ return "zero"; ]
            return "positive";
        }
        let out = "";
        let i = -2;
        while (i < 3) [ out = out + classify(i) + ","; i = i + 1; ]
        out
    )"), "negative,negative,zero,positive,positive,");
}
