#include <memory>
#include <string>

#include <ll/api/event/Emitter.h>
#include <ll/api/event/EmitterBase.h>

#include "LOICollectionA/include/server/Events/modules/RedEnvelopeCompletedEvent.h"

namespace LOICollection::server::Events {
    std::string RedEnvelopeCompletedEvent::getEnvelopeId() const {
        return mId;
    }

    std::string RedEnvelopeCompletedEvent::getSenderUuid() const {
        return mSenderUuid;
    }

    std::string RedEnvelopeCompletedEvent::getKingUuid() const {
        return mKingUuid;
    }

    std::string RedEnvelopeCompletedEvent::getKingName() const {
        return mKingName;
    }

    long long RedEnvelopeCompletedEvent::getKingAmount() const {
        return mKingAmount;
    }

    long long RedEnvelopeCompletedEvent::getTotal() const {
        return mTotal;
    }

    long long RedEnvelopeCompletedEvent::getTime() const {
        return mTime;
    }

    static std::unique_ptr<ll::event::EmitterBase> RedEnvelopeCompletedEmitterFactory();
    class RedEnvelopeCompletedEventEmitter : public ll::event::Emitter<RedEnvelopeCompletedEmitterFactory, RedEnvelopeCompletedEvent> {};

    static std::unique_ptr<ll::event::EmitterBase> RedEnvelopeCompletedEmitterFactory() {
        return std::make_unique<RedEnvelopeCompletedEventEmitter>();
    }
}
