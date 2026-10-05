#pragma once

#include <algorithm>
#include <limits>
#include <string>

namespace LOICollection::server::Plugins {
    enum class WalletTransferType {
        online,
        offline
    };

    namespace detail {
        inline int toScore(long long value) {
            return static_cast<int>(std::clamp<long long>(value, 0, std::numeric_limits<int>::max()));
        }
    }

    struct RedEnvelopeEntry {
        std::string id;
        std::string chatKey;
        std::string senderUuid;
        std::string senderName;

        int count;
        long long expireAt;

        std::string kingUuid;
        std::string kingName;
        long long kingAmount;

        long long total;
    };

    struct WealthEntry {
        std::string uuid;
        std::string name;
        long long balance;
    };
}
