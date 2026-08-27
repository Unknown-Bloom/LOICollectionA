#pragma once

#include <any>
#include <string>
#include <unordered_map>
#include <utility>

namespace LOICollection::frontend {
    struct Context {
        static constexpr int kScriptIdKey = -1;

        std::unordered_map<int, std::any> params;
        std::string scriptId;

        template <typename... Args>
        Context(Args&&... args) {
            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                ((params[Is] = std::forward<Args>(args)), ...);
            }(std::make_index_sequence<sizeof...(Args)>{});
        }

        Context(const Context&) = delete;
        Context(Context&&) = delete;
        Context& operator=(const Context&) = delete;
        Context& operator=(Context&&) = delete;

        static std::string scriptIdOf(const std::unordered_map<int, std::any>& placeholders) {
            const auto it = placeholders.find(kScriptIdKey);
            return it == placeholders.end() ? std::string{} : std::any_cast<std::string>(it->second);
        }

        static std::unordered_map<int, std::any> withScriptId(std::unordered_map<int, std::any> placeholders, const std::string& scriptId) {
            if (!scriptId.empty())
                placeholders[kScriptIdKey] = scriptId;
            return placeholders;
        }
    };
}