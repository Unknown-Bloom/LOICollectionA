#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/AST.h"

namespace LOICollection::frontend::ir {
    class CycleCollector {
    public:
        explicit CycleCollector(bool shared = false) : mShared(shared) {}

        LOICOLLECTION_A_API void track(const ObjectRef& object);
        LOICOLLECTION_A_API void track(const ArrayRef& array);
        LOICOLLECTION_A_API void track(const FunctionRefPtr& closure);

        LOICOLLECTION_A_API void noteReferenceStore();

        LOICOLLECTION_A_API void enter();
        LOICOLLECTION_A_API void leave();

        [[nodiscard]] LOICOLLECTION_A_NDAPI bool exclusive() const {
            return this->mActive.load(std::memory_order_relaxed) <= 1;
        }

        LOICOLLECTION_A_API void barrier(const ValueNode::ValueType* previous, const ValueNode::ValueType& value);

        LOICOLLECTION_A_API void step(std::size_t budget);

        LOICOLLECTION_A_NDAPI std::size_t collect();

        LOICOLLECTION_A_NDAPI bool collecting() const {
            return this->mPhase != Phase::Idle;
        }

        LOICOLLECTION_A_NDAPI static bool holdsReference(const ValueNode::ValueType& value);

        LOICOLLECTION_A_NDAPI std::size_t tracked() const {
            return this->mObjects.size() + this->mArrays.size() + this->mClosures.size();
        }

        LOICOLLECTION_A_NDAPI bool hasPendingWork() const {
            return this->mReferenceStores != 0 && this->tracked() != 0;
        }

    private:
        static constexpr std::size_t kCompactFloor = 4096;

        [[nodiscard]] std::unique_lock<std::mutex> guard() const {
            std::unique_lock<std::mutex> lock(this->mMutex, std::defer_lock);
            if (this->mShared)
                lock.lock();

            return lock;
        }

        enum class Phase : unsigned char { Idle, Build, Index, Mark, Sweep };
        enum class Kind : unsigned char { Object, Array, Closure };

        struct Node {
            Kind kind = Kind::Object;
            void* raw = nullptr;
            std::shared_ptr<void> hold;
            std::size_t indegree = 0;
            bool marked = false;
        };

        bool begin();
        void compact();
        void reserveHeadroom();
        void stepLocked(std::size_t budget);

        template <class Tracked>
        void trackNode(const std::shared_ptr<Tracked>& node, std::vector<std::weak_ptr<Tracked>>& registry);

        void buildStep(std::size_t budget);
        void indexStep(std::size_t budget);
        void markStep(std::size_t budget);
        void sweepStep(std::size_t budget);

        void markValue(const ValueNode::ValueType& value);
        void markNode(std::size_t index);

        template <class Visitor>
        static void visitEdges(const Node& node, Visitor&& visit);

        static void detachEdges(Node& node, std::vector<ValueNode::ValueType>& graveyard);
        static void stamp(const Node& node, std::int32_t index);

        std::vector<std::weak_ptr<Object>> mObjects;
        std::vector<std::weak_ptr<ArrayValue>> mArrays;
        std::vector<std::weak_ptr<FunctionRef>> mClosures;

        std::vector<Node> mNodes;
        std::vector<std::size_t> mPending;
        std::vector<ValueNode::ValueType> mGraveyard;

        std::size_t mObjectCursor = 0;
        std::size_t mArrayCursor = 0;
        std::size_t mClosureCursor = 0;
        std::size_t mCursor = 0;
        std::size_t mReclaimed = 0;
        std::size_t mReferenceStores = 0;
        std::atomic<int> mActive = 0;
        std::size_t mCompactAt = kCompactFloor;

        bool mShared;
        mutable std::mutex mMutex;
        Phase mPhase = Phase::Idle;
    };
}
