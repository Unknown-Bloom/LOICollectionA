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
    // 求购单子系统：买家挂单、卖家按一口价供货（预付款冻结模型）。
    // 与商店购买流程对称：DB 事务提交是唯一不可逆点，游戏状态失败走补偿。
    class MarketWanted {
    public:
        using BlacklistProvider = std::function<ll::Expected<std::vector<std::string>>(const std::string&)>;

        MarketWanted(
            std::shared_ptr<SQLiteStorage> db,
            std::shared_ptr<SQLiteStorage> settingsDb,
            const Config::C_Market& options,
            std::shared_ptr<ll::io::Logger> logger,
            TimerManager& timerManager,
            BlacklistProvider blacklistProvider
        );

        ~MarketWanted();

        MarketWanted(MarketWanted const&) = delete;
        MarketWanted(MarketWanted&&) = delete;
        MarketWanted& operator=(MarketWanted const&) = delete;
        MarketWanted& operator=(MarketWanted&&) = delete;

    public:
        LOICOLLECTION_A_NDAPI ll::Expected<void> createTables();
        LOICOLLECTION_A_API   void startSweep();

        LOICOLLECTION_A_NDAPI ll::Expected<bool> createWanted(Player& player, int slot, const std::string& name, int unitPrice, int amount);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> cancelWanted(Player& player, const std::string& id);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> fillWanted(Player& player, const std::string& id, int amount);

        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getWantedList();
        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getWantedItems(Player& player);
        LOICOLLECTION_A_NDAPI ll::Expected<std::unordered_map<std::string, std::string>> getWantedData(const std::string& id);

    private:
        bool isValid() const;

        // 求购成交事务：更新 amount_filled（完成则删除）+ 写成交记录；返回成交 key
        ll::Expected<std::string> commitWantedFill(const std::string& id, const std::unordered_map<std::string, std::string>& data, int amount, int pay, int tax, Player& seller);
        // 补偿：恢复 amount_filled / 回写求购单 + 删除成交记录
        ll::Expected<void> restoreWantedFill(const std::string& id, const std::unordered_map<std::string, std::string>& data, int amount, const std::string& saleKey);

        // 删除求购单（事务），供取消与过期扫描复用
        ll::Expected<void> deleteWanted(const std::string& id);
        // 恢复求购单（退款失败补偿）
        ll::Expected<void> restoreWanted(const std::string& id, const std::unordered_map<std::string, std::string>& data);

        // 过期扫描：删除过期求购单并按剩余量退款（每小时触发一次）
        ll::Expected<void> sweepExpired();

        // 退款：在线直接发钱，离线累加 SettingsDB（与离线结算同构，宁可迟发不可多发）
        ll::Expected<void> refundBuyer(const std::string& buyerUuid, int score, const std::string& scoreboard);
        // 税收入账：累计 MarketTax.total
        ll::Expected<void> collectTax(int tax);

        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
