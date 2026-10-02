#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <optional>

#include <ll/api/Expected.h>

#include "LOICollectionA/coro/TimerManager.h"

#include "LOICollectionA/base/Wrapper.h"
#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/market/MarketPlugin.h"

#include "common/coro/MockExecutor.h"

using namespace LOICollection::server::Plugins;

class MarketQuoteComponentTest : public testing::Test {
protected:
    MockExecutor mExecutor;
    std::shared_ptr<TimerManager> mTimerManager;
    Config::C_Market mOptions;
    std::unique_ptr<MarketQuote> mQuote;

    void SetUp() override {
        if (!MarketPlugin::getShared()->isValid())
            GTEST_SKIP() << "MarketPlugin is not valid";

        this->mTimerManager = std::make_shared<TimerManager>(this->mExecutor);

        this->mOptions = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Market;
        this->mOptions.StorePriceOutlierRatio = 2.0;

        this->mQuote = std::make_unique<MarketQuote>(
            MarketPlugin::getShared()->getDatabase(),
            this->mOptions,
            MarketPlugin::getShared()->getLogger(),
            *this->mTimerManager
        );
    }
};

TEST(MarketQuotePureLogicTest, IsPriceOutlierBoundaries) {
    EXPECT_FALSE(MarketQuote::isPriceOutlier(100.0, 100, 2.0));
    EXPECT_FALSE(MarketQuote::isPriceOutlier(100.0, 200, 2.0));
    EXPECT_FALSE(MarketQuote::isPriceOutlier(100.0, 50, 2.0));
    EXPECT_TRUE(MarketQuote::isPriceOutlier(100.0, 201, 2.0));
    EXPECT_TRUE(MarketQuote::isPriceOutlier(100.0, 49, 2.0));

    EXPECT_FALSE(MarketQuote::isPriceOutlier(100.0, 100, 1.0));
    EXPECT_TRUE(MarketQuote::isPriceOutlier(100.0, 101, 1.0));
    EXPECT_TRUE(MarketQuote::isPriceOutlier(100.0, 99, 1.0));

    EXPECT_TRUE(MarketQuote::isPriceOutlier(100.0, 0, 2.0));
    EXPECT_TRUE(MarketQuote::isPriceOutlier(100.0, -10, 2.0));

    EXPECT_FALSE(MarketQuote::isPriceOutlier(0.0, 1000, 2.0));
    EXPECT_FALSE(MarketQuote::isPriceOutlier(-1.0, 1000, 2.0));
    EXPECT_FALSE(MarketQuote::isPriceOutlier(100.0, 1000, 0.0));
    EXPECT_FALSE(MarketQuote::isPriceOutlier(100.0, 1000, -1.0));
}

TEST_F(MarketQuoteComponentTest, EmptyComponentReturnsNoQuoteAndEmptyRanking) {
    auto quote = this->mQuote->getQuote("MarketQuoteTestMissingItem");
    ASSERT_TRUE(quote.has_value());
    EXPECT_FALSE(quote.value().has_value());

    auto volume = this->mQuote->getTopVolume(0, 7);
    ASSERT_TRUE(volume.has_value());
    EXPECT_TRUE(volume.value().empty());

    auto turnover = this->mQuote->getTopTurnover(0, 30);
    ASSERT_TRUE(turnover.has_value());
    EXPECT_TRUE(turnover.value().empty());

    auto report = this->mQuote->getReport(30);
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report.value().count, 0LL);
    EXPECT_EQ(report.value().turnover, 0LL);
    EXPECT_EQ(report.value().tax, 0LL);
    EXPECT_EQ(report.value().activeSellers, 0LL);
}

