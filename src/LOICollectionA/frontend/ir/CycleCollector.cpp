#include <algorithm>
#include <utility>

#include "LOICollectionA/frontend/ir/CycleCollector.h"

namespace LOICollection::frontend::ir::detail {
    constexpr std::int32_t kUntracked = -1;
    constexpr std::int32_t kPending = -2;

    enum class NodeKind : unsigned char { Object, Array, Closure };

    struct Node {
        NodeKind kind = NodeKind::Object;
        void* raw = nullptr;
        std::shared_ptr<void> hold;
        std::size_t indegree = 0;
        bool marked = false;
    };

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

    template <class Visitor>
    void visitEdges(const Node& node, Visitor&& visit) {
        switch (node.kind) {
            case NodeKind::Object: {
                const auto& object = *static_cast<Object*>(node.raw);
                for (const auto& slot : object.slots)
                    visit(slot);

                if (object.spill)
                    for (const auto& entry : *object.spill)
                        visit(entry.second);
            } break;
            case NodeKind::Array:
                for (const auto& element : static_cast<ArrayValue*>(node.raw)->elements)
                    visit(element);
                break;
            case NodeKind::Closure:
                for (const auto& capture : static_cast<FunctionRef*>(node.raw)->captures)
                    visit(capture);
                break;
        }
    }

    void stamp(const Node& node, std::int32_t index) {
        switch (node.kind) {
            case NodeKind::Object:
                static_cast<Object*>(node.raw)->heapIndex = index;
                break;
            case NodeKind::Array:
                static_cast<ArrayValue*>(node.raw)->heapIndex = index;
                break;
            case NodeKind::Closure:
                static_cast<FunctionRef*>(node.raw)->heapIndex = index;
                break;
        }
    }

    void detachEdges(Node& node, std::vector<ValueNode::ValueType>& graveyard) {
        switch (node.kind) {
            case NodeKind::Object: {
                auto& object = *static_cast<Object*>(node.raw);
                for (auto& slot : object.slots)
                    graveyard.push_back(std::move(slot));

                if (object.spill)
                    for (auto& entry : *object.spill)
                        graveyard.push_back(std::move(entry.second));
            } break;
            case NodeKind::Array: {
                auto& array = *static_cast<ArrayValue*>(node.raw);
                for (auto& element : array.elements)
                    graveyard.push_back(std::move(element));
            } break;
            case NodeKind::Closure: {
                auto& closure = *static_cast<FunctionRef*>(node.raw);
                for (auto& capture : closure.captures)
                    graveyard.push_back(std::move(capture));
            } break;
        }
    }
}

namespace LOICollection::frontend::ir {
    using namespace detail;

    void CycleCollector::reserveHeadroom() {
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

    void CycleCollector::track(const ObjectRef& object) {
        if (!object || object->heapIndex != kUntracked)
            return;

        object->heapIndex = kPending;
        this->reserveHeadroom();
        this->mObjects.emplace_back(object);
    }

    void CycleCollector::track(const ArrayRef& array) {
        if (!array || array->heapIndex != kUntracked)
            return;

        array->heapIndex = kPending;
        this->reserveHeadroom();
        this->mArrays.emplace_back(array);
    }

    void CycleCollector::track(const FunctionRefPtr& closure) {
        if (!closure || closure->heapIndex != kUntracked)
            return;

        closure->heapIndex = kPending;
        this->reserveHeadroom();
        this->mClosures.emplace_back(closure);
    }

    void CycleCollector::noteReferenceStore() {
        ++this->mReferenceStores;
    }

    bool CycleCollector::holdsReference(const ValueNode::ValueType& value) {
        return edgeOf(value).raw != nullptr;
    }

    std::size_t CycleCollector::collect() {
        if (this->mCollecting)
            return 0;

        this->mCollecting = true;
        struct Guard {
            bool& flag;
            ~Guard() { flag = false; }
        } guard{ this->mCollecting };

        const std::size_t referenceStores = std::exchange(this->mReferenceStores, 0);
        if (referenceStores == 0) {
            this->compact();
            return 0;
        }

        std::vector<Node> nodes;
        nodes.reserve(this->tracked());

        for (auto& weak : this->mObjects)
            if (auto live = weak.lock())
                nodes.push_back({ NodeKind::Object, live.get(), live });

        for (auto& weak : this->mArrays)
            if (auto live = weak.lock())
                nodes.push_back({ NodeKind::Array, live.get(), live });

        for (auto& weak : this->mClosures)
            if (auto live = weak.lock())
                nodes.push_back({ NodeKind::Closure, live.get(), live });

        for (std::size_t i = 0; i < nodes.size(); ++i)
            stamp(nodes[i], static_cast<std::int32_t>(i));

        const auto resolves = [&nodes](const Edge& edge) -> std::int32_t {
            if (edge.index < 0 || static_cast<std::size_t>(edge.index) >= nodes.size())
                return kUntracked;

            if (nodes[static_cast<std::size_t>(edge.index)].raw != edge.raw)
                return kUntracked;

            return edge.index;
        };

        for (const auto& node : nodes) {
            visitEdges(node, [&](const ValueNode::ValueType& value) {
                const std::int32_t target = resolves(edgeOf(value));
                if (target >= 0)
                    ++nodes[static_cast<std::size_t>(target)].indegree;
            });
        }

        std::vector<std::size_t> pending;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (static_cast<std::size_t>(nodes[i].hold.use_count()) <= nodes[i].indegree + 1)
                continue;

            nodes[i].marked = true;
            pending.push_back(i);
        }

        while (!pending.empty()) {
            const std::size_t current = pending.back();
            pending.pop_back();

            visitEdges(nodes[current], [&](const ValueNode::ValueType& value) {
                const std::int32_t target = resolves(edgeOf(value));
                if (target < 0)
                    return;

                Node& node = nodes[static_cast<std::size_t>(target)];
                if (node.marked)
                    return;

                node.marked = true;
                pending.push_back(static_cast<std::size_t>(target));
            });
        }

        std::size_t reclaimed = 0;
        std::vector<ValueNode::ValueType> graveyard;
        for (auto& node : nodes) {
            if (node.marked)
                continue;

            ++reclaimed;
            detachEdges(node, graveyard);
        }

        graveyard.clear();
        nodes.clear();
        this->compact();

        return reclaimed;
    }
}
