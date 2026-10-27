#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include <ll/api/Expected.h>

#include "LOICollectionA/data/json/JsonStorage.h"

#include "LOICollectionA/frontend/storage/ScriptStorage.h"

using namespace LOICollection::frontend;

namespace LOICollection::frontend::storage::detail {
    constexpr std::size_t kMaxComponentChars = 128;
    constexpr std::size_t kMaxKeyChars = 512;
    constexpr std::size_t kMaxValueBytes = 1 << 20;
    constexpr std::size_t kMaxFileBytes = 16 << 20;
    constexpr std::size_t kMaxStores = 256;
    constexpr int kMaxDepth = 64;

    std::string encodeComponent(const std::string& text) {
        static constexpr char kHex[] = "0123456789ABCDEF";

        std::string result;
        result.reserve(text.size());

        for (unsigned char c : text) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
                result.push_back(static_cast<char>(c));
                continue;
            }

            result.push_back('%');
            result.push_back(kHex[c >> 4]);
            result.push_back(kHex[c & 0x0F]);
        }

        return result;
    }

    std::optional<nlohmann::ordered_json> toJson(const TypedValue& value, std::string& error, int depth) {
        if (depth > kMaxDepth) {
            error = "Value nesting is too deep";
            return std::nullopt;
        }

        if (std::holds_alternative<std::monostate>(value))
            return nlohmann::ordered_json(nullptr);
        if (const auto* number = std::get_if<int>(&value))
            return nlohmann::ordered_json(*number);
        if (const auto* number = std::get_if<float>(&value)) {
            if (!std::isfinite(*number)) {
                error = "Non-finite numbers cannot be stored";
                return std::nullopt;
            }

            return nlohmann::ordered_json(*number);
        }
        if (const auto* text = std::get_if<std::string>(&value))
            return nlohmann::ordered_json(*text);
        if (const auto* flag = std::get_if<bool>(&value))
            return nlohmann::ordered_json(*flag);
        if (const auto* array = std::get_if<ArrayRef>(&value)) {
            nlohmann::ordered_json result = nlohmann::ordered_json::array();
            if (!*array)
                return result;

            for (const auto& element : (*array)->elements) {
                auto converted = toJson(element, error, depth + 1);
                if (!converted)
                    return std::nullopt;

                result.push_back(std::move(*converted));
            }

            return result;
        }
        if (const auto* object = std::get_if<ObjectRef>(&value)) {
            if (!*object)
                return nlohmann::ordered_json(nullptr);
            if ((*object)->native) {
                error = "Native objects cannot be stored";
                return std::nullopt;
            }

            nlohmann::ordered_json result = nlohmann::ordered_json::object();
            if ((*object)->layout) {
                const auto& names = (*object)->layout->names;
                for (std::size_t index = 0; index < names.size(); ++index) {
                    const auto* field = (*object)->declaredAt(static_cast<int>(index));
                    if (!field)
                        continue;

                    auto converted = toJson(*field, error, depth + 1);
                    if (!converted)
                        return std::nullopt;

                    result[names[index]] = std::move(*converted);
                }
            }
            if ((*object)->spill) {
                for (const auto& [name, field] : *(*object)->spill) {
                    auto converted = toJson(field, error, depth + 1);
                    if (!converted)
                        return std::nullopt;

                    result[name] = std::move(*converted);
                }
            }

            return result;
        }
        if (std::holds_alternative<FunctionRefPtr>(value)) {
            error = "Functions cannot be stored";
            return std::nullopt;
        }

        error = "Enum values cannot be stored";
        return std::nullopt;
    }

    TypedValue fromJson(const nlohmann::ordered_json& json, int depth) {
        if (depth > kMaxDepth || json.is_null())
            return TypedValue{ std::monostate{} };

        if (json.is_boolean())
            return json.get<bool>();
        if (json.is_number_unsigned()) {
            const auto number = json.get<std::uint64_t>();
            if (number <= static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
                return static_cast<int>(number);

            return static_cast<float>(number);
        }
        if (json.is_number_integer()) {
            const auto number = json.get<std::int64_t>();
            if (number >= std::numeric_limits<int>::min() && number <= std::numeric_limits<int>::max())
                return static_cast<int>(number);

            return static_cast<float>(number);
        }
        if (json.is_number_float())
            return static_cast<float>(json.get<double>());
        if (json.is_string())
            return json.get<std::string>();
        if (json.is_array()) {
            auto result = std::make_shared<ArrayValue>();
            result->elements.reserve(json.size());
            for (const auto& element : json)
                result->elements.push_back(fromJson(element, depth + 1));

            return result;
        }
        if (json.is_object()) {
            auto result = std::make_shared<Object>();
            result->className = "Object";
            result->classIndex = -1;
            for (const auto& [name, field] : json.items())
                result->assign(name, fromJson(field, depth + 1));

            return result;
        }

        return TypedValue{ std::monostate{} };
    }

    std::string scriptPath(const std::string& scriptId) {
        return "storage/script-" + encodeComponent(scriptId) + ".json";
    }

    std::string playerPath(const std::string& playerId, const std::string& scriptId) {
        return "playerdata/player-" + encodeComponent(playerId) + "/script-" + encodeComponent(scriptId) + ".json";
    }

    bool validateKey(const std::string& key, std::string& error) {
        if (key.size() > kMaxKeyChars) {
            error = "Storage key is too long";
            return false;
        }

        return true;
    }

    bool validateId(const std::string& id, const char* name, std::string& error) {
        if (id.empty()) {
            error = std::string("A ") + name + " is required";
            return false;
        }
        if (id.size() > kMaxComponentChars) {
            error = std::string("The ") + name + " is too long";
            return false;
        }

        return true;
    }
}

