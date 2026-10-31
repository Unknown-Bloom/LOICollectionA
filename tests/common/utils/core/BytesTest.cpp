#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "LOICollectionA/utils/core/Bytes.h"

using LOICollection::utils::Bytes::append;
using LOICollection::utils::Bytes::read;

namespace bytes_test_support {
    std::vector<std::uint8_t> valuesOf(const std::vector<std::byte>& data) {
        std::vector<std::uint8_t> out;
        out.reserve(data.size());
        for (const std::byte byte : data)
            out.push_back(static_cast<std::uint8_t>(byte));

        return out;
    }

    template <typename T>
    std::optional<T> readOne(const std::vector<std::byte>& data, size_t& consumed) {
        const std::string_view view(reinterpret_cast<const char*>(data.data()), data.size());
        auto it = view.begin();

        const auto value = read<T>(it, view.end());
        consumed = static_cast<size_t>(it - view.begin());
        return value;
    }
}

TEST(BytesTest, Int32IsWrittenLittleEndian) {
    std::vector<std::byte> data;
    append<std::int32_t>(data, 0x01020304);

    const std::vector<std::uint8_t> expected{ 0x04, 0x03, 0x02, 0x01 };
    EXPECT_EQ(bytes_test_support::valuesOf(data), expected);
}

TEST(BytesTest, Int64IsWrittenLittleEndian) {
    std::vector<std::byte> data;
    append<std::int64_t>(data, 0x0102030405060708);

    const std::vector<std::uint8_t> expected{ 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01 };
    EXPECT_EQ(bytes_test_support::valuesOf(data), expected);
}

TEST(BytesTest, NegativeValuesKeepTwosComplement) {
    std::vector<std::byte> data;
    append<std::int32_t>(data, -2);

    const std::vector<std::uint8_t> expected{ 0xfe, 0xff, 0xff, 0xff };
    EXPECT_EQ(bytes_test_support::valuesOf(data), expected);
}

TEST(BytesTest, DoubleMatchesItsBitPattern) {
    std::vector<std::byte> data;
    append(data, 1.0);

    const std::uint64_t bits = std::bit_cast<std::uint64_t>(1.0);
    ASSERT_EQ(data.size(), sizeof(bits));
    for (size_t i = 0; i < sizeof(bits); ++i)
        EXPECT_EQ(static_cast<std::uint8_t>(data[i]), static_cast<std::uint8_t>(bits >> (8 * i))) << "byte " << i;
}

TEST(BytesTest, RoundTripEveryWidth) {
    std::vector<std::byte> data;
    append<std::int32_t>(data, 0x7f010203);
    append<std::int64_t>(data, -1234567890123LL);
    append(data, 3.141592653589793);
    append<std::uint8_t>(data, 0xfe);

    auto it = std::string_view(reinterpret_cast<const char*>(data.data()), data.size()).begin();
    const auto view = std::string_view(reinterpret_cast<const char*>(data.data()), data.size());
    const auto end = view.end();

    EXPECT_EQ(read<std::int32_t>(it, end).value_or(0), 0x7f010203);
    EXPECT_EQ(read<std::int64_t>(it, end).value_or(0), -1234567890123LL);
    EXPECT_DOUBLE_EQ(read<double>(it, end).value_or(0.0), 3.141592653589793);
    EXPECT_EQ(read<std::uint8_t>(it, end).value_or(0), 0xfe);
    EXPECT_EQ(it, end);
}

TEST(BytesTest, ReadAdvancesByExactWidth) {
    std::vector<std::byte> data;
    append<std::int32_t>(data, 42);
    append<std::int64_t>(data, 43);

    size_t consumed = 0;
    EXPECT_EQ(bytes_test_support::readOne<std::int32_t>(data, consumed).value_or(0), 42);
    EXPECT_EQ(consumed, sizeof(std::int32_t));
}

TEST(BytesTest, TruncatedReadFailsWithoutAdvancing) {
    std::vector<std::byte> data;
    append<std::int32_t>(data, 1);

    const auto view = std::string_view(reinterpret_cast<const char*>(data.data()), data.size());
    auto it = view.begin();
    const auto before = it;

    EXPECT_FALSE(read<std::int64_t>(it, view.end()).has_value());
    EXPECT_EQ(it, before);

    EXPECT_TRUE(read<std::int32_t>(it, view.end()).has_value());
    EXPECT_EQ(it, view.end());
    EXPECT_FALSE(read<std::uint8_t>(it, view.end()).has_value());
}

TEST(BytesTest, EmptyBufferReadFails) {
    const std::string_view view;
    auto it = view.begin();

    EXPECT_FALSE(read<std::uint8_t>(it, view.end()).has_value());
    EXPECT_FALSE(read<std::int32_t>(it, view.end()).has_value());
}

TEST(BytesTest, FloatAndDoubleSizesArePreserved) {
    std::vector<std::byte> data;
    append(data, 1.5f);
    append(data, 2.5);

    EXPECT_EQ(data.size(), sizeof(float) + sizeof(double));

    const auto view = std::string_view(reinterpret_cast<const char*>(data.data()), data.size());
    auto it = view.begin();

    EXPECT_FLOAT_EQ(read<float>(it, view.end()).value_or(0.0f), 1.5f);
    EXPECT_DOUBLE_EQ(read<double>(it, view.end()).value_or(0.0), 2.5);
    EXPECT_EQ(it, view.end());
}
