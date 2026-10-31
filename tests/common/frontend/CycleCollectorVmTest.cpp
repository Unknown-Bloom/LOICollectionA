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
        "func link(a, b) -> int { a[0] = b; return 0; }\n"
        "func build(n: int) -> int {\n"
        "    let first = [n];\n"
        "    let second = [n];\n"
        "    link(first, second);\n"
        "    link(second, first);\n"
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
        "func link(a, b) -> int { a[0] = b; return 0; }\n"
        "link(first, second);\n"
        "link(second, first);\n",
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
        "first[0] = second;\n",
        diagnostics
    );
    ASSERT_TRUE(chunk) << diagnostics.getErrorMessage();
    EXPECT_TRUE(chunk->hasRefStores);
}

TEST(CycleCollectorVmTest, ReclaimsAFieldCycleAtTheEndOfARunOnASharedHeap) {
    DiagnosticEngine diagnostics;
    auto chunk = compile(
        "func link(a, b) -> int { a.next = b; return 0; }\n"
        "class Node {\n"
        "public:\n"
        "    value = 0;\n"
        "    next = 0;\n"
        "}\n"
        "func build() -> int {\n"
        "    let first = new Node();\n"
        "    let second = new Node();\n"
        "    link(first, second);\n"
        "    link(second, first);\n"
        "    return 0;\n"
        "}\n"
        "build();\n",
        diagnostics
    );
    ASSERT_TRUE(chunk) << diagnostics.getErrorMessage();
    ASSERT_FALSE(chunk->methodBodies.empty());
    ASSERT_TRUE(chunk->methodBodies.front()->hasRefStores);

    auto heap = std::make_shared<CycleCollector>(true);

    VM vm(diagnostics);
    vm.setHeap(heap);
    vm.run(chunk, Context{});

    ASSERT_FALSE(diagnostics.hasErrors()) << diagnostics.getErrorMessage();
    EXPECT_EQ(vm.report().collectedObjects, 2u);
    EXPECT_EQ(heap->tracked(), 0u);
}

TEST(CycleCollectorVmTest, KeepsTheTrackedGraphFlatAcrossRepeatedRuns) {
    DiagnosticEngine diagnostics;
    auto chunk = compile(
        "func link(a, b) -> int { a.next = b; return 0; }\n"
        "class Node {\n"
        "public:\n"
        "    next = 0;\n"
        "}\n"
        "func build() -> int {\n"
        "    let first = new Node();\n"
        "    let second = new Node();\n"
        "    link(first, second);\n"
        "    link(second, first);\n"
        "    return 0;\n"
        "}\n"
        "build();\n",
        diagnostics
    );
    ASSERT_TRUE(chunk) << diagnostics.getErrorMessage();

    auto heap = std::make_shared<CycleCollector>(true);

    VM vm(diagnostics);
    vm.setHeap(heap);

    for (int i = 0; i < 200; ++i)
        vm.run(chunk, Context{});

    ASSERT_FALSE(diagnostics.hasErrors()) << diagnostics.getErrorMessage();
    EXPECT_LT(heap->tracked(), 4u);
    EXPECT_GT(vm.report().collectedObjects, 0u);
}
