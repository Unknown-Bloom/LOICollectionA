#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/ir/CycleCollector.h"

namespace LOICollection::frontend::runtime {
    class ScriptHeapRegistry {
    public:
        LOICOLLECTION_A_NDAPI static ScriptHeapRegistry& getInstance();

        LOICOLLECTION_A_API std::shared_ptr<ir::CycleCollector> forScript(const std::string& scriptId);
        LOICOLLECTION_A_API void release(const std::string& scriptId);
        LOICOLLECTION_A_API void releaseAll();

    private:
        ScriptHeapRegistry() = default;

        std::mutex mMutex;
        std::unordered_map<std::string, std::shared_ptr<ir::CycleCollector>> mHeaps;
    };
}