TEST_F(MarketQuoteComponentTest, GetQuoteAggregatesCountsBoundsAndLastPrice) {
    std::string item = "MarketQuoteTestItemA";

    this->mQuote->onItemSold(item, 100, 5, 1000, "seller_a", "buyer_a");
    this->mQuote->onItemSold(item, 101, 6, 2000, "seller_a", "buyer_a");

    auto quote = this->mQuote->getQuote(item);
    ASSERT_TRUE(quote.has_value());
    ASSERT_TRUE(quote.value().has_value());

    EXPECT_EQ(quote.value()->count30d, 2);
    EXPECT_EQ(quote.value()->avg7d, 100);
    EXPECT_EQ(quote.value()->avg30d, 100);
    EXPECT_EQ(quote.value()->min30d, 100);
    EXPECT_EQ(quote.value()->max30d, 101);
    EXPECT_EQ(quote.value()->lastPrice, 101);

    this->mQuote->onItemSold(item, 102, 7, 500, "seller_a", "buyer_a");

    quote = this->mQuote->getQuote(item);
    ASSERT_TRUE(quote.has_value());
    ASSERT_TRUE(quote.value().has_value());

    EXPECT_EQ(quote.value()->count30d, 3);
    EXPECT_EQ(quote.value()->avg7d, 101);
    EXPECT_EQ(quote.value()->avg30d, 101);
    EXPECT_EQ(quote.value()->min30d, 100);
    EXPECT_EQ(quote.value()->max30d, 102);
    EXPECT_EQ(quote.value()->lastPrice, 101);

    this->mQuote->onItemSold(item, 105, 8, 3000, "seller_a", "buyer_a");

    quote = this->mQuote->getQuote(item);
    ASSERT_TRUE(quote.has_value());
    ASSERT_TRUE(quote.value().has_value());

    EXPECT_EQ(quote.value()->count30d, 4);
    EXPECT_EQ(quote.value()->avg7d, 102);
    EXPECT_EQ(quote.value()->avg30d, 102);
    EXPECT_EQ(quote.value()->min30d, 100);
    EXPECT_EQ(quote.value()->max30d, 105);
    EXPECT_EQ(quote.value()->lastPrice, 105);
}

TEST_F(MarketQuoteComponentTest, SelfBuyIsExcludedFromAveragesButCountedEverywhereElse) {
    std::string item = "MarketQuoteTestItemB";

    this->mQuote->onItemSold(item, 100, 5, 1000, "seller_b", "buyer_b");
    this->mQuote->onItemSold(item, 150, 5, 2000, "seller_b", "seller_b");

    auto quote = this->mQuote->getQuote(item);
    ASSERT_TRUE(quote.has_value());
    ASSERT_TRUE(quote.value().has_value());

    EXPECT_EQ(quote.value()->avg7d, 100);
    EXPECT_EQ(quote.value()->avg30d, 100);
    EXPECT_EQ(quote.value()->count30d, 2);
    EXPECT_EQ(quote.value()->min30d, 100);
    EXPECT_EQ(quote.value()->max30d, 150);
    EXPECT_EQ(quote.value()->lastPrice, 150);

    std::string emptyBuyerItem = "MarketQuoteTestItemB2";

    this->mQuote->onItemSold(emptyBuyerItem, 100, 0, 1000, "seller_c", "");
    this->mQuote->onItemSold(emptyBuyerItem, 150, 0, 2000, "seller_c", "");

    auto emptyBuyerQuote = this->mQuote->getQuote(emptyBuyerItem);
    ASSERT_TRUE(emptyBuyerQuote.has_value());
    ASSERT_TRUE(emptyBuyerQuote.value().has_value());

    EXPECT_EQ(emptyBuyerQuote.value()->avg7d, 125);
    EXPECT_EQ(emptyBuyerQuote.value()->count30d, 2);
}

