#pragma once

#include <vector>
#include <string>
#include <filesystem>
#include <string_view>
#include <type_traits>
#include <shared_mutex>

#include <nlohmann/json.hpp>

#include <ll/api/Expected.h>

#include "LOICollectionA/data/json/JsonError.h"

#include "LOICollectionA/base/Macro.h"

class JsonStorage {
public:
    LOICOLLECTION_A_API   explicit JsonStorage(std::filesystem::path path);
    LOICOLLECTION_A_API   ~JsonStorage();

    LOICOLLECTION_A_NDAPI ll::Expected<void> load();

    LOICOLLECTION_A_API   void write(const nlohmann::ordered_json& json);

    LOICOLLECTION_A_API   void remove(std::string_view key);
    LOICOLLECTION_A_API   void remove_ptr(std::string_view ptr);

    LOICOLLECTION_A_NDAPI bool has(std::string_view key) const;
    LOICOLLECTION_A_NDAPI bool has_ptr(std::string_view ptr) const;

    LOICOLLECTION_A_NDAPI nlohmann::ordered_json get() const;

    LOICOLLECTION_A_NDAPI std::vector<std::string> keys() const;

    template <typename T>
    requires std::is_default_constructible<T>::value
    [[nodiscard]] ll::Expected<T> get(std::string_view key) const {
        std::shared_lock lock(this->mMutex);

        try {
            return this->mJson.at(key).get<T>();
        } catch (const nlohmann::json::out_of_range&) {
            return ll::makeErrorCodeError(JsonError::makeErrorCode(JsonError::JsonStorageErrorCode::KeyNotFound));
        } catch (const nlohmann::json::type_error&) {
            return ll::makeErrorCodeError(JsonError::makeErrorCode(JsonError::JsonStorageErrorCode::TypeMismatch));
        }
    }

    template <typename T>
    requires std::is_default_constructible<T>::value
    [[nodiscard]] ll::Expected<T> get_ptr(std::string_view ptr) const {
        std::shared_lock lock(this->mMutex);

        nlohmann::json_pointer<std::string> ptrs{ std::string(ptr) };

        try {
            return this->mJson.at(ptrs).get<T>();
        } catch (const nlohmann::json::out_of_range&) {
            return ll::makeErrorCodeError(JsonError::makeErrorCode(JsonError::JsonStorageErrorCode::KeyNotFound));
        } catch (const nlohmann::json::type_error&) {
            return ll::makeErrorCodeError(JsonError::makeErrorCode(JsonError::JsonStorageErrorCode::TypeMismatch));
        }
    }

    template <typename T>
    void set(std::string_view key, T value) {
        std::unique_lock lock(this->mMutex);

        this->mJson[key] = std::forward<T>(value);
    }

    template <typename T>
    void set_ptr(std::string_view ptr, T value) {
        std::unique_lock lock(this->mMutex);

        nlohmann::json_pointer<std::string> ptrs{ std::string(ptr) };
        this->mJson[ptrs] = std::forward<T>(value);
    }

    LOICOLLECTION_A_NDAPI ll::Expected<void> save() const;

private:
    mutable std::shared_mutex mMutex;

    std::filesystem::path mPath;

    nlohmann::ordered_json mJson;
};
