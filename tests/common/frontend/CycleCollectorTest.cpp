#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "LOICollectionA/frontend/ir/CycleCollector.h"

#include "common/frontend/CommonTest.h"

using namespace LOICollection::frontend;
using namespace LOICollection::frontend::ir;

TEST(CycleCollectorTest, ReclaimsTwoObjectCycle) {
    CycleCollector collector;

    {
        auto first = std::make_shared<Object>();
        auto second = std::make_shared<Object>();
        first->addField("next", second);
        second->addField("next", first);

        collector.track(first);
        collector.track(second);
        collector.noteReferenceStore();
    }

    EXPECT_EQ(collector.tracked(), 2u);
    EXPECT_EQ(collector.collect(), 2u);
    EXPECT_EQ(collector.tracked(), 0u);
}

TEST(CycleCollectorTest, ReclaimsSelfReference) {
    CycleCollector collector;

    {
        auto node = std::make_shared<Object>();
        node->addField("self", node);

        collector.track(node);
        collector.noteReferenceStore();
    }

    EXPECT_EQ(collector.collect(), 1u);
}

TEST(CycleCollectorTest, KeepsGraphHeldFromOutside) {
    CycleCollector collector;

    auto root = std::make_shared<Object>();
    auto child = std::make_shared<Object>();
    root->addField("child", child);
    child->addField("parent", root);

    collector.track(root);
    collector.track(child);
    collector.noteReferenceStore();

    EXPECT_EQ(collector.collect(), 0u);
    EXPECT_EQ(collector.tracked(), 2u);

    const auto* retained = root->find("child");
    ASSERT_NE(retained, nullptr);
    ASSERT_TRUE(std::holds_alternative<ObjectRef>(*retained));
    EXPECT_EQ(std::get<ObjectRef>(*retained), child);
}

TEST(CycleCollectorTest, ReclaimsArrayCycle) {
    CycleCollector collector;

    {
        auto first = std::make_shared<ArrayValue>();
        auto second = std::make_shared<ArrayValue>();
        first->elements.emplace_back(second);
        second->elements.emplace_back(first);

        collector.track(first);
        collector.track(second);
        collector.noteReferenceStore();
    }

    EXPECT_EQ(collector.collect(), 2u);
}

TEST(CycleCollectorTest, ReclaimsClosureCaptureCycle) {
    CycleCollector collector;

    {
        auto closure = std::make_shared<FunctionRef>();
        auto holder = std::make_shared<Object>();
        closure->captures.emplace_back(holder);
        holder->addField("callback", closure);

        collector.track(closure);
        collector.track(holder);
        collector.noteReferenceStore();
    }

    EXPECT_EQ(collector.collect(), 2u);
}

TEST(CycleCollectorTest, KeepsClosureReachableFromOutside) {
    CycleCollector collector;

    auto closure = std::make_shared<FunctionRef>();
    auto holder = std::make_shared<Object>();
    closure->captures.emplace_back(holder);
    holder->addField("callback", closure);

    collector.track(closure);
    collector.track(holder);
    collector.noteReferenceStore();

    EXPECT_EQ(collector.collect(), 0u);
    ASSERT_EQ(holder->slots.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<FunctionRefPtr>(holder->slots.front()));
}

TEST(CycleCollectorTest, CompactsAcyclicNodesWithoutScanningTheGraph) {
    CycleCollector collector;

    {
        auto node = std::make_shared<Object>();
        collector.track(node);
    }

    EXPECT_EQ(collector.tracked(), 1u);
    EXPECT_EQ(collector.collect(), 0u);
    EXPECT_EQ(collector.tracked(), 0u);
}

namespace cycle_collector_test_support {
    struct ProbeHandle : NativeHandle {
        FunctionRefPtr closure;

        void trace(ValueVisitor& visitor) override { visitor.visitClosure(this->closure); }
    };
}

TEST(CycleCollectorTest, FollowsReferencesHeldByNativeHandles) {
    CycleCollector collector;

    {
        auto object = std::make_shared<Object>();
        auto closure = std::make_shared<FunctionRef>();

        auto handle = std::make_shared<cycle_collector_test_support::ProbeHandle>();
        handle->closure = closure;
        object->native = handle;

        closure->captures.emplace_back(object);

        collector.track(object);
        collector.track(closure);
        collector.noteReferenceStore();
    }

    EXPECT_EQ(collector.collect(), 2u);
}

TEST(CycleCollectorTest, AdvancesInBoundedSteps) {
    CycleCollector collector;

    {
        for (int i = 0; i < 3; ++i) {
            auto first = std::make_shared<Object>();
            auto second = std::make_shared<Object>();
            first->addField("next", second);
            second->addField("next", first);

            collector.track(first);
            collector.track(second);
        }

        collector.noteReferenceStore();
    }

    collector.step(1);
    EXPECT_TRUE(collector.collecting());

    std::size_t steps = 1;
    while (collector.collecting() && steps < 64) {
        collector.step(1);
        ++steps;
    }

    EXPECT_FALSE(collector.collecting());
    EXPECT_GT(steps, 1u);
    EXPECT_EQ(collector.tracked(), 0u);
}

TEST(CycleCollectorTest, BarrierKeepsAValueStoredDuringTheCollection) {
    CycleCollector collector;

    auto first = std::make_shared<Object>();
    auto second = std::make_shared<Object>();
    first->addField("next", second);
    second->addField("next", first);

    collector.track(first);
    collector.track(second);
    collector.noteReferenceStore();

    collector.step(2);
    collector.step(2);
    ASSERT_TRUE(collector.collecting());

    collector.barrier(nullptr, ValueNode::ValueType(second));

    while (collector.collecting())
        collector.step(16);

    ASSERT_TRUE(second->find("next") != nullptr);
    EXPECT_TRUE(std::holds_alternative<ObjectRef>(*second->find("next")));
}
