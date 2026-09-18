#pragma once

#include <string>

namespace JsonError {
    enum class JsonStorageErrorCode : int {
        KeyNotFound = 1,
        TypeMismatch = 2
    };

    struct JsonStorageErrorCategory : std::error_category {
        [[nodiscard]] const char* name() const noexcept override {
            return "JsonStorageError";
        }
        
        [[nodiscard]] std::string message(int ev) const override {
            switch (static_cast<JsonStorageErrorCode>(ev)) {
                case JsonStorageErrorCode::KeyNotFound: return "Key not found";
                case JsonStorageErrorCode::TypeMismatch: return "Type mismatch";
            }

            return "Unknown";
        }
    };

    inline std::error_code makeErrorCode(JsonStorageErrorCode e) {
        static JsonStorageErrorCategory cat;
        return std::error_code{ static_cast<int>(e), cat };
    }
}
