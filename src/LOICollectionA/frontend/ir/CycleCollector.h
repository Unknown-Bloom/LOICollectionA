#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/AST.h"

namespace LOICollection::frontend::ir {
    class CycleCollector {
    public:
        LOICOLLECTION_A_API void track(const ObjectRef& object);
        LOICOLLECTION_A_API void track(const ArrayRef& array);
        LOICOLLECTION_A_API void track(const FunctionRefPtr& closure);

        LOICOLLECTION_A_API void noteReferenceStore();

        LOICOLLECTION_A_NDAPI std::size_t collect();

        LOICOLLECTION_A_NDAPI static bool holdsReference(const ValueNode::ValueType& value);

        LOICOLLECTION_A_NDAPI std::size_t tracked() const {
            return this->mObjects.size() + this->mArrays.size() + this->mClosures.size();
        }

        LOICOLLECTION_A_NDAPI bool hasPendingWork() const {
            return this->mReferenceStores != 0 && this->tracked() != 0;
        }

    private:
        static constexpr std::size_t kCompactFloor = 4096;

        void compact();
        void reserveHeadroom();

        std::vector<std::weak_ptr<Object>> mObjects;
        std::vector<std::weak_ptr<ArrayValue>> mArrays;
        std::vector<std::weak_ptr<FunctionRef>> mClosures;

        std::size_t mReferenceStores = 0;
        std::size_t mCompactAt = kCompactFloor;
        bool mCollecting = false;
    };
}
