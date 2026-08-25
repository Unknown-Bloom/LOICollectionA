#include <memory>
#include <string>

#include <ll/api/event/Emitter.h>
#include <ll/api/event/EmitterBase.h>

#include "LOICollectionA/include/server/Events/modules/WalletTransferEvent.h"

namespace LOICollection::server::Events {
    std::string WalletTransferEvent::getFromUuid() const {
        return mFromUuid;
    }

    std::string WalletTransferEvent::getFromName() const {
        return mFromName;
    }

    std::string WalletTransferEvent::getToUuid() const {
        return mToUuid;
    }

    std::string WalletTransferEvent::getToName() const {
        return mToName;
    }

    long long WalletTransferEvent::getAmount() const {
        return mAmount;
    }

    long long WalletTransferEvent::getFee() const {
        return mFee;
    }

    std::string WalletTransferEvent::getType() const {
        return mType;
    }

    long long WalletTransferEvent::getTime() const {
        return mTime;
    }

    static std::unique_ptr<ll::event::EmitterBase> WalletTransferEmitterFactory();
    class WalletTransferEventEmitter : public ll::event::Emitter<WalletTransferEmitterFactory, WalletTransferEvent> {};

    static std::unique_ptr<ll::event::EmitterBase> WalletTransferEmitterFactory() {
        return std::make_unique<WalletTransferEventEmitter>();
    }
}