namespace LOICollection::frontend::storage {
    using namespace detail;

    ScriptStorage::ScriptStorage(std::filesystem::path root) : mRoot(std::move(root)) {}

    ll::Expected<TypedValue> ScriptStorage::get(const std::string& scriptId, const std::string& key) {
        std::string error;
        if (!validateId(scriptId, "script id", error) || !validateKey(key, error))
            return ll::makeStringError(error);

        return this->read(scriptPath(scriptId), key);
    }

    ll::Expected<TypedValue> ScriptStorage::set(const std::string& scriptId, const std::string& key, const TypedValue& value) {
        std::string error;
        if (!validateId(scriptId, "script id", error) || !validateKey(key, error))
            return ll::makeStringError(error);

        return this->write(scriptPath(scriptId), key, value);
    }

    ll::Expected<bool> ScriptStorage::has(const std::string& scriptId, const std::string& key) {
        std::string error;
        if (!validateId(scriptId, "script id", error) || !validateKey(key, error))
            return ll::makeStringError(error);

        return this->contains(scriptPath(scriptId), key);
    }

    ll::Expected<bool> ScriptStorage::remove(const std::string& scriptId, const std::string& key) {
        std::string error;
        if (!validateId(scriptId, "script id", error) || !validateKey(key, error))
            return ll::makeStringError(error);

        return this->erase(scriptPath(scriptId), key);
    }

    ll::Expected<ArrayRef> ScriptStorage::keys(const std::string& scriptId) {
        std::string error;
        if (!validateId(scriptId, "script id", error))
            return ll::makeStringError(error);

        return this->names(scriptPath(scriptId));
    }

    ll::Expected<void> ScriptStorage::clear(const std::string& scriptId) {
        std::string error;
        if (!validateId(scriptId, "script id", error))
            return ll::makeStringError(error);

        return this->eraseAll(scriptPath(scriptId));
    }

    ll::Expected<TypedValue> ScriptStorage::playerGet(const std::string& playerId, const std::string& scriptId, const std::string& key) {
        std::string error;
        if (!validateId(playerId, "player id", error) || !validateId(scriptId, "script id", error) || !validateKey(key, error))
            return ll::makeStringError(error);

        return this->read(playerPath(playerId, scriptId), key);
    }

    ll::Expected<TypedValue> ScriptStorage::playerSet(const std::string& playerId, const std::string& scriptId, const std::string& key, const TypedValue& value) {
        std::string error;
        if (!validateId(playerId, "player id", error) || !validateId(scriptId, "script id", error) || !validateKey(key, error))
            return ll::makeStringError(error);

        return this->write(playerPath(playerId, scriptId), key, value);
    }

    ll::Expected<bool> ScriptStorage::playerHas(const std::string& playerId, const std::string& scriptId, const std::string& key) {
        std::string error;
        if (!validateId(playerId, "player id", error) || !validateId(scriptId, "script id", error) || !validateKey(key, error))
            return ll::makeStringError(error);

        return this->contains(playerPath(playerId, scriptId), key);
    }

    ll::Expected<bool> ScriptStorage::playerRemove(const std::string& playerId, const std::string& scriptId, const std::string& key) {
        std::string error;
        if (!validateId(playerId, "player id", error) || !validateId(scriptId, "script id", error) || !validateKey(key, error))
            return ll::makeStringError(error);

        return this->erase(playerPath(playerId, scriptId), key);
    }

