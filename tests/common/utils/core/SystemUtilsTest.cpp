#include <gtest/gtest.h>

#include <algorithm>
#include <unordered_set>

#include "LOICollectionA/utils/core/SystemUtils.h"

TEST(SystemUtilsTest, ToIntValidNumber) {
    EXPECT_EQ(SystemUtils::toInt("123", -1), 123);
    EXPECT_EQ(SystemUtils::toInt("-456"), -456);
    EXPECT_EQ(SystemUtils::toInt("0"), 0);
}

TEST(SystemUtilsTest, ToIntInvalidStringReturnsDefault) {
    EXPECT_EQ(SystemUtils::toInt("abc", 10), 10);
    EXPECT_EQ(SystemUtils::toInt("12.3", -1), -1);
    EXPECT_EQ(SystemUtils::toInt("", 99), 99);
}

TEST(SystemUtilsTest, ToDoubleValidNumber) {
    EXPECT_DOUBLE_EQ(SystemUtils::toDouble("0.05", -1.0), 0.05);
    EXPECT_DOUBLE_EQ(SystemUtils::toDouble("1.5"), 1.5);
    EXPECT_DOUBLE_EQ(SystemUtils::toDouble("0"), 0.0);
    EXPECT_DOUBLE_EQ(SystemUtils::toDouble("-2.75", 0.0), -2.75);
}

TEST(SystemUtilsTest, ToDoubleInvalidStringReturnsDefault) {
    EXPECT_DOUBLE_EQ(SystemUtils::toDouble("abc", 10.0), 10.0);
    EXPECT_DOUBLE_EQ(SystemUtils::toDouble("", -1.0), -1.0);
    EXPECT_DOUBLE_EQ(SystemUtils::toDouble("12..3", -1.0), -1.0);
    EXPECT_DOUBLE_EQ(SystemUtils::toDouble("0.05x", -1.0), -1.0);
}

TEST(SystemUtilsTest, GetIntersection) {
    std::vector<std::vector<std::string>> input = {
        {"a", "b", "c", "d"},
        {"b", "c", "e"},
        {"c", "b", "f"}
    };

    auto result = SystemUtils::getIntersection(input);
    std::unordered_set<std::string> resultSet(result.begin(), result.end());

    EXPECT_EQ(resultSet.size(), 2);
    EXPECT_TRUE(resultSet.contains("b"));
    EXPECT_TRUE(resultSet.contains("c"));
}

TEST(SystemUtilsTest, GetIntersectionEmpty) {
    std::vector<std::vector<std::string>> input = {
        {"a", "b"},
        {"c", "d"}
    };

    auto result = SystemUtils::getIntersection(input);
    EXPECT_TRUE(result.empty());
}

TEST(SystemUtilsTest, ToFormatTimeValid) {
    EXPECT_EQ(SystemUtils::toFormatTime("20231225120000"), "2023-12-25 12:00:00");
    EXPECT_EQ(SystemUtils::toFormatTime("19990101000001"), "1999-01-01 00:00:01");
}

TEST(SystemUtilsTest, ToFormatTimeInvalidReturnsDefault) {
    EXPECT_EQ(SystemUtils::toFormatTime("notatime", "DEFAULT"), "DEFAULT");
    EXPECT_EQ(SystemUtils::toFormatTime("20231301120000", "ERROR"), "ERROR");
}

TEST(SystemUtilsTest, GetTimeSpan) {
    std::string span = SystemUtils::getTimeSpan("2024-01-01 01:00:00", "2024-01-01 00:00:00", "");
    EXPECT_EQ(span, "3600");

    span = SystemUtils::getTimeSpan("2024-01-02 00:00:00", "2024-01-01 00:00:00", "");
    EXPECT_EQ(span, "86400");
}

TEST(SystemUtilsTest, GetTimeSpanEndBeforeStartReturnsDefault) {
    std::string span = SystemUtils::getTimeSpan("2024-01-01 00:00:00", "2024-01-02 00:00:00", "INVALID");
    EXPECT_EQ(span, "INVALID");
}

TEST(SystemUtilsTest, GetTimeSpanInvalidFormatReturnsDefault) {
    EXPECT_EQ(SystemUtils::getTimeSpan("abc", "2024-01-01 00:00:00", "ERR"), "ERR");
    EXPECT_EQ(SystemUtils::getTimeSpan("2024-01-01 00:00:00", "xyz", "ERR"), "ERR");
}

TEST(SystemUtilsTest, ToFormatSecond) {
    std::string res = SystemUtils::toFormatSecond("3725", "");

    EXPECT_NE(res.find("1h"), std::string::npos);
    EXPECT_NE(res.find("2min"), std::string::npos);
    EXPECT_NE(res.find("5s"), std::string::npos);

    EXPECT_EQ(SystemUtils::toFormatSecond("0"), "");
}

TEST(SystemUtilsTest, ToFormatSecondInvalidStringReturnsDefault) {
    EXPECT_EQ(SystemUtils::toFormatSecond("abc", "N/A"), "N/A");
    EXPECT_EQ(SystemUtils::toFormatSecond("-5", "N/A"), "N/A");
}

TEST(SystemUtilsTest, ToTimeCalculate) {
    std::string result = SystemUtils::toTimeCalculate("2024-01-01 12:00:00", 60, "");
    EXPECT_EQ(result, "20240101120100");

    result = SystemUtils::toTimeCalculate("2024-12-31 23:59:59", 2, "");
    EXPECT_EQ(result, "20250101000001");
}

TEST(SystemUtilsTest, ToTimeCalculateInvalidInput) {
    EXPECT_EQ(SystemUtils::toTimeCalculate("bad time", 60, "FAIL"), "FAIL");
}

TEST(SystemUtilsTest, IsPastOrPresentHistoricDateReturnsTrue) {
    EXPECT_TRUE(SystemUtils::isPastOrPresent("20000101000000"));
}

TEST(SystemUtilsTest, IsPastOrPresentFarFutureDateReturnsFalse) {
    EXPECT_FALSE(SystemUtils::isPastOrPresent("20991231235959"));
}

TEST(SystemUtilsTest, IsPastOrPresentInvalidFormatReturnsFalse) {
    EXPECT_FALSE(SystemUtils::isPastOrPresent("not_a_date"));
}

TEST(SystemUtilsTest, GetCurrentTimestampIsNumeric) {
    std::string ts = SystemUtils::getCurrentTimestamp();

    EXPECT_FALSE(ts.empty());
    EXPECT_TRUE(std::all_of(ts.begin(), ts.end(), ::isdigit));
}

TEST(SystemUtilsTest, GetNowTimeDefaultFormat) {
    std::string now = SystemUtils::getNowTime();

    EXPECT_EQ(now.size(), 19);
    EXPECT_EQ(now[4], '-');
    EXPECT_EQ(now[7], '-');
    EXPECT_EQ(now[10], ' ');
    EXPECT_EQ(now[13], ':');
    EXPECT_EQ(now[16], ':');
}

TEST(SystemUtilsTest, GetNowTimeCustomFormat) {
    std::string now = SystemUtils::getNowTime("%H:%M");

    EXPECT_EQ(now.size(), 5);
    EXPECT_EQ(now[2], ':');
}
