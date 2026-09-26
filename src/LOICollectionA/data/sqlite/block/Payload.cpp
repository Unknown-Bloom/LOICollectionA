#include "LOICollectionA/data/sqlite/block/Payload.h"

#include <cstring>

#include <bit>

#include <limits>
#include <type_traits>

namespace {
    template <typename T>
    void appendInt(std::vector<std::byte>& out, T value) {
        static_assert(std::is_trivially_copyable_v<T>);
        using U = std::make_unsigned_t<T>;
        U raw = std::bit_cast<U>(value);
        for (size_t i = 0; i < sizeof(T); ++i)
            out.push_back(static_cast<std::byte>((raw >> (8 * i)) & 0xFF));
    }

    void appendDouble(std::vector<std::byte>& out, double value) {
        auto raw = std::bit_cast<std::uint64_t>(value);
        for (size_t i = 0; i < 8; ++i)
            out.push_back(static_cast<std::byte>((raw >> (8 * i)) & 0xFF));
    }

    template <typename T>
    std::optional<T> readInt(
        std::string_view::const_iterator& it, std::string_view::const_iterator const& end) {
        const size_t len = sizeof(T);
        if (static_cast<size_t>(end - it) < len)
            return std::nullopt;
        using U = std::make_unsigned_t<T>;
        U raw = 0;
        for (size_t i = 0; i < len; ++i)
            raw |= U(static_cast<std::uint8_t>(*it++)) << (8 * i);
        return std::bit_cast<T>(raw);
    }

    std::optional<double> readDouble(
        std::string_view::const_iterator& it, std::string_view::const_iterator const& end) {
        if (static_cast<size_t>(end - it) < 8)
            return std::nullopt;
        std::uint64_t raw = 0;
        for (size_t i = 0; i < 8; ++i)
            raw |= std::uint64_t(static_cast<std::uint8_t>(*it++)) << (8 * i);
        return std::bit_cast<double>(raw);
    }
}

void PayloadWriter::write(LOICollection::PropKey key, std::int64_t value) {
    appendInt<std::int32_t>(mData, key);
    mData.push_back(static_cast<std::byte>(PayloadType::Int));
    appendInt<std::int64_t>(mData, value);
}

void PayloadWriter::write(LOICollection::PropKey key, double value) {
    appendInt<std::int32_t>(mData, key);
    mData.push_back(static_cast<std::byte>(PayloadType::Double));
    appendDouble(mData, value);
}

void PayloadWriter::write(LOICollection::PropKey key, std::string_view value) {
    appendInt<std::int32_t>(mData, key);
    mData.push_back(static_cast<std::byte>(PayloadType::Text));
    if (value.size() > static_cast<size_t>(std::numeric_limits<std::int32_t>::max()))
        value = value.substr(0, static_cast<size_t>(std::numeric_limits<std::int32_t>::max()));
    appendInt<std::int32_t>(mData, static_cast<std::int32_t>(value.size()));
    for (char c : value)
        mData.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
}

void PayloadWriter::write(PayloadField const& field) {
    switch (field.type) {
        case PayloadType::Int: this->write(field.key, field.intValue); break;
        case PayloadType::Double: this->write(field.key, field.realValue); break;
        case PayloadType::Text: this->write(field.key, field.textValue); break;
        default: break;
    }
}

std::vector<std::byte> PayloadWriter::data() const {
    return mData;
}

PayloadReader::PayloadReader(std::string_view data) noexcept : mData(data) {}

std::optional<PayloadField> PayloadReader::readField(std::string_view::const_iterator& it) const {
    auto key = readInt<std::int32_t>(it, mData.end());
    if (!key)
        return std::nullopt;

    if (it == mData.end())
        return std::nullopt;

    PayloadType type = static_cast<PayloadType>(static_cast<std::uint8_t>(*it++));

    PayloadField field;
    field.key = *key;
    field.type = type;

    switch (type) {
        case PayloadType::Int: {
            auto v = readInt<std::int64_t>(it, mData.end());
            if (!v)
                return std::nullopt;
            field.intValue = *v;
            break;
        }
        case PayloadType::Double: {
            auto v = readDouble(it, mData.end());
            if (!v)
                return std::nullopt;
            field.realValue = *v;
            break;
        }
        case PayloadType::Text: {
            auto len = readInt<std::int32_t>(it, mData.end());
            if (!len || *len < 0)
                return std::nullopt;
            size_t n = static_cast<size_t>(*len);
            if (static_cast<size_t>(mData.end() - it) < n)
                return std::nullopt;
            field.textValue = std::string_view(&*it, n);
            it += n;
            break;
        }
        default:
            return std::nullopt;
    }

    return field;
}

std::optional<PayloadField> PayloadReader::find(LOICollection::PropKey key) const noexcept {
    auto it = mData.begin();
    for (;;) {
        auto field = readField(it);
        if (!field)
            return std::nullopt;
        if (field->key == key)
            return field;
    }
}

std::string PayloadReader::materializeText(PayloadField const& field) const {
    return std::string(field.textValue);
}