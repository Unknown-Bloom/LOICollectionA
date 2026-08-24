#pragma once

#include <string>
#include <utility>

#include <ll/api/event/Event.h>

#include "LOICollectionA/base/Macro.h"

namespace LOICollection::server::Events {
    class MarketItemSoldEvent final : public ll::event::Event {
    protected:
        std::string mItemName;
        int mPrice;
        int mTax;
        std::string mBuyerUuid;
        std::string mSellerUuid;
        long long mTime;

    public:
        explicit MarketItemSoldEvent(
            std::string itemName,
            int price,
            int tax,
            std::string buyerUuid,
            std::string sellerUuid,
            long long time
        ) : mItemName(std::move(itemName)),
            mPrice(price),
            mTax(tax),
            mBuyerUuid(std::move(buyerUuid)),
            mSellerUuid(std::move(sellerUuid)),
            mTime(time) {}

        LOICOLLECTION_A_NDAPI std::string getItemName() const;
        LOICOLLECTION_A_NDAPI int getPrice() const;
        LOICOLLECTION_A_NDAPI int getTax() const;
        LOICOLLECTION_A_NDAPI std::string getBuyerUuid() const;
        LOICOLLECTION_A_NDAPI std::string getSellerUuid() const;
        LOICOLLECTION_A_NDAPI long long getTime() const;
    };
}
