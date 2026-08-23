#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/include/server/Plugins/market/MarketType.h"

class SQLiteStorage;
class TimerManager;

namespace ll {
    namespace io {
        class Logger;
    }
}

namespace Config {
    struct C_Market;
}

namespace LOICollection::server::Plugins {
    struct QuoteInfo {
        int avg7d = 0;
        int avg30d = 0;
        int min30d = 0;
        int max30d = 0;
        int count30d = 0;
        int lastPrice = 0;
    };

    struct QuoteReport {
        long long count = 0;         // 区间成交笔数
        long long turnover = 0;      // 区间成交总额
        long long tax = 0;           // 区间税收总额
        long long activeSellers = 0; // 活跃卖家数
    };

    class MarketQuote {
    public:
        MarketQuote(
            std::shared_ptr<SQLiteStorage> db,
            const Config::C_Market& options,
            std::shared_ptr<ll::io::Logger> logger,
            TimerManager& timerManager
        );

        ~MarketQuote();

        MarketQuote(MarketQuote const&) = delete;
        MarketQuote(MarketQuote&&) = delete;
        MarketQuote& operator=(MarketQuote const&) = delete;
        MarketQuote& operator=(MarketQuote&&) = delete;

    public:
        LOICOLLECTION_A_NDAPI ll::Expected<void> start();

        LOICOLLECTION_A_NDAPI ll::Expected<void> rebuild();

        LOICOLLECTION_A_API void onItemSold(const std::string& itemName, int price, int tax, long long time, const std::string& sellerUuid, const std::string& buyerUuid);

        LOICOLLECTION_A_NDAPI ll::Expected<std::optional<QuoteInfo>> getQuote(const std::string& itemName) const;

        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::pair<std::string, long long>>> getTopVolume(int limit, int days = 30) const;

        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::pair<std::string, long long>>> getTopTurnover(int limit, int days = 30) const;

        LOICOLLECTION_A_NDAPI ll::Expected<QuoteReport> getReport(int days) const;

        // 离群判定：偏离有效均价超过 ratio 倍的成交不计入均价（仍计入成交量）；ratio <= 0 或均价为 0 时关闭
        LOICOLLECTION_A_NDAPI static bool isPriceOutlier(double average, int price, double ratio);

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
