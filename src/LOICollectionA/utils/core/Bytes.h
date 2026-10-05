#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <optional>
#include <type_traits>
#include <string_view>

namespace LOICollection::utils::Bytes {
    namespace detail {
        template <typename T, typename = void>
        struct Raw {
            using Type = std::make_unsigned_t<T>;
        };

        template <typename T>
        struct Raw<T, std::enable_if_t<std::is_floating_point_v<T>>> {
            using Type = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
        };
    }

    template <typename T>
    void append(std::vector<std::byte>& out, T value) {
        static_assert(std::is_trivially_copyable_v<T>);

        using U = typename detail::Raw<T>::Type;

        const U raw = std::bit_cast<U>(value);
        for (size_t i = 0; i < sizeof(T); ++i)
            out.push_back(static_cast<std::byte>((raw >> (8 * i)) & 0xFF));
    }

    template <typename T>
    std::optional<T> read(std::string_view::const_iterator& it, std::string_view::const_iterator const& end) {
        static_assert(std::is_trivially_copyable_v<T>);

        const size_t len = sizeof(T);
        if (static_cast<size_t>(end - it) < len)
            return std::nullopt;

        using U = typename detail::Raw<T>::Type;

        U raw = 0;
        for (size_t i = 0; i < len; ++i)
            raw |= U(static_cast<std::uint8_t>(*it++)) << (8 * i);

        return std::bit_cast<T>(raw);
    }
}
