#pragma once

/* Test-only stand-in for LeviLamina's <ll/api/Expected.h>, backed by
 * std::expected. The standalone frontend test build compiles against
 * this shim instead of the LeviLamina SDK; the production build never
 * sees this header. */

#include <expected>
#include <string>
#include <system_error>
#include <utility>

namespace ll {
    class Error {
    public:
        Error() = default;

        explicit Error(std::string message) : mMessage(std::move(message)) {}

        [[nodiscard]] std::string message() const { return mMessage; }

    private:
        std::string mMessage;
    };

    template <typename T>
    using Expected = std::expected<T, Error>;

    inline auto Unexpected(Error error) {
        return std::unexpected(std::move(error));
    }

    inline auto makeStringError(std::string message) {
        return std::unexpected(Error(std::move(message)));
    }

    inline auto makeErrorCodeError(std::error_code ec) {
        return std::unexpected(Error(ec.message()));
    }
}
