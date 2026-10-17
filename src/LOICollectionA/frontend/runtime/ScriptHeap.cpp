#include <utility>

#include "LOICollectionA/frontend/runtime/ScriptHeap.h"

namespace LOICollection::frontend::runtime {
    ScriptHeapRegistry& ScriptHeapRegistry::getInstance() {
        static ScriptHeapRegistry instance;

        return instance;
    }

    std::shared_ptr<ir::CycleCollector> ScriptHeapRegistry::forScript(const std::string& scriptId) {
        const std::lock_guard<std::mutex> guard(this->mMutex);

        auto& heap = this->mHeaps[scriptId];
        if (!heap)
            heap = std::make_shared<ir::CycleCollector>(true);

        return heap;
    }

    void ScriptHeapRegistry::release(const std::string& scriptId) {
        const std::lock_guard<std::mutex> guard(this->mMutex);

        this->mHeaps.erase(scriptId);
    }

    void ScriptHeapRegistry::releaseAll() {
        const std::lock_guard<std::mutex> guard(this->mMutex);

        this->mHeaps.clear();
    }
}
