#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <ll/api/Expected.h>

#include "LOICollectionA/frontend/Callback.h"

class JsonStorage;

namespace LOICollection::frontend::storage {
    class ScriptStorage {
    public:
        explicit ScriptStorage(std::filesystem::path root);

        [[nodiscard]] ll::Expected<TypedValue> get(const std::string& scriptId, const std::string& key);
        [[nodiscard]] ll::Expected<TypedValue> set(const std::string& scriptId, const std::string& key, const TypedValue& value);
        [[nodiscard]] ll::Expected<bool> has(const std::string& scriptId, const std::string& key);
        [[nodiscard]] ll::Expected<bool> remove(const std::string& scriptId, const std::string& key);
        [[nodiscard]] ll::Expected<ArrayRef> keys(const std::string& scriptId);
        [[nodiscard]] ll::Expected<void> clear(const std::string& scriptId);

        [[nodiscard]] ll::Expected<TypedValue> playerGet(const std::string& playerId, const std::string& scriptId, const std::string& key);
        [[nodiscard]] ll::Expected<TypedValue> playerSet(const std::string& playerId, const std::string& scriptId, const std::string& key, const TypedValue& value);
        [[nodiscard]] ll::Expected<bool> playerHas(const std::string& playerId, const std::string& scriptId, const std::string& key);
        [[nodiscard]] ll::Expected<bool> playerRemove(const std::string& playerId, const std::string& scriptId, const std::string& key);
        [[nodiscard]] ll::Expected<ArrayRef> playerKeys(const std::string& playerId, const std::string& scriptId);
        [[nodiscard]] ll::Expected<void> playerClear(const std::string& playerId, const std::string& scriptId);

    private:
        std::filesystem::path mRoot;
        std::mutex mMutex;
        std::unordered_map<std::string, std::shared_ptr<JsonStorage>> mStores;

        [[nodiscard]] ll::Expected<std::shared_ptr<JsonStorage>> store(const std::string& relative);
        [[nodiscard]] ll::Expected<TypedValue> read(const std::string& relative, const std::string& key);
        [[nodiscard]] ll::Expected<TypedValue> write(const std::string& relative, const std::string& key, const TypedValue& value);
        [[nodiscard]] ll::Expected<bool> contains(const std::string& relative, const std::string& key);
        [[nodiscard]] ll::Expected<bool> erase(const std::string& relative, const std::string& key);
        [[nodiscard]] ll::Expected<ArrayRef> names(const std::string& relative);
        [[nodiscard]] ll::Expected<void> eraseAll(const std::string& relative);
    };
}
