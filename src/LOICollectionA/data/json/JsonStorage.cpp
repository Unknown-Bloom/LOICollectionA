#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <string_view>
#include <shared_mutex>

#include <nlohmann/json.hpp>

#include <ll/api/Expected.h>

#include "LOICollectionA/data/json/JsonStorage.h"

JsonStorage::JsonStorage(std::filesystem::path path) : mPath(std::move(path)){}
JsonStorage::~JsonStorage() = default;

ll::Expected<void> JsonStorage::load() {
    std::unique_lock lock(this->mMutex);

    std::error_code ec;
    if (!std::filesystem::exists(mPath, ec)) {
        if (ec) 
            return ll::makeErrorCodeError(ec);

        std::error_code dir_ec;
        std::filesystem::create_directories(mPath.parent_path(), dir_ec);
        if (dir_ec) 
            return ll::makeErrorCodeError(dir_ec);

        this->mJson = nlohmann::ordered_json::object();

        std::ofstream file(this->mPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
            return ll::makeErrorCodeError(std::make_error_code(std::errc::permission_denied));
        
        file << this->mJson.dump(4);

        return {};
    }

    std::ifstream file(mPath, std::ios::binary);
    if (!file.is_open())
        return ll::makeErrorCodeError(std::make_error_code(std::errc::permission_denied));

    try {
        file >> this->mJson;
    } catch (const nlohmann::json::parse_error& e) {
        return ll::makeExceptionError(std::make_exception_ptr(e));
    }

    return {};
}

void JsonStorage::write(const nlohmann::ordered_json& json) {
    std::unique_lock lock(this->mMutex);
    
    this->mJson = json;
}

void JsonStorage::remove(std::string_view key) {
    std::unique_lock lock(this->mMutex);

    this->mJson.erase(key);
}

void JsonStorage::remove_ptr(std::string_view ptr) {
    std::unique_lock lock(this->mMutex);

    nlohmann::json_pointer<std::string> ptrs((std::string(ptr)));
    if (!this->mJson.contains(ptrs)) 
        return;

    auto& parent = this->mJson.at(ptrs.parent_pointer());
    parent.erase(ptrs.back());
}

bool JsonStorage::has(std::string_view key) const {
    std::shared_lock lock(this->mMutex);
    
    return this->mJson.contains(key);
}

bool JsonStorage::has_ptr(std::string_view ptr) const {
    std::shared_lock lock(this->mMutex);

    nlohmann::json_pointer<std::string> ptrs((std::string(ptr)));
    return this->mJson.contains(ptrs);
}

nlohmann::ordered_json JsonStorage::get() const {
    std::shared_lock lock(this->mMutex);

    return this->mJson;
}

std::vector<std::string> JsonStorage::keys() const {
    std::shared_lock lock(this->mMutex);

    std::vector<std::string> keys;
    keys.reserve(this->mJson.size());

    for (const auto& item : this->mJson.items())
        keys.emplace_back(item.key());

    return keys;
}

ll::Expected<void> JsonStorage::save() const {
    std::unique_lock lock(this->mMutex);

    std::error_code ec;
    std::filesystem::create_directories(this->mPath.parent_path(), ec);
    if (ec)
        return ll::makeErrorCodeError(ec);

    std::ofstream file(this->mPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return ll::makeErrorCodeError(std::make_error_code(std::errc::permission_denied));

    file << this->mJson.dump(4);

    return {};
}
