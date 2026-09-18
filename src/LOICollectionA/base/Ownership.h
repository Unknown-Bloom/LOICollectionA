#pragma once

#include <memory>

#include <type_traits>

#include <utility>

template <typename T>
using observer = T*;

template <typename T, typename... Args>
[[nodiscard]] std::unique_ptr<T> makeOwned(Args&&... args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
}

template <typename T, typename... Args>
[[nodiscard]] std::shared_ptr<T> makeShared(Args&&... args) {
    return std::make_shared<T>(std::forward<Args>(args)...);
}

static_assert(std::is_same_v<observer<int>, int*>);