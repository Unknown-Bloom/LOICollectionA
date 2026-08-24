#pragma once

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"

namespace LOICollection::server::Plugins {
    class MarketPlugin;

    class MarketGui {
    public:
        MarketGui() = default;
        ~MarketGui() = default;

        MarketGui(MarketGui const&) = delete;
        MarketGui(MarketGui&&) = delete;
        MarketGui& operator=(MarketGui const&) = delete;
        MarketGui& operator=(MarketGui&&) = delete;

    public:
        LOICOLLECTION_A_NDAPI ll::Expected<void> registerAll(MarketPlugin& owner);

    private:
        void registerCore(MarketPlugin& owner);
        void registerTrade(MarketPlugin& owner);
        void registerStore(MarketPlugin& owner);
        void registerQuote(MarketPlugin& owner);
        void registerWanted(MarketPlugin& owner);
        void registerAuction(MarketPlugin& owner);
    };
}
