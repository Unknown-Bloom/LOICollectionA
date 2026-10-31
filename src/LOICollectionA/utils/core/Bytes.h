#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <type_traits>
#include <vector>

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

        template <typename T>
        using RawOf = typename Raw<T>::Type;

        template <typename U>
        [[nodiscard]] constexpr U toLittle(U value) noexcept {
            if constexpr (std::endian::native == std::endian::big)
                return std::byteswap(value);
            else
                return value;
        }
    }

    template <typename T>
    void append(std::vector<std::byte>& out, T value) {
        static_assert(std::is_trivially_copyable_v<T>);

        const detail::RawOf<T> raw = detail::toLittle(std::bit_cast<detail::RawOf<T>>(value));
        const auto* bytes = reinterpret_cast<const std::byte*>(&raw);
        out.insert(out.end(), bytes, bytes + sizeof(T));
    }

    template <typename T>
    std::optional<T> read(std::string_view::const_iterator& it, std::string_view::const_iterator const& end) {
        static_assert(std::is_trivially_copyable_v<T>);

        if (static_cast<size_t>(end - it) < sizeof(T))
            return std::nullopt;

        detail::RawOf<T> raw{};
        std::memcpy(&raw, &*it, sizeof(T));
        it += static_cast<std::ptrdiff_t>(sizeof(T));

        return std::bit_cast<T>(detail::toLittle(raw));
    }
}
