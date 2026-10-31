#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

class I18nUtils final {
public:
    struct StringHash {
        using is_transparent = void;

        std::size_t operator()(std::string_view value) const noexcept {
            return std::hash<std::string_view>{}(value);
        }
    };

    struct StringEqual {
        using is_transparent = void;

        bool operator()(std::string_view left, std::string_view right) const noexcept {
            return left == right;
        }
    };

    using Translations = std::unordered_map<std::string, std::string, StringHash, StringEqual>;
    using Table = std::unordered_map<std::string, Translations, StringHash, StringEqual>;

    inline static std::unique_ptr<I18nUtils>& getInstance() {
        static std::unique_ptr<I18nUtils> instance;
        return instance;
    }

    explicit I18nUtils(const std::string& path);
    ~I18nUtils() = default;

    void set(std::string_view local, std::string_view key, std::string_view value);

    [[nodiscard]] bool has(std::string_view local) const;

    [[nodiscard]] const Translations* findTable(std::string_view local) const;

    [[nodiscard]] std::string get(std::string_view local, std::string_view key) const;
    
public:
    Table data;
    std::string defaultLocale{};
};

namespace I18nUtilsTools {
    inline std::string tr(std::string_view local, std::string_view key) {
        const I18nUtils& i18n = *I18nUtils::getInstance();

        if (const I18nUtils::Translations* translations = i18n.findTable(local)) {
            if (const auto it = translations->find(key); it != translations->end())
                return it->second;

            return std::string(key);
        }

        if (const I18nUtils::Translations* fallback = i18n.findTable(i18n.defaultLocale))
            if (const auto it = fallback->find(key); it != fallback->end())
                return it->second;

        return std::string(key);
    }
}