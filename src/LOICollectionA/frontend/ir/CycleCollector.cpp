#include <algorithm>
#include <cstdint>
#include <utility>

#include "LOICollectionA/frontend/ir/CycleCollector.h"

namespace LOICollection::frontend::ir::detail {
    constexpr std::int32_t kUntracked = -1;
    constexpr std::int32_t kPending = -2;

    struct Edge {
        std::int32_t index = kUntracked;
        const void* raw = nullptr;
    };

    Edge edgeOf(const ValueNode::ValueType& value) {
        if (const auto* object = std::get_if<ObjectRef>(&value))
            return *object ? Edge{ (*object)->heapIndex, object->get() } : Edge{};

        if (const auto* array = std::get_if<ArrayRef>(&value))
            return *array ? Edge{ (*array)->heapIndex, array->get() } : Edge{};

        if (const auto* closure = std::get_if<FunctionRefPtr>(&value))
            return *closure ? Edge{ (*closure)->heapIndex, closure->get() } : Edge{};

        return {};
    }

    template <class Sink>
    struct NativeEdgeSink final : ValueVisitor {
        Sink& sink;

        explicit NativeEdgeSink(Sink& target) : sink(target) {}

        void visitObject(ObjectRef& value) override { this->sink(value); }
        void visitArray(ArrayRef& value) override { this->sink(value); }
        void visitClosure(FunctionRefPtr& value) override { this->sink(value); }
    };

    template <class Sink>
    void visitNative(Object& object, Sink&& sink) {
        if (!object.native)
            return;

        NativeEdgeSink<Sink> visitor{ sink };
        object.native->trace(visitor);
    }
}

namespace LOICollection::frontend::ir {
    using namespace detail;

    bool CycleCollector::holdsReference(const ValueNode::ValueType& value) {
        return edgeOf(value).raw != nullptr;
    }

    void CycleCollector::reserveHeadroom() {
        if (this->mPhase != Phase::Idle)
            return;

        if (this->tracked() < this->mCompactAt)
            return;

        this->compact();
    }

    void CycleCollector::compact() {
        std::erase_if(this->mObjects, [](const std::weak_ptr<Object>& node) { return node.expired(); });
        std::erase_if(this->mArrays, [](const std::weak_ptr<ArrayValue>& node) { return node.expired(); });
        std::erase_if(this->mClosures, [](const std::weak_ptr<FunctionRef>& node) { return node.expired(); });

        this->mCompactAt = std::max(kCompactFloor, this->tracked() * 2);
    }

    template <class Tracked>
    void CycleCollector::trackNode(const std::shared_ptr<Tracked>& node, std::vector<std::weak_ptr<Tracked>>& registry) {
        if (!node)
            return;

        if (node->heapIndex != kUntracked)
            return;

        const auto lock = this->guard();
        if (node->heapIndex != kUntracked)
            return;

        node->heapIndex = kPending;
        this->reserveHeadroom();
        registry.emplace_back(node);
    }

    void CycleCollector::track(const ObjectRef& object) {
        this->trackNode(object, this->mObjects);
    }

    void CycleCollector::track(const ArrayRef& array) {
        this->trackNode(array, this->mArrays);
    }

    void CycleCollector::track(const FunctionRefPtr& closure) {
        this->trackNode(closure, this->mClosures);
    }

    void CycleCollector::noteReferenceStore() {
        this->mReferenceStores = 1;
    }

    void CycleCollector::enter() {
        this->mActive.fetch_add(1, std::memory_order_acq_rel);
    }

    void CycleCollector::leave() {
        this->mActive.fetch_sub(1, std::memory_order_acq_rel);
    }

    void CycleCollector::markNode(std::size_t index) {
        if (index >= this->mNodes.size())
            return;

        Node& node = this->mNodes[index];
        if (node.marked)
            return;

        node.marked = true;

        if (this->mPhase == Phase::Mark)
            this->mPending.push_back(index);
    }

