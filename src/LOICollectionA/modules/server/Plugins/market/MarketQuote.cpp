#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <ll/api/Expected.h>
#include <ll/api/io/Logger.h>

#include "LOICollectionA/coro/TimerManager.h"

#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/data/SQLiteStorage.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/market/MarketQuote.h"

namespace LOICollection::server::Plugins {
    namespace {
        constexpr long long DAY_SECONDS = 86'400LL;
        constexpr long long WINDOW_7D = 7LL * DAY_SECONDS;
        constexpr long long WINDOW_30D = 30LL * DAY_SECONDS;

        int normalizeDays(int days) {
            return days <= 7 ? 7 : 30;
        }

        struct QuoteStat {
            int count7d = 0;
            int count30d = 0;
            long long volume7d = 0;
            long long volume30d = 0;
            long long tax7d = 0;
            long long tax30d = 0;
            int validCount7d = 0;
            long long validSum7d = 0;
            int validCount30d = 0;
            long long validSum30d = 0;
            int min30d = 0;
            int max30d = 0;
            int lastPrice = 0;
            long long lastTime = 0;
        };
    }

    struct MarketQuote::Impl {
        std::shared_ptr<SQLiteStorage> db;
        const Config::C_Market& options;
        std::shared_ptr<ll::io::Logger> logger;
        TimerManager& timerManager;

        std::unordered_map<std::string, QuoteStat> stats;
        std::unordered_set<std::string> activeSellers7d;
        std::unordered_set<std::string> activeSellers30d;

        bool isOutlier(const QuoteStat& stat, int price) const {
            if (stat.validCount30d <= 0)
                return false;

            double average = static_cast<double>(stat.validSum30d) / stat.validCount30d;

            return MarketQuote::isPriceOutlier(average, price, this->options.StorePriceOutlierRatio);
        }

        void mergeStat(QuoteStat& stat, int price, int tax, long long time, bool outlier, bool within7d) const {
            stat.count30d++;
            stat.volume30d += price;
            stat.tax30d += tax;

            if (within7d) {
                stat.count7d++;
                stat.volume7d += price;
                stat.tax7d += tax;
            }

            if (stat.min30d <= 0 || price < stat.min30d)
                stat.min30d = price;
            if (price > stat.max30d)
                stat.max30d = price;

            if (time > stat.lastTime) {
                stat.lastTime = time;
                stat.lastPrice = price;
            }

            if (!outlier) {
                stat.validCount30d++;
                stat.validSum30d += price;
                if (within7d) {
                    stat.validCount7d++;
                    stat.validSum7d += price;
                }
            }
        }
    };

    ll::Expected<void> MarketQuote::start() {
        if (!this->mImpl->options.StoreQuoteEnabled)
            return {};

        return this->rebuild().and_then([this]() -> ll::Expected<void> {
            int minutes = this->mImpl->options.StoreQuoteRefreshMinutes;
            if (minutes <= 0)
                return {};

            this->mImpl->timerManager.loopSchedule("MarketQuoteRefresh", std::chrono::minutes(minutes), [this]() -> void {
                this->rebuild().or_else([this](ll::Error e) -> ll::Expected<void> {
                    this->mImpl->logger->warn("MarketQuote: refresh failed: {}", e.message());

                    return ll::Unexpected(e);
                });
            });

            return {};
        });
    }

    ll::Expected<void> MarketQuote::rebuild() {
        auto loadTable = [this](const std::string& table) -> ll::Expected<std::unordered_map<std::string, std::unordered_map<std::string, std::string>>> {
            return this->mImpl->db->list(table)
                .and_then([this, table](const std::vector<std::string>& keys) -> ll::Expected<std::unordered_map<std::string, std::unordered_map<std::string, std::string>>> {
                    return this->mImpl->db->get(table, keys);
                });
        };

        return loadTable("Store")
            .and_then([this](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> stores) -> ll::Expected<std::unordered_map<std::string, std::string>> {
                std::unordered_map<std::string, std::string> owners;
                owners.reserve(stores.size());
                for (const auto& [key, row] : stores)
                    owners[key] = row.at("owner_uuid");

                return owners;
            })
            .and_then([this, loadTable](std::unordered_map<std::string, std::string> owners) -> ll::Expected<void> {
                return loadTable("StoreSale")
                    .transform([this, owners](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> sales) mutable -> void {
                        std::unordered_map<std::string, QuoteStat> rebuilt;
                        std::unordered_set<std::string> sellers7d;
                        std::unordered_set<std::string> sellers30d;

                        std::string nowTime = SystemUtils::getNowTime();

                        for (const auto& [key, row] : sales) {
                            long long age = SystemUtils::toInt(SystemUtils::getTimeSpan(nowTime, row.at("time"), ""), -1);
                            if (age < 0 || age > WINDOW_30D)
                                continue;

                            const std::string& itemName = row.at("item_name");
                            int price = SystemUtils::toInt(row.at("price"), 0);
                            int tax = SystemUtils::toInt(row.contains("tax") ? row.at("tax") : "0", 0);
                            long long time = SystemUtils::toLongLong(key, 0);
                            bool within7d = age <= WINDOW_7D;

                            QuoteStat& stat = rebuilt[itemName];
                            bool selfBuy = row.contains("buyer_uuid") && row.contains("seller_uuid") &&
                                !row.at("buyer_uuid").empty() && row.at("buyer_uuid") == row.at("seller_uuid");
                            bool outlier = this->mImpl->isOutlier(stat, price);
                            this->mImpl->mergeStat(stat, price, tax, time, outlier || selfBuy, within7d);

                            std::string seller = row.contains("seller_uuid") ? row.at("seller_uuid") : "";
                            if (seller.empty()) {
                                auto it = owners.find(row.at("store_id"));
                                if (it != owners.end())
                                    seller = it->second;
                            }
                            if (seller.empty())
                                continue;

                            if (within7d)
                                sellers7d.insert(seller);
                            sellers30d.insert(seller);
                        }

                        this->mImpl->stats = std::move(rebuilt);
                        this->mImpl->activeSellers7d = std::move(sellers7d);
                        this->mImpl->activeSellers30d = std::move(sellers30d);
                    });
            });
    }

