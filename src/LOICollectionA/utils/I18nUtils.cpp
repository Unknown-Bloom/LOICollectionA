#include <string>
#include <fstream>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "LOICollectionA/utils/I18nUtils.h"

I18nUtils::I18nUtils(const std::string& path) {
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(path)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;

        nlohmann::ordered_json dataJson;
        if (std::ifstream file{entry.path()}; file.is_open())
            file >> dataJson; 

        const std::string& locale = entry.path().stem().string();
        for (const auto& [key, value] : dataJson.items())
            this->set(locale, key, value.get<std::string>());
    }
}

void I18nUtils::set(std::string_view local, std::string_view key, std::string_view value) {
    this->data[std::string(local)].insert({ std::string(key), std::string(value) });
}

bool I18nUtils::has(std::string_view local) const {
    return this->data.find(local) != this->data.end();
}

const I18nUtils::Translations* I18nUtils::findTable(std::string_view local) const {
    const auto localIt = this->data.find(local);
    return localIt == this->data.end() ? nullptr : &localIt->second;
}

std::string I18nUtils::get(std::string_view local, std::string_view key) const {
    if (const Translations* translations = this->findTable(local))
        if (const auto keyIt = translations->find(key); keyIt != translations->end())
            return keyIt->second;

    return std::string(key);
}