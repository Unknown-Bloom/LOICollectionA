#include <gtest/gtest.h>

#include <memory>

#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/ir/CycleCollector.h"
#include "LOICollectionA/frontend/ir/VM.h"
#include "LOICollectionA/frontend/runtime/ScriptHeap.h"

using namespace LOICollection::frontend;
using namespace LOICollection::frontend::ir;

namespace {
    void link(std::shared_ptr<Object>& left, std::shared_ptr<Object>& right) {
        left->addField("next", right);
        right->addField("next", left);
    }
}

TEST(CrossHeapTest, SharedHeapReclaimsACycleThatSpansTwoVirtualMachines) {
    auto heap = std::make_shared<CycleCollector>();

    DiagnosticEngine firstDiagnostics;
    VM first(firstDiagnostics);
    first.setHeap(heap);

    DiagnosticEngine secondDiagnostics;
    VM second(secondDiagnostics);
    second.setHeap(heap);

    {
        auto left = std::make_shared<Object>();
        auto right = std::make_shared<Object>();
        link(left, right);

        first.heap().track(left);
        second.heap().track(right);
        first.heap().noteReferenceStore();
    }

    EXPECT_EQ(first.heap().collect(), 2u);
    EXPECT_EQ(first.heap().tracked(), 0u);
}

TEST(CrossHeapTest, SeparateHeapsStillReclaimTheirOwnCycles) {
    DiagnosticEngine firstDiagnostics;
    VM first(firstDiagnostics);

    DiagnosticEngine secondDiagnostics;
    VM second(secondDiagnostics);

    {
        auto left = std::make_shared<Object>();
        auto right = std::make_shared<Object>();
        link(left, right);

        first.heap().track(left);
        first.heap().track(right);
        first.heap().noteReferenceStore();

        auto other = std::make_shared<Object>();
        auto peer = std::make_shared<Object>();
        link(other, peer);

        second.heap().track(other);
        second.heap().track(peer);
        second.heap().noteReferenceStore();
    }

    EXPECT_EQ(first.heap().collect(), 2u);
    EXPECT_EQ(second.heap().collect(), 2u);
}

TEST(CrossHeapTest, CollectionWaitsUntilTheHeapIsExclusive) {
    auto heap = std::make_shared<CycleCollector>();

    heap->enter();
    heap->enter();

    {
        auto left = std::make_shared<Object>();
        auto right = std::make_shared<Object>();
        link(left, right);

        heap->track(left);
        heap->track(right);
        heap->noteReferenceStore();
    }

    EXPECT_EQ(heap->collect(), 0u);

    heap->leave();
    EXPECT_EQ(heap->collect(), 2u);

    heap->leave();
}

TEST(CrossHeapTest, ScriptHeapRegistrySharesOneHeapPerScript) {
    auto& registry = runtime::ScriptHeapRegistry::getInstance();

    auto first = registry.forScript("cross.heap.probe");
    auto second = registry.forScript("cross.heap.probe");
    auto other = registry.forScript("cross.heap.other");

    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first, second);
    EXPECT_NE(first, other);

    registry.release("cross.heap.probe");
    EXPECT_NE(registry.forScript("cross.heap.probe"), first);
    EXPECT_EQ(registry.forScript("cross.heap.other"), other);

    registry.release("cross.heap.other");
}