    void MarketQuote::onItemSold(const std::string& itemName, int price, int tax, long long time, const std::string& sellerUuid, const std::string& buyerUuid) {
        QuoteStat& stat = this->mImpl->stats[itemName];

        bool selfBuy = !buyerUuid.empty() && buyerUuid == sellerUuid;
        bool outlier = this->mImpl->isOutlier(stat, price);
        this->mImpl->mergeStat(stat, price, tax, time, outlier || selfBuy, true);

        if (!sellerUuid.empty())
            this->mImpl->activeSellers7d.insert(sellerUuid);
        if (!sellerUuid.empty())
            this->mImpl->activeSellers30d.insert(sellerUuid);
    }

    ll::Expected<std::optional<QuoteInfo>> MarketQuote::getQuote(const std::string& itemName) const {
        auto it = this->mImpl->stats.find(itemName);
        if (it == this->mImpl->stats.end())
            return std::nullopt;

        const QuoteStat& stat = it->second;
        QuoteInfo info;
        info.avg7d = stat.validCount7d > 0 ? static_cast<int>(stat.validSum7d / stat.validCount7d) : 0;
        info.avg30d = stat.validCount30d > 0 ? static_cast<int>(stat.validSum30d / stat.validCount30d) : 0;
        info.min30d = stat.min30d;
        info.max30d = stat.max30d;
        info.count30d = stat.count30d;
        info.lastPrice = stat.lastPrice;

        return info;
    }

    ll::Expected<std::vector<std::pair<std::string, long long>>> MarketQuote::getTopVolume(int limit, int days) const {
        int window = normalizeDays(days);

        std::vector<std::pair<std::string, long long>> items;
        items.reserve(this->mImpl->stats.size());
        for (const auto& [itemName, stat] : this->mImpl->stats)
            items.emplace_back(itemName, window == 7 ? stat.count7d : stat.count30d);

        std::sort(items.begin(), items.end(), [](const auto& left, const auto& right) -> bool {
            if (left.second != right.second)
                return left.second > right.second;

            return left.first < right.first;
        });

        if (limit > 0 && static_cast<int>(items.size()) > limit)
            items.resize(limit);

        return items;
    }

    ll::Expected<std::vector<std::pair<std::string, long long>>> MarketQuote::getTopTurnover(int limit, int days) const {
        int window = normalizeDays(days);

        std::vector<std::pair<std::string, long long>> items;
        items.reserve(this->mImpl->stats.size());
        for (const auto& [itemName, stat] : this->mImpl->stats)
            items.emplace_back(itemName, window == 7 ? stat.volume7d : stat.volume30d);

        std::sort(items.begin(), items.end(), [](const auto& left, const auto& right) -> bool {
            if (left.second != right.second)
                return left.second > right.second;

            return left.first < right.first;
        });

        if (limit > 0 && static_cast<int>(items.size()) > limit)
            items.resize(limit);

        return items;
    }

    ll::Expected<QuoteReport> MarketQuote::getReport(int days) const {
        int window = normalizeDays(days);

        QuoteReport report;
        for (const auto& [itemName, stat] : this->mImpl->stats) {
            if (window == 7) {
                report.count += stat.count7d;
                report.turnover += stat.volume7d;
                report.tax += stat.tax7d;
            } else {
                report.count += stat.count30d;
                report.turnover += stat.volume30d;
                report.tax += stat.tax30d;
            }
        }

        report.activeSellers = window == 7
            ? static_cast<long long>(this->mImpl->activeSellers7d.size())
            : static_cast<long long>(this->mImpl->activeSellers30d.size());

        return report;
    }

    bool MarketQuote::isPriceOutlier(double average, int price, double ratio) {
        if (ratio <= 0.0 || average <= 0.0)
            return false;

        return static_cast<double>(price) > average * ratio ||
            static_cast<double>(price) < average / ratio;
    }

    MarketQuote::MarketQuote(
        std::shared_ptr<SQLiteStorage> db,
        const Config::C_Market& options,
        std::shared_ptr<ll::io::Logger> logger,
        TimerManager& timerManager
    ) : mImpl(std::make_unique<Impl>(
            std::move(db),
            options,
            std::move(logger),
            timerManager
        )) {}

    MarketQuote::~MarketQuote() = default;
}
