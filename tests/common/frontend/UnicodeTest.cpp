#include <gtest/gtest.h>

#include <string>

#include "LOICollectionA/frontend/Unicode.h"

using namespace LOICollection::frontend;

namespace unicode_test_support {
    const std::string ascii = "abc";
    const std::string cjk = "\xE4\xBD\xA0\xE5\xA5\xBD";
    const std::string emoji = "\xF0\x9F\x98\x80";
    const std::string mixed = "a" + cjk + emoji + "z";
}

TEST(UnicodeTest, CodepointWidthClassifiesLeadBytes) {
    EXPECT_EQ(codepointWidth('\0'), 1u);
    EXPECT_EQ(codepointWidth('a'), 1u);
    EXPECT_EQ(codepointWidth('\x7f'), 1u);
    EXPECT_EQ(codepointWidth(static_cast<char>(0x80)), 1u);
    EXPECT_EQ(codepointWidth(static_cast<char>(0xbf)), 1u);
    EXPECT_EQ(codepointWidth(static_cast<char>(0xc0)), 2u);
    EXPECT_EQ(codepointWidth(static_cast<char>(0xdf)), 2u);
    EXPECT_EQ(codepointWidth(static_cast<char>(0xe0)), 3u);
    EXPECT_EQ(codepointWidth(static_cast<char>(0xef)), 3u);
    EXPECT_EQ(codepointWidth(static_cast<char>(0xf0)), 4u);
    EXPECT_EQ(codepointWidth(static_cast<char>(0xf7)), 4u);
    EXPECT_EQ(codepointWidth(static_cast<char>(0xf8)), 1u);
    EXPECT_EQ(codepointWidth(static_cast<char>(0xff)), 1u);
}

TEST(UnicodeTest, CodepointCountCountsCharactersNotBytes) {
    EXPECT_EQ(codepointCount(""), 0u);
    EXPECT_EQ(codepointCount(unicode_test_support::ascii), 3u);
    EXPECT_EQ(codepointCount(unicode_test_support::cjk), 2u);
    EXPECT_EQ(codepointCount(unicode_test_support::emoji), 1u);
    EXPECT_EQ(codepointCount(unicode_test_support::mixed), 5u);
    EXPECT_EQ(unicode_test_support::cjk.size(), 6u);
}

TEST(UnicodeTest, CodepointCountTreatsBrokenLeadAsSingleCharacter) {
    EXPECT_EQ(codepointCount(std::string(1, static_cast<char>(0x80))), 1u);
    EXPECT_EQ(codepointCount(std::string(1, static_cast<char>(0xff))), 1u);
    EXPECT_EQ(codepointCount(std::string("\xE4\xBD", 2)), 1u);
}

TEST(UnicodeTest, CodepointDistanceClampsToOffset) {
    EXPECT_EQ(codepointDistance(unicode_test_support::cjk, 0), 0u);
    EXPECT_EQ(codepointDistance(unicode_test_support::cjk, 3), 1u);
    EXPECT_EQ(codepointDistance(unicode_test_support::cjk, 6), 2u);
    EXPECT_EQ(codepointDistance(unicode_test_support::cjk, 100), 2u);
    EXPECT_EQ(codepointDistance(unicode_test_support::mixed, 1), 1u);
    EXPECT_EQ(codepointDistance(unicode_test_support::mixed, 4), 2u);
}

TEST(UnicodeTest, CodepointOffsetFindsByteIndex) {
    EXPECT_EQ(codepointOffset(unicode_test_support::cjk, 0), 0u);
    EXPECT_EQ(codepointOffset(unicode_test_support::cjk, 1), 3u);
    EXPECT_EQ(codepointOffset(unicode_test_support::cjk, 2), std::string::npos);
    EXPECT_EQ(codepointOffset("", 0), std::string::npos);
    EXPECT_EQ(codepointOffset(unicode_test_support::mixed, 1), 1u);
    EXPECT_EQ(codepointOffset(unicode_test_support::mixed, 3), 7u);
    EXPECT_EQ(codepointOffset(unicode_test_support::mixed, 4), 11u);
    EXPECT_EQ(codepointOffset(unicode_test_support::mixed, 5), std::string::npos);
}

TEST(UnicodeTest, CodepointAtReturnsWholeSequence) {
    EXPECT_EQ(codepointAt(unicode_test_support::cjk, 0).value(), unicode_test_support::cjk.substr(0, 3));
    EXPECT_EQ(codepointAt(unicode_test_support::cjk, 1).value(), unicode_test_support::cjk.substr(3, 3));
    EXPECT_EQ(codepointAt(unicode_test_support::emoji, 0).value(), unicode_test_support::emoji);
    EXPECT_EQ(codepointAt(unicode_test_support::mixed, 0).value(), "a");
    EXPECT_EQ(codepointAt(unicode_test_support::mixed, 4).value(), "z");
    EXPECT_FALSE(codepointAt(unicode_test_support::cjk, 2).has_value());
    EXPECT_FALSE(codepointAt("", 0).has_value());
}

TEST(UnicodeTest, CodepointAtClampsTruncatedSequence) {
    const std::string truncated = "\xE4\xBD";

    EXPECT_EQ(codepointAt(truncated, 0).value(), truncated);
}

TEST(UnicodeTest, OffsetAndAtAgreeForEveryIndex) {
    const std::string text = unicode_test_support::mixed;
    const size_t total = codepointCount(text);

    size_t consumed = 0;
    for (size_t index = 0; index < total; ++index) {
        const size_t offset = codepointOffset(text, index);
        EXPECT_EQ(offset, consumed) << "index " << index;
        EXPECT_EQ(codepointDistance(text, offset), index);
        ASSERT_TRUE(codepointAt(text, index).has_value());
        EXPECT_EQ(codepointAt(text, index).value().size(), codepointWidth(text[offset]));
        consumed += codepointWidth(text[offset]);
    }

    EXPECT_EQ(consumed, text.size());
    EXPECT_EQ(codepointOffset(text, total), std::string::npos);
}
