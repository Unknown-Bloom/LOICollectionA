#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"

class Player;
class SQLiteStorage;
class TimerManager;

namespace ll {
    namespace io {
        class Logger;
    }
}

namespace Config {
    struct C_Market;
}

namespace LOICollection::server::Plugins {
    // 英式拍卖子系统：卖家挂拍、买家竞价（出价即冻结全款，被超越自动退款）。
    // 到期由定时扫描结算：有出价者成交（物品给买家、卖家实收扣税），无人出价流拍退回物品。
    // 与求购/购买同构：DB 事务提交是唯一不可逆点，游戏状态失败走补偿；重启时补结算。
    class MarketAuction {
    public:
        using BlacklistProvider = std::function<ll::Expected<std::vector<std::string>>(const std::string&)>;

        MarketAuction(
            std::shared_ptr<SQLiteStorage> db,
            std::shared_ptr<SQLiteStorage> settingsDb,
            const Config::C_Market& options,
            std::shared_ptr<ll::io::Logger> logger,
            TimerManager& timerManager,
            BlacklistProvider blacklistProvider
        );

        ~MarketAuction();

        MarketAuction(MarketAuction const&) = delete;
        MarketAuction(MarketAuction&&) = delete;
        MarketAuction& operator=(MarketAuction const&) = delete;
        MarketAuction& operator=(MarketAuction&&) = delete;

    public:
        LOICOLLECTION_A_NDAPI ll::Expected<void> createTables();
        LOICOLLECTION_A_API   void startSweep();

        LOICOLLECTION_A_NDAPI ll::Expected<bool> createAuction(Player& player, int slot, const std::string& name, int startPrice, int durationSeconds);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> bidAuction(Player& player, const std::string& id, int price);

        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getAuctionList();
        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getAuctionItems(Player& player);
        LOICOLLECTION_A_NDAPI ll::Expected<std::unordered_map<std::string, std::string>> getAuctionData(const std::string& id);

    private:
        bool isValid() const;

        // 到期扫描：结算到期且未结算的拍卖（有出价者成交，无人出价流拍）；玩家离线则顺延下次扫描
        ll::Expected<void> sweepExpired();

        // 结算成功分支：物品给买家、卖家实收扣税、成交记录计入行情与税收
        ll::Expected<void> finalizeWin(const std::string& id, const std::unordered_map<std::string, std::string>& data);
        // 结算流拍分支：物品退还卖家
        ll::Expected<void> finalizeLose(const std::string& id, const std::unordered_map<std::string, std::string>& data);

        // 补偿：恢复拍卖单（settled 归零）+ 删除成交记录
        ll::Expected<void> restoreAuction(const std::string& id, const std::unordered_map<std::string, std::string>& data, const std::string& saleKey);

        // 退款：在线直接发钱，离线累加 SettingsDB（与离线结算同构，宁可迟发不可多发）
        ll::Expected<void> refundScore(const std::string& uuid, int score, const std::string& scoreboard);
        // 税收入账：累计 MarketTax.total
        ll::Expected<void> collectTax(int tax);

        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
