#pragma once

#include <string>
#include <utility>

#include <ll/api/event/Event.h>

#include "LOICollectionA/base/Macro.h"

namespace LOICollection::server::Events {
    class WalletTransferEvent final : public ll::event::Event {
    protected:
        std::string mFromUuid;
        std::string mFromName;
        std::string mToUuid;
        std::string mToName;
        long long mAmount;
        long long mFee;
        std::string mType;
        long long mTime;

    public:
        explicit WalletTransferEvent(
            std::string fromUuid,
            std::string fromName,
            std::string toUuid,
            std::string toName,
            long long amount,
            long long fee,
            std::string type,
            long long time
        ) : mFromUuid(std::move(fromUuid)),
            mFromName(std::move(fromName)),
            mToUuid(std::move(toUuid)),
            mToName(std::move(toName)),
            mAmount(amount),
            mFee(fee),
            mType(std::move(type)),
            mTime(time) {}

        LOICOLLECTION_A_NDAPI std::string getFromUuid() const;
        LOICOLLECTION_A_NDAPI std::string getFromName() const;
        LOICOLLECTION_A_NDAPI std::string getToUuid() const;
        LOICOLLECTION_A_NDAPI std::string getToName() const;
        LOICOLLECTION_A_NDAPI long long getAmount() const;
        LOICOLLECTION_A_NDAPI long long getFee() const;
        LOICOLLECTION_A_NDAPI std::string getType() const;
        LOICOLLECTION_A_NDAPI long long getTime() const;
    };
}
