#pragma once

#include <any>
#include <string>
#include <unordered_map>
#include <utility>

namespace LOICollection::frontend {
    struct Context {
        // Reserved placeholder key carrying the current script's id (the file
        // name relative to the `gui` directory).  Negative so it never collides
        // with the positional parameter slots (0, 1, 2, ...).
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
    };
}