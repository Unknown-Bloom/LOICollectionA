#pragma once

namespace LOICollection::server::Plugins {
    enum class MarketTradeType {
        sell,
        buy
    };

    enum class MarketStoreReviewStatus {
        pending,
        approved,
        rejected
    };

    struct StoreScoreInput {
        double ageDays = 0.0;
        int transactions30 = 0;
        long long volume30 = 0;
        int approvedReviews = 0;
        double approvedAverage = 0.0;
        int approved180 = 0;
        int badReviews30 = 0;
        int reviews30 = 0;
        double globalApprovedAverage = 0.0;
    };
}
