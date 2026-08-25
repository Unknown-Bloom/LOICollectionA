#pragma once

#include <string>

namespace LOICollection::server::Plugins {
    struct RedEnvelopeEntry {
        std::string id;
        std::string chatKey;
        std::string senderUuid;
        std::string senderName;

        int count;
        long long expireAt;

        std::string kingUuid;
        std::string kingName;
        int kingAmount;

        int total;
    };

    struct WealthEntry {
        std::string uuid;
        std::string name;
        long long balance;
    };
}
