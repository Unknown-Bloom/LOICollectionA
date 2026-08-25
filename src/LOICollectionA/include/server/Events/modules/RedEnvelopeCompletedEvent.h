#pragma once

#include <string>
#include <utility>

#include <ll/api/event/Event.h>

#include "LOICollectionA/base/Macro.h"

namespace LOICollection::server::Events {
    class RedEnvelopeCompletedEvent final : public ll::event::Event {
    protected:
        std::string mId;
        std::string mSenderUuid;
        std::string mKingUuid;
        std::string mKingName;
        int mKingAmount;
        int mTotal;
        long long mTime;

    public:
        explicit RedEnvelopeCompletedEvent(
            std::string id,
            std::string senderUuid,
            std::string kingUuid,
            std::string kingName,
            int kingAmount,
            int total,
            long long time
        ) : mId(std::move(id)),
            mSenderUuid(std::move(senderUuid)),
            mKingUuid(std::move(kingUuid)),
            mKingName(std::move(kingName)),
            mKingAmount(kingAmount),
            mTotal(total),
            mTime(time) {}

        LOICOLLECTION_A_NDAPI std::string getEnvelopeId() const;
        LOICOLLECTION_A_NDAPI std::string getSenderUuid() const;
        LOICOLLECTION_A_NDAPI std::string getKingUuid() const;
        LOICOLLECTION_A_NDAPI std::string getKingName() const;
        LOICOLLECTION_A_NDAPI int getKingAmount() const;
        LOICOLLECTION_A_NDAPI int getTotal() const;
        LOICOLLECTION_A_NDAPI long long getTime() const;
    };
}
