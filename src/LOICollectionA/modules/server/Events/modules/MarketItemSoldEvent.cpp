#include <memory>
#include <string>

#include <ll/api/event/Emitter.h>
#include <ll/api/event/EmitterBase.h>

#include "LOICollectionA/include/server/Events/modules/MarketItemSoldEvent.h"

namespace LOICollection::server::Events {
    std::string MarketItemSoldEvent::getItemName() const {
        return mItemName;
    }

    int MarketItemSoldEvent::getPrice() const {
        return mPrice;
    }

    int MarketItemSoldEvent::getTax() const {
        return mTax;
    }

    std::string MarketItemSoldEvent::getBuyerUuid() const {
        return mBuyerUuid;
    }

    std::string MarketItemSoldEvent::getSellerUuid() const {
        return mSellerUuid;
    }

    long long MarketItemSoldEvent::getTime() const {
        return mTime;
    }

    static std::unique_ptr<ll::event::EmitterBase> MarketItemSoldEmitterFactory();
    class MarketItemSoldEventEmitter : public ll::event::Emitter<MarketItemSoldEmitterFactory, MarketItemSoldEvent> {};

    static std::unique_ptr<ll::event::EmitterBase> MarketItemSoldEmitterFactory() {
        return std::make_unique<MarketItemSoldEventEmitter>();
    }
}
