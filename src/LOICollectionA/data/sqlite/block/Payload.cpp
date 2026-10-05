#include <bit>
#include <limits>
#include <string>
#include <type_traits>

#include "LOICollectionA/utils/core/Bytes.h"

#include "LOICollectionA/data/sqlite/block/Payload.h"

using namespace LOICollection::utils;

void PayloadWriter::write(LOICollection::PropKey key, std::int64_t value) {
    Bytes::append<std::int32_t>(mData, key);
    mData.push_back(static_cast<std::byte>(PayloadType::Int));
    Bytes::append<std::int64_t>(mData, value);
}

void PayloadWriter::write(LOICollection::PropKey key, double value) {
    Bytes::append<std::int32_t>(mData, key);
    mData.push_back(static_cast<std::byte>(PayloadType::Double));
    Bytes::append(mData, value);
}

void PayloadWriter::write(LOICollection::PropKey key, std::string_view value) {
    Bytes::append<std::int32_t>(mData, key);
    mData.push_back(static_cast<std::byte>(PayloadType::Text));
    if (value.size() > static_cast<size_t>(std::numeric_limits<std::int32_t>::max()))
        value = value.substr(0, static_cast<size_t>(std::numeric_limits<std::int32_t>::max()));
    Bytes::append<std::int32_t>(mData, static_cast<std::int32_t>(value.size()));
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
    auto key = Bytes::read<std::int32_t>(it, mData.end());
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
            auto v = Bytes::read<std::int64_t>(it, mData.end());
            if (!v)
                return std::nullopt;
            
            field.intValue = *v;
            break;
        }
        case PayloadType::Double: {
            auto v = Bytes::read<double>(it, mData.end());
            if (!v)
                return std::nullopt;

            field.realValue = *v;
            break;
        }
        case PayloadType::Text: {
            auto len = Bytes::read<std::int32_t>(it, mData.end());
            if (!len || *len < 0)
                return std::nullopt;

            auto n = static_cast<size_t>(*len);
            if (static_cast<size_t>(mData.end() - it) < n)
                return std::nullopt;

            field.textValue = std::string_view(&*it, n);
            it += static_cast<long long>(n);
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
