#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "LOICollectionA/data/sqlite/block/Payload.h"

#include "common/data/StorageTestSupport.h"

using storage_test_support::payloadView;

namespace payload_edge_test_support {
    PayloadReader readerOf(std::vector<std::byte> const& data) {
        return PayloadReader(payloadView(data));
    }
}

TEST(PayloadEdgeTest, Int64ExtremesRoundTrip) {
    PayloadWriter writer;
    writer.write(1, std::numeric_limits<std::int64_t>::min());
    writer.write(2, std::numeric_limits<std::int64_t>::max());

    auto data = writer.data();
    auto reader = payload_edge_test_support::readerOf(data);

    auto low = reader.find(1);
    ASSERT_TRUE(low.has_value());
    EXPECT_EQ(low->type, PayloadType::Int);
    EXPECT_EQ(low->intValue, std::numeric_limits<std::int64_t>::min());

    auto high = reader.find(2);
    ASSERT_TRUE(high.has_value());
    EXPECT_EQ(high->type, PayloadType::Int);
    EXPECT_EQ(high->intValue, std::numeric_limits<std::int64_t>::max());
}

TEST(PayloadEdgeTest, NegativeKeyRoundTrips) {
    PayloadWriter writer;
    writer.write(-5, std::int64_t(7));

    auto data = writer.data();
    auto reader = payload_edge_test_support::readerOf(data);

    auto field = reader.find(-5);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->intValue, 7);
}

TEST(PayloadEdgeTest, DoubleValuesRoundTripExactly) {
    PayloadWriter writer;
    writer.write(1, 0.1);
    writer.write(2, -2.75);
    writer.write(3, 1e300);

    auto data = writer.data();
    auto reader = payload_edge_test_support::readerOf(data);

    auto first = reader.find(1);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->type, PayloadType::Double);
    EXPECT_DOUBLE_EQ(first->realValue, 0.1);

    auto second = reader.find(2);
    ASSERT_TRUE(second.has_value());
    EXPECT_DOUBLE_EQ(second->realValue, -2.75);

    auto third = reader.find(3);
    ASSERT_TRUE(third.has_value());
    EXPECT_DOUBLE_EQ(third->realValue, 1e300);
}

TEST(PayloadEdgeTest, TextWithEmbeddedNulRoundTrips) {
    PayloadWriter writer;
    writer.write(1, std::string_view("a\0b", 3));

    auto data = writer.data();
    auto reader = payload_edge_test_support::readerOf(data);

    auto field = reader.find(1);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->type, PayloadType::Text);
    EXPECT_EQ(reader.materializeText(*field), std::string_view("a\0b", 3));
}

TEST(PayloadEdgeTest, LongTextRoundTrips) {
    std::string text(100000, 'x');
    text.append("tail");

    PayloadWriter writer;
    writer.write(1, text);

    auto data = writer.data();
    auto reader = payload_edge_test_support::readerOf(data);

    auto field = reader.find(1);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(reader.materializeText(*field).size(), text.size());
    EXPECT_EQ(reader.materializeText(*field), text);
}

TEST(PayloadEdgeTest, Utf8TextRoundTrips) {
    PayloadWriter writer;
    writer.write(1, std::string_view("玩家数据"));

    auto data = writer.data();
    auto reader = payload_edge_test_support::readerOf(data);

    auto field = reader.find(1);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(reader.materializeText(*field), "玩家数据");
}

TEST(PayloadEdgeTest, FindReturnsFirstFieldForDuplicateKey) {
    PayloadWriter writer;
    writer.write(1, std::string_view("first"));
    writer.write(1, std::string_view("second"));

    auto data = writer.data();
    auto reader = payload_edge_test_support::readerOf(data);

    auto field = reader.find(1);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(reader.materializeText(*field), "first");
}

TEST(PayloadEdgeTest, ForEachYieldsEveryFieldIncludingDuplicates) {
    PayloadWriter writer;
    writer.write(1, std::string_view("first"));
    writer.write(1, std::string_view("second"));
    writer.write(2, std::int64_t(3));

    auto data = writer.data();
    auto reader = payload_edge_test_support::readerOf(data);

    int seen = 0;
    int firstKeyCount = 0;
    reader.forEach([&](PayloadField const& field) {
        ++seen;
        if (field.key == 1)
            ++firstKeyCount;
    });
    EXPECT_EQ(seen, 3);
    EXPECT_EQ(firstKeyCount, 2);
}

TEST(PayloadEdgeTest, MaterializeTextIsIndependentOfSourceBuffer) {
    PayloadWriter writer;
    writer.write(1, std::string_view("stable"));

    auto data = writer.data();
    auto reader = payload_edge_test_support::readerOf(data);

    auto field = reader.find(1);
    ASSERT_TRUE(field.has_value());

    auto text = reader.materializeText(*field);
    text.assign("mutated");
    EXPECT_EQ(reader.materializeText(*field), "stable");
}

TEST(PayloadEdgeTest, EmptyReaderYieldsNoFields) {
    PayloadReader reader{std::string_view{}};

    EXPECT_FALSE(reader.find(1).has_value());

    int seen = 0;
    reader.forEach([&seen](PayloadField const&) { ++seen; });
    EXPECT_EQ(seen, 0);
}

TEST(PayloadEdgeTest, TruncatedBuffersStopParsingWithoutCrashing) {
    PayloadWriter writer;
    writer.write(1, std::string_view("hello"));
    writer.write(2, std::int64_t(42));

    auto data = writer.data();
    for (std::size_t len = 0; len <= data.size(); ++len) {
        PayloadReader reader(payloadView(data).substr(0, len));

        int seen = 0;
        reader.forEach([&seen](PayloadField const&) { ++seen; });
        EXPECT_LE(seen, 2);

        if (auto field = reader.find(1); field.has_value())
            EXPECT_EQ(reader.materializeText(*field), "hello");
    }
}

TEST(PayloadEdgeTest, GarbageTypeByteStopsIteration) {
    PayloadWriter writer;
    writer.write(1, std::string_view("hello"));

    auto data = writer.data();
    data.push_back(static_cast<std::byte>(0xFF));

    auto reader = payload_edge_test_support::readerOf(data);

    int seen = 0;
    reader.forEach([&seen](PayloadField const&) { ++seen; });
    EXPECT_EQ(seen, 1);

    auto field = reader.find(1);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(reader.materializeText(*field), "hello");
}

TEST(PayloadEdgeTest, TextLengthBeyondBufferIsRejected) {
    PayloadWriter writer;
    writer.write(1, std::string_view("ab"));

    auto data = writer.data();
    data[8] = static_cast<std::byte>(0x7F);

    auto reader = payload_edge_test_support::readerOf(data);
    EXPECT_FALSE(reader.find(1).has_value());

    int seen = 0;
    reader.forEach([&seen](PayloadField const&) { ++seen; });
    EXPECT_EQ(seen, 0);
}