    void CycleCollector::markValue(const ValueNode::ValueType& value) {
        if (this->mPhase != Phase::Index && this->mPhase != Phase::Mark && this->mPhase != Phase::Sweep)
            return;

        const Edge edge = edgeOf(value);
        if (edge.index < 0 || static_cast<std::size_t>(edge.index) >= this->mNodes.size())
            return;

        if (this->mNodes[static_cast<std::size_t>(edge.index)].raw != edge.raw)
            return;

        this->markNode(static_cast<std::size_t>(edge.index));
    }

    void CycleCollector::barrier(const ValueNode::ValueType* previous, const ValueNode::ValueType& value) {
        if (!this->collecting())
            return;

        const auto lock = this->guard();
        if (!this->collecting())
            return;

        if (previous)
            this->markValue(*previous);

        this->markValue(value);
    }

    template <class Visitor>
    void CycleCollector::visitEdges(const Node& node, Visitor&& visit) {
        switch (node.kind) {
            case Kind::Object: {
                auto& object = *static_cast<Object*>(node.raw);
                for (auto& slot : object.slots)
                    visit(slot);

                if (object.spill)
                    for (auto& entry : *object.spill)
                        visit(entry.second);

                visitNative(object, [&visit](auto& reference) { visit(reference); });
            } break;
            case Kind::Array:
                for (auto& element : static_cast<ArrayValue*>(node.raw)->elements)
                    visit(element);
                break;
            case Kind::Closure:
                for (auto& capture : static_cast<FunctionRef*>(node.raw)->captures)
                    visit(capture);
                break;
        }
    }

    void CycleCollector::stamp(const Node& node, std::int32_t index) {
        switch (node.kind) {
            case Kind::Object:
                static_cast<Object*>(node.raw)->heapIndex = index;
                break;
            case Kind::Array:
                static_cast<ArrayValue*>(node.raw)->heapIndex = index;
                break;
            case Kind::Closure:
                static_cast<FunctionRef*>(node.raw)->heapIndex = index;
                break;
        }
    }

    void CycleCollector::detachEdges(Node& node, std::vector<ValueNode::ValueType>& graveyard) {
        const auto detach = [&graveyard](auto& reference) {
            graveyard.emplace_back(std::move(reference));
        };

        switch (node.kind) {
            case Kind::Object: {
                auto& object = *static_cast<Object*>(node.raw);
                for (auto& slot : object.slots)
                    detach(slot);

                if (object.spill)
                    for (auto& entry : *object.spill)
                        detach(entry.second);

                visitNative(object, detach);
            } break;
            case Kind::Array: {
                auto& array = *static_cast<ArrayValue*>(node.raw);
                for (auto& element : array.elements)
                    detach(element);
            } break;
            case Kind::Closure: {
                auto& closure = *static_cast<FunctionRef*>(node.raw);
                for (auto& capture : closure.captures)
                    detach(capture);
            } break;
        }
    }

    bool CycleCollector::begin() {
        if (this->mPhase != Phase::Idle)
            return true;

        if (std::exchange(this->mReferenceStores, 0) == 0)
            return false;

        this->mNodes.clear();
        this->mPending.clear();
        this->mGraveyard.clear();

        this->mObjectCursor = 0;
        this->mArrayCursor = 0;
        this->mClosureCursor = 0;
        this->mCursor = 0;
        this->mReclaimed = 0;
        this->mPhase = Phase::Build;

        return true;
    }

    void CycleCollector::buildStep(std::size_t budget) {
        while (budget != 0 && this->mObjectCursor < this->mObjects.size()) {
            if (auto live = this->mObjects[this->mObjectCursor++].lock())
                this->mNodes.push_back({ Kind::Object, live.get(), live });

            --budget;
        }

        while (budget != 0 && this->mArrayCursor < this->mArrays.size()) {
            if (auto live = this->mArrays[this->mArrayCursor++].lock())
                this->mNodes.push_back({ Kind::Array, live.get(), live });

            --budget;
        }

        while (budget != 0 && this->mClosureCursor < this->mClosures.size()) {
            if (auto live = this->mClosures[this->mClosureCursor++].lock())
                this->mNodes.push_back({ Kind::Closure, live.get(), live });

            --budget;
        }

        if (this->mObjectCursor == this->mObjects.size()
            && this->mArrayCursor == this->mArrays.size()
            && this->mClosureCursor == this->mClosures.size()) {
            for (std::size_t i = 0; i < this->mNodes.size(); ++i)
                stamp(this->mNodes[i], static_cast<std::int32_t>(i));

            this->mCursor = 0;
            this->mPhase = Phase::Index;
        }
    }