    ll::Expected<ArrayRef> ScriptStorage::playerKeys(const std::string& playerId, const std::string& scriptId) {
        std::string error;
        if (!validateId(playerId, "player id", error) || !validateId(scriptId, "script id", error))
            return ll::makeStringError(error);

        return this->names(playerPath(playerId, scriptId));
    }

    ll::Expected<void> ScriptStorage::playerClear(const std::string& playerId, const std::string& scriptId) {
        std::string error;
        if (!validateId(playerId, "player id", error) || !validateId(scriptId, "script id", error))
            return ll::makeStringError(error);

        return this->eraseAll(playerPath(playerId, scriptId));
    }

    ll::Expected<std::shared_ptr<JsonStorage>> ScriptStorage::store(const std::string& relative) {
        std::lock_guard lock(this->mMutex);

        if (const auto it = this->mStores.find(relative); it != this->mStores.end())
            return it->second;

        const std::filesystem::path path = this->mRoot / relative;

        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            const auto size = std::filesystem::file_size(path, ec);
            if (!ec && size > kMaxFileBytes)
                return ll::makeStringError("Script storage file is too large: " + path.string());
        }

        auto storage = std::make_shared<JsonStorage>(path);
        if (auto loaded = storage->load(); !loaded)
            return ll::makeStringError("Failed to load script storage: " + loaded.error().message());

        if (this->mStores.size() >= kMaxStores)
            this->mStores.erase(this->mStores.begin());

        this->mStores.emplace(relative, storage);

        return storage;
    }

    ll::Expected<TypedValue> ScriptStorage::read(const std::string& relative, const std::string& key) {
        auto storage = this->store(relative);
        if (!storage)
            return ll::Unexpected(storage.error());

        auto json = storage.value()->get<nlohmann::ordered_json>(key);
        if (!json)
            return TypedValue{ std::monostate{} };

        return fromJson(*json, 0);
    }

    ll::Expected<TypedValue> ScriptStorage::write(const std::string& relative, const std::string& key, const TypedValue& value) {
        std::string error;
        auto json = toJson(value, error, 0);
        if (!json)
            return ll::makeStringError(error);

        const std::string encoded = json->dump();
        if (encoded.size() > kMaxValueBytes)
            return ll::makeStringError("Storage value is too large");

        auto storage = this->store(relative);
        if (!storage)
            return ll::Unexpected(storage.error());

        try {
            auto projected = storage.value()->get();
            projected[key] = *json;

            if (projected.dump().size() > kMaxFileBytes)
                return ll::makeStringError("Script storage file would be too large");
        } catch (const std::exception& e) {
            return ll::makeStringError(std::string("Failed to encode script storage: ") + e.what());
        }

        storage.value()->set(key, std::move(*json));
        if (auto saved = storage.value()->save(); !saved)
            return ll::makeStringError("Failed to save script storage: " + saved.error().message());

        return value;
    }

    ll::Expected<bool> ScriptStorage::contains(const std::string& relative, const std::string& key) {
        auto storage = this->store(relative);
        if (!storage)
            return ll::Unexpected(storage.error());

        return storage.value()->has(key);
    }

    ll::Expected<bool> ScriptStorage::erase(const std::string& relative, const std::string& key) {
        auto storage = this->store(relative);
        if (!storage)
            return ll::Unexpected(storage.error());

        if (!storage.value()->has(key))
            return false;

        storage.value()->remove(key);
        if (auto saved = storage.value()->save(); !saved)
            return ll::makeStringError("Failed to save script storage: " + saved.error().message());

        return true;
    }

    ll::Expected<ArrayRef> ScriptStorage::names(const std::string& relative) {
        auto storage = this->store(relative);
        if (!storage)
            return ll::Unexpected(storage.error());

        auto result = std::make_shared<ArrayValue>();
        for (auto& key : storage.value()->keys())
            result->elements.emplace_back(std::move(key));

        return result;
    }

    ll::Expected<void> ScriptStorage::eraseAll(const std::string& relative) {
        auto storage = this->store(relative);
        if (!storage)
            return ll::Unexpected(storage.error());

        storage.value()->write(nlohmann::ordered_json::object());
        if (auto saved = storage.value()->save(); !saved)
            return ll::makeStringError("Failed to save script storage: " + saved.error().message());

        return {};
    }
}
