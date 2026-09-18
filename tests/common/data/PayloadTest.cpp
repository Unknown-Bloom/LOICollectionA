#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "LOICollectionA/data/sqlite/block/Payload.h"

namespace {
    std::string_view asView(std::vector<std::byte> const& data) {
        return std::string_view(reinterpret_cast<char const*>(data.data()), data.size());
    }
}

TEST(PayloadTest, RoundTripKeepsTypeAndValue) {
    PayloadWriter writer;
    writer.write(1, std::string_view("hello"));
    writer.write(2, std::int64_t(42));
    writer.write(3, 3.5);

    auto data = writer.data();
    ASSERT_FALSE(data.empty());

    PayloadReader reader(asView(data));

    auto text = reader.find(1);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(text->type, PayloadType::Text);
    EXPECT_EQ(reader.materializeText(*text), "hello");

    auto number = reader.find(2);
    ASSERT_TRUE(number.has_value());
    EXPECT_EQ(number->type, PayloadType::Int);
    EXPECT_EQ(number->intValue, 42);

    auto real = reader.find(3);
    ASSERT_TRUE(real.has_value());
    EXPECT_EQ(real->type, PayloadType::Double);
    EXPECT_DOUBLE_EQ(real->realValue, 3.5);
}

TEST(PayloadTest, FindMissingKeyReturnsEmpty) {
    PayloadWriter writer;
    writer.write(7, std::string_view("value"));

    auto data = writer.data();
    PayloadReader reader(asView(data));

    EXPECT_FALSE(reader.find(8).has_value());
    EXPECT_TRUE(reader.find(7).has_value());
}

TEST(PayloadTest, ForEachVisitsFieldsInWriteOrder) {
    PayloadWriter writer;
    writer.write(11, std::string_view("first"));
    writer.write(12, std::string_view("second"));
    writer.write(13, std::string_view("third"));

    auto data = writer.data();
    PayloadReader reader(asView(data));

    std::vector<int> keys;
    std::vector<std::string> values;

    reader.forEach([&](PayloadField const& field) -> void {
        keys.emplace_back(field.key);
        values.emplace_back(reader.materializeText(field));
    });

    EXPECT_EQ(keys, (std::vector<int>{ 11, 12, 13 }));
    EXPECT_EQ(values, (std::vector<std::string>{ "first", "second", "third" }));
}

TEST(PayloadTest, EmptyPayloadHasNoFields) {
    PayloadWriter writer;
    auto data = writer.data();

    PayloadReader reader(asView(data));

    std::size_t visited = 0;
    reader.forEach([&](PayloadField const&) -> void { ++visited; });

    EXPECT_EQ(visited, 0);
}
