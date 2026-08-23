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
        // 寄售、黑名单与玩家通用查询
        void registerCore(MarketPlugin& owner);
        // 玩家面对面交易
        void registerTrade(MarketPlugin& owner);
        // 个人商店与评价审核
        void registerStore(MarketPlugin& owner);
        // 行情聚合
        void registerQuote(MarketPlugin& owner);
        // 求购单
        void registerWanted(MarketPlugin& owner);
        // 拍卖
        void registerAuction(MarketPlugin& owner);
    };
}
