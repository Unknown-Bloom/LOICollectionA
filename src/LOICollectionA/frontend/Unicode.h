#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace LOICollection::frontend {
    [[nodiscard]] inline size_t codepointWidth(char lead) {
        auto byte = static_cast<unsigned char>(lead);

        if ((byte & 0xF8) == 0xF0) return 4;
        if ((byte & 0xF0) == 0xE0) return 3;
        if ((byte & 0xE0) == 0xC0) return 2;

        return 1;
    }

    [[nodiscard]] inline size_t codepointCount(const std::string& text) {
        size_t count = 0;
        for (size_t i = 0; i < text.size(); i += codepointWidth(text[i]))
            ++count;

        return count;
    }

    [[nodiscard]] inline size_t codepointDistance(const std::string& text, size_t offset) {
        size_t count = 0;
        for (size_t i = 0; i < offset && i < text.size(); i += codepointWidth(text[i]))
            ++count;

        return count;
    }

    [[nodiscard]] inline size_t codepointOffset(const std::string& text, size_t index) {
        size_t seen = 0;
        for (size_t i = 0; i < text.size(); i += codepointWidth(text[i])) {
            if (seen == index)
                return i;

            ++seen;
        }

        return std::string::npos;
    }

    [[nodiscard]] inline std::optional<std::string> codepointAt(const std::string& text, size_t index) {
        size_t offset = codepointOffset(text, index);
        if (offset == std::string::npos)
            return std::nullopt;

        return text.substr(offset, codepointWidth(text[offset]));
    }
}
