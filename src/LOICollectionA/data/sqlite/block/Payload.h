#pragma once

#include <cstddef>

#include <cstdint>

#include <string>
#include <string_view>
#include <vector>

#include <optional>
#include <utility>

#include "LOICollectionA/base/Macro.h"

namespace LOICollection {
    using PropKey = std::int32_t;
}

enum class PayloadType : std::uint8_t {
    Null = 0,
    Int = 1,
    Double = 2,
    Text = 3
};

struct PayloadField {
    LOICollection::PropKey key = 0;
    PayloadType type = PayloadType::Null;
    std::int64_t intValue = 0;
    double realValue = 0.0;
    std::string_view textValue;
};

class PayloadWriter {
public:
    PayloadWriter() = default;

    LOICOLLECTION_A_API void write(LOICollection::PropKey key, std::int64_t value);
    LOICOLLECTION_A_API void write(LOICollection::PropKey key, double value);
    LOICOLLECTION_A_API void write(LOICollection::PropKey key, std::string_view value);
    LOICOLLECTION_A_API void write(PayloadField const& field);

    LOICOLLECTION_A_NDAPI std::vector<std::byte> data() const;

private:
    std::vector<std::byte> mData;
};

class PayloadReader {
public:
    explicit PayloadReader(std::string_view data) noexcept;

    [[nodiscard]] std::optional<PayloadField> find(LOICollection::PropKey key) const noexcept;
    [[nodiscard]] std::string materializeText(PayloadField const& field) const;

    template <typename F>
    void forEach(F&& f) const {
        auto it = mData.begin();
        for (;;) {
            if (auto ok = readField(it); ok) {
                PayloadField field = *ok;
                std::string owned;
                if (field.type == PayloadType::Text) {
                    owned.assign(field.textValue);
                    field.textValue = owned;
                }
                std::forward<F>(f)(field);
            } else {
                break;
            }
        }
    }

private:
    std::optional<PayloadField> readField(std::string_view::const_iterator& it) const;

    std::string_view mData;
};