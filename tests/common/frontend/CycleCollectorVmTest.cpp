#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/ir/VM.h"

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;
using namespace LOICollection::frontend::ir;

TEST(CycleCollectorVmTest, ReclaimsArraysAbandonedByAFunction) {
    DiagnosticEngine diagnostics;
    auto chunk = compile(
        "func build(n: int) -> int {\n"
        "    let first = [n];\n"
        "    let second = [n];\n"
        "    first[0] = second;\n"
        "    second[0] = first;\n"
        "    return n;\n"
        "}\n"
        "build(1);\n",
        diagnostics
    );
    ASSERT_TRUE(chunk) << diagnostics.getErrorMessage();

    VM vm(diagnostics);
    vm.run(chunk, Context{});

    ASSERT_FALSE(diagnostics.hasErrors()) << diagnostics.getErrorMessage();
    EXPECT_EQ(vm.report().collectedObjects, 2u);
    EXPECT_EQ(vm.heap().tracked(), 0u);
}

TEST(CycleCollectorVmTest, KeepsArraysReachableFromGlobals) {
    DiagnosticEngine diagnostics;
    auto chunk = compile(
        "let first = [1];\n"
        "let second = [2];\n"
        "first[0] = second;\n"
        "second[0] = first;\n",
        diagnostics
    );
    ASSERT_TRUE(chunk) << diagnostics.getErrorMessage();

    VM vm(diagnostics);
    vm.run(chunk, Context{});

    ASSERT_FALSE(diagnostics.hasErrors()) << diagnostics.getErrorMessage();
    EXPECT_EQ(vm.report().collectedObjects, 0u);
    EXPECT_EQ(vm.heap().tracked(), 2u);
    EXPECT_EQ(vm.heap().collect(), 0u);
    EXPECT_EQ(vm.heap().tracked(), 2u);
}

TEST(CycleCollectorVmTest, SkipsTrackingWhenAScriptNeverStoresReferences) {
    DiagnosticEngine diagnostics;
    auto chunk = compile(
        "let a = 1;\n"
        "let b = 2;\n"
        "a + b\n",
        diagnostics
    );
    ASSERT_TRUE(chunk) << diagnostics.getErrorMessage();
    EXPECT_FALSE(chunk->hasRefStores);

    VM vm(diagnostics);
    vm.run(chunk, Context{});

    ASSERT_FALSE(diagnostics.hasErrors()) << diagnostics.getErrorMessage();
    EXPECT_EQ(vm.heap().tracked(), 0u);
    EXPECT_EQ(vm.report().collectedObjects, 0u);
}

TEST(CycleCollectorVmTest, TracksChunksThatStoreReferences) {
    DiagnosticEngine diagnostics;
    auto chunk = compile(
        "let first = [1];\n"
        "let second = [2];\n"
        "first[0] = second;\n"
        "second[0] = first;\n",
        diagnostics
    );
    ASSERT_TRUE(chunk) << diagnostics.getErrorMessage();
    EXPECT_TRUE(chunk->hasRefStores);
}
