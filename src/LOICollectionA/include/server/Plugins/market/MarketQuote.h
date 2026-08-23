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

        LOICOLLECTION_A_API void onItemSold(const std::string& itemName, int price, long long time, const std::string& sellerUuid);

        LOICOLLECTION_A_NDAPI ll::Expected<std::optional<QuoteInfo>> getQuote(const std::string& itemName) const;

        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getTopVolume(int limit) const;

        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::pair<std::string, long long>>> getTopTurnover(int limit) const;

        LOICOLLECTION_A_NDAPI ll::Expected<long long> getTotalTurnover() const;

        LOICOLLECTION_A_NDAPI ll::Expected<long long> getActiveSellers() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