TEST_F(MarketQuoteComponentTest, OutlierIsExcludedFromAveragesButCountedEverywhereElse) {
    std::string item = "MarketQuoteTestItemC";

    this->mQuote->onItemSold(item, 100, 1, 1000, "seller_d", "buyer_d");
    this->mQuote->onItemSold(item, 100, 1, 2000, "seller_d", "buyer_d");
    this->mQuote->onItemSold(item, 100, 1, 3000, "seller_d", "buyer_d");
    this->mQuote->onItemSold(item, 1000, 1, 4000, "seller_d", "buyer_d");

    auto quote = this->mQuote->getQuote(item);
    ASSERT_TRUE(quote.has_value());
    ASSERT_TRUE(quote.value().has_value());

    EXPECT_EQ(quote.value()->avg7d, 100);
    EXPECT_EQ(quote.value()->avg30d, 100);
    EXPECT_EQ(quote.value()->count30d, 4);
    EXPECT_EQ(quote.value()->min30d, 100);
    EXPECT_EQ(quote.value()->max30d, 1000);
    EXPECT_EQ(quote.value()->lastPrice, 1000);

    this->mQuote->onItemSold(item, 110, 1, 5000, "seller_d", "buyer_d");

    quote = this->mQuote->getQuote(item);
    ASSERT_TRUE(quote.has_value());
    ASSERT_TRUE(quote.value().has_value());

    EXPECT_EQ(quote.value()->avg7d, 102);
    EXPECT_EQ(quote.value()->avg30d, 102);
    EXPECT_EQ(quote.value()->count30d, 5);
    EXPECT_EQ(quote.value()->max30d, 1000);
    EXPECT_EQ(quote.value()->lastPrice, 110);
}

TEST_F(MarketQuoteComponentTest, RankingByVolumeAndTurnoverWithNameTieBreak) {
    this->mQuote->onItemSold("MarketQuoteTestRankA", 100, 0, 1000, "seller_r", "buyer_r");
    this->mQuote->onItemSold("MarketQuoteTestRankA", 100, 0, 2000, "seller_r", "buyer_r");
    this->mQuote->onItemSold("MarketQuoteTestRankA", 100, 0, 3000, "seller_r", "buyer_r");

    this->mQuote->onItemSold("MarketQuoteTestRankB", 500, 0, 1000, "seller_r", "buyer_r");
    this->mQuote->onItemSold("MarketQuoteTestRankB", 500, 0, 2000, "seller_r", "buyer_r");

    this->mQuote->onItemSold("MarketQuoteTestRankC", 100, 0, 1000, "seller_r", "buyer_r");

    this->mQuote->onItemSold("MarketQuoteTestRankD", 500, 0, 1000, "seller_r", "buyer_r");

    auto volume = this->mQuote->getTopVolume(0, 7);
    ASSERT_TRUE(volume.has_value());
    EXPECT_EQ(
        volume.value(),
        (std::vector<std::pair<std::string, long long>>{
            { "MarketQuoteTestRankA", 3 },
            { "MarketQuoteTestRankB", 2 },
            { "MarketQuoteTestRankC", 1 },
            { "MarketQuoteTestRankD", 1 }
        })
    );

    auto turnover = this->mQuote->getTopTurnover(0, 7);
    ASSERT_TRUE(turnover.has_value());
    EXPECT_EQ(
        turnover.value(),
        (std::vector<std::pair<std::string, long long>>{
            { "MarketQuoteTestRankB", 1000 },
            { "MarketQuoteTestRankD", 500 },
            { "MarketQuoteTestRankA", 300 },
            { "MarketQuoteTestRankC", 100 }
        })
    );

    auto limited = this->mQuote->getTopVolume(2, 7);
    ASSERT_TRUE(limited.has_value());
    ASSERT_EQ(limited.value().size(), 2u);
    EXPECT_EQ(limited.value().at(0).first, "MarketQuoteTestRankA");
    EXPECT_EQ(limited.value().at(1).first, "MarketQuoteTestRankB");
}

