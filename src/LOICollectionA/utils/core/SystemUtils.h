#pragma once

#include <string>
#include <vector>

namespace SystemUtils {
    std::string getCurrentTimestamp();
    std::string getNowTime(const std::string& format = "%Y-%m-%d %H:%M:%S");
    std::string getTimeSpan(const std::string& str, const std::string& target, const std::string& defaultValue = "");
    std::string toFormatTime(const std::string& str, const std::string& defaultValue = "");
    std::string toFormatSecond(const std::string& str, const std::string& defaultValue = "");
    std::string toTimeCalculate(const std::string& str, int seconds, const std::string& defaultValue = "");

    std::vector<std::string> getIntersection(const std::vector<std::vector<std::string>>& elements);

    int toInt(const std::string& str, int defaultValue = 0);
    long long toLongLong(const std::string& str, long long defaultValue = 0);
    double toDouble(const std::string& str, double defaultValue = 0.0);
        
    bool isPastOrPresent(const std::string& str);
}