    void CycleCollector::indexStep(std::size_t budget) {
        const auto resolves = [this](const Edge& edge) -> std::int32_t {
            if (edge.index < 0 || static_cast<std::size_t>(edge.index) >= this->mNodes.size())
                return kUntracked;

            if (this->mNodes[static_cast<std::size_t>(edge.index)].raw != edge.raw)
                return kUntracked;

            return edge.index;
        };

        while (budget != 0 && this->mCursor < this->mNodes.size()) {
            visitEdges(this->mNodes[this->mCursor], [this, &resolves](const ValueNode::ValueType& value) {
                const std::int32_t target = resolves(edgeOf(value));
                if (target >= 0)
                    ++this->mNodes[static_cast<std::size_t>(target)].indegree;
            });

            ++this->mCursor;
            --budget;
        }

        if (this->mCursor < this->mNodes.size())
            return;

        this->mPending.clear();
        for (std::size_t i = 0; i < this->mNodes.size(); ++i) {
            if (static_cast<std::size_t>(this->mNodes[i].hold.use_count()) <= this->mNodes[i].indegree + 1)
                continue;

            this->markNode(i);
        }

        this->mPhase = Phase::Mark;
    }

    void CycleCollector::markStep(std::size_t budget) {
        while (budget != 0 && !this->mPending.empty()) {
            const std::size_t current = this->mPending.back();
            this->mPending.pop_back();

            visitEdges(this->mNodes[current], [this](const ValueNode::ValueType& value) {
                this->markValue(value);
            });

            --budget;
        }

        if (!this->mPending.empty())
            return;

        this->mCursor = 0;
        this->mPhase = Phase::Sweep;
    }

    void CycleCollector::sweepStep(std::size_t budget) {
        while (budget != 0 && this->mCursor < this->mNodes.size()) {
            Node& node = this->mNodes[this->mCursor++];

            if (!node.marked) {
                ++this->mReclaimed;
                detachEdges(node, this->mGraveyard);
            }

            --budget;
        }

        if (this->mCursor < this->mNodes.size())
            return;

        this->mGraveyard.clear();
        this->mNodes.clear();
        this->mPhase = Phase::Idle;
        this->compact();
    }

    void CycleCollector::step(std::size_t budget) {
        if (!this->exclusive())
            return;

        const auto lock = this->guard();

        if (this->mPhase == Phase::Idle && !this->begin()) {
            this->reserveHeadroom();
            return;
        }

        if (budget == 0)
            budget = 1;

        switch (this->mPhase) {
            case Phase::Build:
                this->buildStep(budget);
                break;
            case Phase::Index:
                this->indexStep(budget);
                break;
            case Phase::Mark:
                this->markStep(budget);
                break;
            case Phase::Sweep:
                this->sweepStep(budget);
                break;
            case Phase::Idle:
                break;
        }
    }

    std::size_t CycleCollector::collect() {
        if (!this->exclusive())
            return 0;

        const auto lock = this->guard();

        if (!this->begin()) {
            this->compact();
            return 0;
        }

        while (this->mPhase != Phase::Idle)
            this->stepLocked(SIZE_MAX);

        return this->mReclaimed;
    }

    void CycleCollector::stepLocked(std::size_t budget) {
        if (budget == 0)
            budget = 1;

        switch (this->mPhase) {
            case Phase::Build:
                this->buildStep(budget);
                break;
            case Phase::Index:
                this->indexStep(budget);
                break;
            case Phase::Mark:
                this->markStep(budget);
                break;
            case Phase::Sweep:
                this->sweepStep(budget);
                break;
            case Phase::Idle:
                break;
        }
    }
}