TEST_F(MarketQuoteComponentTest, ReportAggregatesCountsTurnoverTaxAndActiveSellers) {
    this->mQuote->onItemSold("MarketQuoteTestReportA", 100, 5, 1000, "seller_one", "buyer_one");
    this->mQuote->onItemSold("MarketQuoteTestReportA", 100, 5, 2000, "seller_two", "buyer_two");
    this->mQuote->onItemSold("MarketQuoteTestReportA", 100, 5, 3000, "seller_one", "buyer_three");
    this->mQuote->onItemSold("MarketQuoteTestReportB", 500, 25, 1000, "seller_one", "buyer_one");
    this->mQuote->onItemSold("MarketQuoteTestReportB", 500, 25, 2000, "seller_one", "buyer_one");
    this->mQuote->onItemSold("MarketQuoteTestReportC", 100, 7, 1000, "", "buyer_one");
    this->mQuote->onItemSold("MarketQuoteTestReportD", 200, 10, 1000, "seller_four", "seller_four");

    auto report = this->mQuote->getReport(7);
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report.value().count, 7LL);
    EXPECT_EQ(report.value().turnover, 1600LL);
    EXPECT_EQ(report.value().tax, 82LL);
    EXPECT_EQ(report.value().activeSellers, 3LL);
}

TEST_F(MarketQuoteComponentTest, DayWindowNormalisationGroupsDaysIntoSevenOrThirty) {
    this->mQuote->onItemSold("MarketQuoteTestWindowA", 100, 5, 1000, "seller_w", "buyer_w");
    this->mQuote->onItemSold("MarketQuoteTestWindowA", 100, 5, 2000, "seller_w", "buyer_w");
    this->mQuote->onItemSold("MarketQuoteTestWindowB", 300, 15, 1000, "seller_w", "buyer_w");

    auto weekReport = this->mQuote->getReport(7);
    auto monthReport = this->mQuote->getReport(30);
    auto zeroReport = this->mQuote->getReport(0);
    auto negativeReport = this->mQuote->getReport(-5);

    ASSERT_TRUE(weekReport.has_value());
    ASSERT_TRUE(monthReport.has_value());
    ASSERT_TRUE(zeroReport.has_value());
    ASSERT_TRUE(negativeReport.has_value());

    EXPECT_EQ(weekReport.value().count, 3LL);
    EXPECT_EQ(weekReport.value().turnover, 500LL);
    EXPECT_EQ(weekReport.value().tax, 25LL);
    EXPECT_EQ(weekReport.value().activeSellers, 1LL);

    EXPECT_EQ(monthReport.value().count, weekReport.value().count);
    EXPECT_EQ(monthReport.value().turnover, weekReport.value().turnover);
    EXPECT_EQ(monthReport.value().tax, weekReport.value().tax);
    EXPECT_EQ(monthReport.value().activeSellers, weekReport.value().activeSellers);

    EXPECT_EQ(zeroReport.value().count, weekReport.value().count);
    EXPECT_EQ(zeroReport.value().turnover, weekReport.value().turnover);
    EXPECT_EQ(zeroReport.value().activeSellers, weekReport.value().activeSellers);

    EXPECT_EQ(negativeReport.value().count, weekReport.value().count);
    EXPECT_EQ(negativeReport.value().turnover, weekReport.value().turnover);

    auto weekVolume = this->mQuote->getTopVolume(0, 7);
    auto monthVolume = this->mQuote->getTopVolume(0, 30);
    auto eightDayVolume = this->mQuote->getTopVolume(0, 8);

    ASSERT_TRUE(weekVolume.has_value());
    ASSERT_TRUE(monthVolume.has_value());
    ASSERT_TRUE(eightDayVolume.has_value());

    EXPECT_EQ(weekVolume.value(), monthVolume.value());
    EXPECT_EQ(weekVolume.value(), eightDayVolume.value());

    auto weekTurnover = this->mQuote->getTopTurnover(0, 7);
    auto monthTurnover = this->mQuote->getTopTurnover(0, 30);

    ASSERT_TRUE(weekTurnover.has_value());
    ASSERT_TRUE(monthTurnover.has_value());

    EXPECT_EQ(weekTurnover.value(), monthTurnover.value());
}
