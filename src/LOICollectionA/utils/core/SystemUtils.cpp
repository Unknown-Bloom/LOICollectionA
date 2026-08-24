#include <chrono>
#include <string>
#include <sstream>
#include <charconv>
#include <algorithm>
#include <unordered_set>

#include "LOICollectionA/utils/core/SystemUtils.h"

namespace SystemUtils {
    std::string getCurrentTimestamp() {
        auto mTimeNow = std::chrono::system_clock::now();
        return std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(mTimeNow.time_since_epoch()).count());
    }

    std::string getNowTime(const std::string& format) {
        auto mTimeNow = std::chrono::system_clock::now();
        auto mTimeNowSecond = std::chrono::floor<std::chrono::seconds>(mTimeNow); 
        auto zt = std::chrono::zoned_time(std::chrono::current_zone(), mTimeNowSecond);
        return std::vformat("{:" + format + "}", std::make_format_args(zt));
    }

    std::string getTimeSpan(const std::string& str, const std::string& target, const std::string& defaultValue) {
        std::chrono::local_seconds mTp;
        if ((std::istringstream(str) >> std::chrono::parse("%Y-%m-%d %H:%M:%S", mTp)).fail())
            return defaultValue;
        
        std::chrono::local_seconds mTargetTp;
        if ((std::istringstream(target) >> std::chrono::parse("%Y-%m-%d %H:%M:%S", mTargetTp)).fail())
            return defaultValue;
        
        auto mDuration = mTp - mTargetTp;

        if (mDuration.count() < 0)
            return defaultValue;

        return std::to_string(std::chrono::duration_cast<std::chrono::seconds>(mDuration).count());
    }

    std::string toFormatTime(const std::string& str, const std::string& defaultValue) {
        std::chrono::local_seconds mTp;
        if ((std::istringstream(str) >> std::chrono::parse("%Y%m%d%H%M%S", mTp)).fail())
            return defaultValue;
        
        return std::format("{:%Y-%m-%d %H:%M:%S}", mTp);
    }

    std::string toFormatSecond(const std::string& str, const std::string& defaultValue) {
        long long seconds;
        auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), seconds);

        if (ec != std::errc() || ptr != str.data() + str.size())
            return defaultValue;

        if (seconds <= 0)
            return defaultValue;

        static const long long minute = 60, hour = 3600, day = 86400, month = 2592000, year = 31536000;
        long long yt = seconds / year, mn = (seconds %= year) / month, dt = (seconds %= month) / day, 
                ht = (seconds %= day) / hour, mt = (seconds %= hour) / minute; seconds %= minute;
        
        std::string result;
        auto append = [&result](const std::string& str, long long value) -> void {
            if (value > 0) result += std::to_string(value) + str + " ";
        };

        append("y", yt);
        append("m", mn);
        append("d", dt);
        append("h", ht);
        append("min", mt);
        append("s", seconds);
        
        return result;
    }

    std::string toTimeCalculate(const std::string& str, int seconds, const std::string& defaultValue) {
        std::chrono::local_seconds mTp;
        if ((std::istringstream(str) >> std::chrono::parse("%Y-%m-%d %H:%M:%S", mTp)).fail())
            return defaultValue;
        
        mTp += std::chrono::seconds(seconds);
        return std::format("{:%Y%m%d%H%M%S}", mTp);
    }

    std::vector<std::string> getIntersection(const std::vector<std::vector<std::string>>& elements) {
        auto it = std::min_element(elements.begin(), elements.end(), [](const auto& left, const auto& right) -> bool {
            return left.size() < right.size();
        });

        std::unordered_set<std::string> mCommonSet(it->begin(), it->end());
        for (auto iter = std::next(elements.begin()); iter != elements.end() && !mCommonSet.empty(); ++iter) {
            const std::unordered_set<std::string> mCurrentSet(iter->begin(), iter->end());
            std::erase_if(mCommonSet, [&](const auto& element) -> bool {
                return !mCurrentSet.contains(element);
            });
        }

        return { mCommonSet.begin(), mCommonSet.end() };
    }

    int toInt(const std::string& str, int defaultValue) {
        int result;
        auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), result);

        return (ec == std::errc() && ptr == str.data() + str.size()) ? result : defaultValue;
    }

    long long toLongLong(const std::string& str, long long defaultValue) {
        long long result;
        auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), result);

        return (ec == std::errc() && ptr == str.data() + str.size()) ? result : defaultValue;
    }

    double toDouble(const std::string& str, double defaultValue) {
        double result;
        auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), result);

        return (ec == std::errc() && ptr == str.data() + str.size()) ? result : defaultValue;
    }

    bool isPastOrPresent(const std::string& str) {
        std::chrono::local_seconds mLocalTp;
        if ((std::istringstream(str) >> std::chrono::parse("%Y%m%d%H%M%S", mLocalTp)).fail())
            return false;
        
        auto mTargetSysTp = std::chrono::current_zone()->to_sys(mLocalTp);
        auto mNowSysTp = std::chrono::system_clock::now();
        return mTargetSysTp <= mNowSysTp;
    }
}