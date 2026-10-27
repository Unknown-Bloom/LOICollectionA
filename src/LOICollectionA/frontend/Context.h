#pragma once

#include <any>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

class Player;

namespace LOICollection::frontend {
    struct Context {
        static constexpr int kScriptIdKey = -1;
        static constexpr int kOwnerIdKey = -2;

        std::unordered_map<int, std::any> params;
        std::string scriptId;
        std::string ownerId;

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
            const auto* scriptId = it == placeholders.end()
                ? nullptr : std::any_cast<std::string>(&it->second);
            return scriptId ? *scriptId : std::string{};
        }

        static std::unordered_map<int, std::any> withScriptId(std::unordered_map<int, std::any> placeholders, const std::string& scriptId) {
            if (!scriptId.empty())
                placeholders[kScriptIdKey] = scriptId;
            return placeholders;
        }

        static std::string ownerIdOf(const std::unordered_map<int, std::any>& placeholders) {
            const auto it = placeholders.find(kOwnerIdKey);
            const auto* ownerId = it == placeholders.end()
                ? nullptr : std::any_cast<std::string>(&it->second);
            return ownerId ? *ownerId : std::string{};
        }

        static std::unordered_map<int, std::any> withOwnerId(std::unordered_map<int, std::any> placeholders, const std::string& ownerId) {
            if (!ownerId.empty())
                placeholders[kOwnerIdKey] = ownerId;
            return placeholders;
        }

        static std::optional<std::reference_wrapper<Player>> playerOf(const std::unordered_map<int, std::any>& placeholders) {
            const auto it = placeholders.find(0);
            if (it == placeholders.end())
                return std::nullopt;

            const auto* player = std::any_cast<std::reference_wrapper<Player>>(&it->second);
            if (!player)
                return std::nullopt;

            return *player;
        }
    };
}
