#include <gtest/gtest.h>

#include <string>

#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/ir/Mir.h"

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;
using namespace LOICollection::frontend::ir;

namespace ref_store_analysis_test_support {
    const MirInstr* findStore(const MirChunk& chunk, MirOp op) {
        for (const auto& instr : chunk.code)
            if (instr.op == op)
                return &instr;

        return nullptr;
    }

    bool scalarFlag(const MirInstr& instr) {
        return (instr.imm & static_cast<int>(MirFlag::ScalarStore)) != 0;
    }
}

TEST(RefStoreAnalysisTest, MarksFieldStoresOfScalarValues) {
    DiagnosticEngine diagnostics;
    auto chunk = compile(
        "class Point {\n"
        "public:\n"
        "    x = 0;\n"
        "}\n"
        "let p = new Point();\n"
        "p.x = 41;\n",
        diagnostics
    );
    ASSERT_TRUE(chunk) << diagnostics.getErrorMessage();

    const MirInstr* store = ref_store_analysis_test_support::findStore(*chunk, MirOp::STORE_FIELD_SLOT);
    if (!store)
        store = ref_store_analysis_test_support::findStore(*chunk, MirOp::STORE_FIELD);

    ASSERT_NE(store, nullptr);
    EXPECT_TRUE(ref_store_analysis_test_support::scalarFlag(*store));
    EXPECT_FALSE(chunk->hasRefStores);
}

TEST(RefStoreAnalysisTest, LeavesReferenceStoresUnmarked) {
    DiagnosticEngine diagnostics;
    auto chunk = compile(
        "class Point {\n"
        "public:\n"
        "    x = 0;\n"
        "}\n"
        "let p = new Point();\n"
        "let q = new Point();\n"
        "p.x = q;\n",
        diagnostics
    );
    ASSERT_TRUE(chunk) << diagnostics.getErrorMessage();

    const MirInstr* store = ref_store_analysis_test_support::findStore(*chunk, MirOp::STORE_FIELD_SLOT);
    if (!store)
        store = ref_store_analysis_test_support::findStore(*chunk, MirOp::STORE_FIELD);

    ASSERT_NE(store, nullptr);
    EXPECT_FALSE(ref_store_analysis_test_support::scalarFlag(*store));
    EXPECT_TRUE(chunk->hasRefStores);
}

TEST(RefStoreAnalysisTest, MarksIndexStoresOfScalarValues) {
    DiagnosticEngine diagnostics;
    auto chunk = compile(
        "let values = [0];\n"
        "values[0] = 7;\n",
        diagnostics
    );
    ASSERT_TRUE(chunk) << diagnostics.getErrorMessage();

    const MirInstr* store = ref_store_analysis_test_support::findStore(*chunk, MirOp::STORE_INDEX);
    ASSERT_NE(store, nullptr);
    EXPECT_TRUE(ref_store_analysis_test_support::scalarFlag(*store));
}

TEST(RefStoreAnalysisTest, DetectsClosureCaptures) {
    DiagnosticEngine diagnostics;
    auto chunk = compile(
        "let base = 1;\n"
        "let add = func(x: int) -> int { return x + base; };\n"
        "add(2)\n",
        diagnostics
    );
    ASSERT_TRUE(chunk) << diagnostics.getErrorMessage();
    EXPECT_TRUE(chunk->hasRefStores);
}
