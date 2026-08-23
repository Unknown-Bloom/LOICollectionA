#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <unordered_map>

#include <fmt/core.h>

#include <ll/api/Expected.h>
#include <ll/api/event/EventBus.h>
#include <ll/api/service/Bedrock.h>

#include <mc/deps/nbt/Tag.h>
#include <mc/deps/nbt/CompoundTag.h>

#include <mc/world/level/Level.h>
#include <mc/world/actor/player/Player.h>
#include <mc/world/actor/player/PlayerInventory.h>
#include <mc/world/actor/player/Inventory.h>

#include <mc/world/item/ItemStack.h>
#include <mc/world/item/SaveContext.h>
#include <mc/world/item/SaveContextFactory.h>

#include <mc/server/commands/CommandPermissionLevel.h>

#include "LOICollectionA/include/server/Plugins/LanguagePlugin.h"
#include "LOICollectionA/include/server/Events/modules/MarketItemSoldEvent.h"

#include "LOICollectionA/coro/TimerManager.h"

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/mc-server/InventoryUtils.h"
#include "LOICollectionA/utils/mc-server/ScoreboardUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/data/SQLiteStorage.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/market/MarketWanted.h"
#include "LOICollectionA/include/server/Plugins/market/MarketPlugin.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    struct MarketWanted::Impl {
        std::shared_ptr<SQLiteStorage> db;
        std::shared_ptr<SQLiteStorage> settingsDb;
        const Config::C_Market& options;
        std::shared_ptr<ll::io::Logger> logger;
        TimerManager& timerManager;
        BlacklistProvider blacklistProvider;

        Impl(
            std::shared_ptr<SQLiteStorage> db_,
            std::shared_ptr<SQLiteStorage> settingsDb_,
            const Config::C_Market& options_,
            std::shared_ptr<ll::io::Logger> logger_,
            TimerManager& timerManager_,
            BlacklistProvider blacklistProvider_
        ) : db(std::move(db_)),
            settingsDb(std::move(settingsDb_)),
            options(options_),
            logger(std::move(logger_)),
            timerManager(timerManager_),
            blacklistProvider(std::move(blacklistProvider_)) {}
    };

    ll::Expected<void> MarketWanted::createTables() {
        return this->mImpl->db->create("StoreWanted", [](SQLiteStorage::ColumnCallback ctor) -> void {
            ctor("wanted_uuid");
            ctor("wanted_name");
            ctor("item_type");
            ctor("item_data");
            ctor("item_name");
            ctor("unit_price");
            ctor("amount_total");
            ctor("amount_filled");
            ctor("created_at");
            ctor("expire_at");
        });
    }

    ll::Expected<bool> MarketWanted::createWanted(Player& player, int slot, const std::string& name, int unitPrice, int amount) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        if (!this->mImpl->options.StoreWantedEnabled)
            return false;

        if (unitPrice <= 0 || amount <= 0)
            return false;

        ItemStack mItemStack = player.mInventory->mInventory->getItem(slot);
        if (!mItemStack || mItemStack.isNull())
            return false;

        std::string mName = name;
        size_t mBegin = mName.find_first_not_of(" \t\r\n");
        if (mBegin == std::string::npos)
            return false;
        mName = mName.substr(mBegin, mName.find_last_not_of(" \t\r\n") - mBegin + 1);
        if (mName.empty())
            return false;

        std::string mUuid = player.getUuid().asString();
        std::string mScoreboard = this->mImpl->options.TargetScoreboard;
        long long mFrozen = static_cast<long long>(unitPrice) * amount;
        if (mFrozen <= 0 || mFrozen > 2'147'483'647LL)
            return false;

        return this->mImpl->db->find("StoreWanted", {
            { "wanted_uuid", mUuid }
        }, SQLiteStorage::FindCondition::AND)
            .and_then([this, mUuid, mScoreboard, mFrozen, mName, mItemStack = std::move(mItemStack), unitPrice, amount, &player](const std::vector<std::string>& items) -> ll::Expected<bool> {
                if (static_cast<int>(items.size()) >= this->mImpl->options.StoreWantedMaxPerPlayer)
                    return false;

                if (ScoreboardUtils::getScore(player, mScoreboard) < mFrozen) {
                    return LanguagePlugin::getShared()->getLanguage(player)
                        .transform([&player](const std::string& language) -> bool {
                            player.sendMessage(tr(language, "market.gui.sell.sellItem.tips3"));

                            return false;
                        });
                }

                // 预付款冻结：创建求购单时扣全款，保证接单必有钱
                ScoreboardUtils::reduceScore(player, mScoreboard, static_cast<int>(mFrozen));

                std::unordered_map<std::string, std::string> data = {
                    { "wanted_uuid", mUuid },
                    { "wanted_name", player.getRealName() },
                    { "item_type", mItemStack.getTypeName() },
                    { "item_data", mItemStack.save(*SaveContextFactory::createCloneSaveContext())->toSnbt(SnbtFormat::Minimize, 0) },
                    { "item_name", mName },
                    { "unit_price", std::to_string(unitPrice) },
                    { "amount_total", std::to_string(amount) },
                    { "amount_filled", "0" },
                    { "created_at", SystemUtils::getNowTime() },
                    { "expire_at", SystemUtils::toTimeCalculate(
                          SystemUtils::getNowTime(),
                          this->mImpl->options.StoreWantedExpireDays * 86'400
                      ) }
                };

                return this->mImpl->db->set("StoreWanted", SystemUtils::getCurrentTimestamp(), data)
                    .or_else([mScoreboard, mFrozen, &player](ll::Error e) -> ll::Expected<void> {
                        // 写库失败：退回冻结资金，求购单未生效
                        ScoreboardUtils::addScore(player, mScoreboard, static_cast<int>(mFrozen));

                        return ll::Unexpected(e);
                    })
                    .transform([this, &player, mName]() -> bool {
                        this->mImpl->logger->info(fmt::runtime(tr({}, "market.log20")), player.getRealName(), mName);

                        return true;
                    });
            });
    }

    ll::Expected<bool> MarketWanted::cancelWanted(Player& player, const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        if (!this->mImpl->options.StoreWantedEnabled)
            return false;

        return this->getWantedData(id)
            .and_then([this, id, &player](std::unordered_map<std::string, std::string> data) -> ll::Expected<bool> {
                bool mIsAdmin = player.getCommandPermissionLevel() >= CommandPermissionLevel::GameDirectors;
                if (data.at("wanted_uuid") != player.getUuid().asString() && !mIsAdmin)
                    return false;

                int unitPrice = SystemUtils::toInt(data.at("unit_price"), 0);
                int total = SystemUtils::toInt(data.at("amount_total"), 0);
                int filled = SystemUtils::toInt(data.at("amount_filled"), 0);
                int refund = unitPrice * std::max(0, total - filled);

                // 事务先删除求购单（不可逆点），再退款；退款失败则恢复求购单（补偿）
                return this->deleteWanted(id)
                    .and_then([this, id, data, refund, &player]() -> ll::Expected<void> {
                        std::string mScoreboard = this->mImpl->options.TargetScoreboard;

                        return this->refundBuyer(player.getUuid().asString(), refund, mScoreboard)
                            .or_else([this, id, data, &player](ll::Error e) -> ll::Expected<void> {
                                this->mImpl->logger->warn(fmt::runtime(tr({}, "market.log21")), player.getRealName(), id);

                                return this->restoreWanted(id, data).and_then([e = std::move(e)]() mutable -> ll::Expected<void> {
                                    return ll::Unexpected(std::move(e));
                                });
                            });
                    })
                    .transform([this, &player, id]() -> bool {
                        this->mImpl->logger->info(fmt::runtime(tr({}, "market.log22")), player.getRealName(), id);

                        return true;
                    });
            });
    }

    ll::Expected<bool> MarketWanted::fillWanted(Player& player, const std::string& id, int amount) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        if (!this->mImpl->options.StoreWantedEnabled)
            return false;

        if (amount <= 0)
            return false;

        return this->getWantedData(id)
            .and_then([this, id, amount, &player](std::unordered_map<std::string, std::string> data) -> ll::Expected<bool> {
                std::string buyerUuid = data.at("wanted_uuid");
                if (buyerUuid == player.getUuid().asString())
                    return false;

                // 过期/已满校验
                if (SystemUtils::isPastOrPresent(data.at("expire_at")))
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::WantedExpired));

                int total = SystemUtils::toInt(data.at("amount_total"), 0);
                int filled = SystemUtils::toInt(data.at("amount_filled"), 0);
                int remaining = total - filled;
                if (remaining <= 0)
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::WantedFilled));
                if (amount > remaining)
                    return false;

                // 阶段 1：仅允许买家在线时供货
                Player* buyer = ll::service::getLevel()->getPlayer(mce::UUID::fromString(buyerUuid));
                if (!buyer)
                    return false;

                return this->mImpl->blacklistProvider(buyerUuid)
                    .and_then([this, id, amount, data, buyerUuid, &player, buyer](const std::vector<std::string>& blacklists) -> ll::Expected<bool> {
                        if (std::find(blacklists.begin(), blacklists.end(), player.getUuid().asString()) != blacklists.end()) {
                            return LanguagePlugin::getShared()->getLanguage(player)
                                .transform([&player](const std::string& language) -> bool {
                                    player.sendMessage(tr(language, "market.gui.error"));

                                    return false;
                                });
                        }

                        if (!InventoryUtils::isItemInInventory(player, data.at("item_type"), amount))
                            return false;

                        std::string mScoreboard = this->mImpl->options.TargetScoreboard;
                        int unitPrice = SystemUtils::toInt(data.at("unit_price"), 0);
                        long long mPay = static_cast<long long>(unitPrice) * amount;
                        int pay = static_cast<int>(mPay);
                        int tax = static_cast<int>(std::floor(pay * this->mImpl->options.StoreTransactionTaxRate));
                        int sellerAmount = pay - tax;

                        // 买家资金已在创建时全额冻结，此处不再扣款；
                        // 事务提交（摘单 + 写成交）是唯一不可逆点。
                        return this->commitWantedFill(id, data, amount, pay, tax, player)
                            .and_then([this, data, amount, pay, sellerAmount, tax, buyerUuid, &player, buyer](const std::string& saleKey) -> ll::Expected<bool> {
                                // 卖家交出物品 → 买家收物品（校验已通过，失败概率极低）
                                InventoryUtils::clearItem(player, data.at("item_type"), amount);

                                ItemStack mItemStack = ItemStack::fromTag(CompoundTag::fromSnbt(data.at("item_data"))->mTags);
                                InventoryUtils::giveItem(*buyer, mItemStack, amount);

                                player.refreshInventory();
                                buyer->refreshInventory();

                                // 卖家结算：供货者一定在线，同步发钱
                                ScoreboardUtils::addScore(player, this->mImpl->options.TargetScoreboard, sellerAmount);

                                return this->collectTax(tax)
                                    .transform([this, &player, buyer, data, saleKey, amount, pay, tax, buyerUuid]() -> bool {
                                        this->mImpl->logger->info(fmt::runtime(tr({}, "market.log24")), player.getRealName(), data.at("item_name"));

                                        if (auto language = LanguagePlugin::getShared()->getLanguage(*buyer); language.has_value())
                                            buyer->sendMessage(fmt::format(fmt::runtime(tr(language.value(), "market.gui.wanted.fill.tips")), data.at("item_name"), amount));

                                        ll::event::EventBus::getInstance().publish(LOICollection::server::Events::MarketItemSoldEvent(
                                            data.at("item_name"),
                                            pay,
                                            tax,
                                            buyerUuid,
                                            player.getUuid().asString(),
                                            SystemUtils::toLongLong(saleKey, 0)
                                        ));

                                        return true;
                                    });
                            });
                    });
            });
    }

    ll::Expected<std::string> MarketWanted::commitWantedFill(
        const std::string& id,
        const std::unordered_map<std::string, std::string>& data,
        int amount,
        int pay,
        int tax,
        Player& seller
    ) {
        auto transaction = SQLiteStorageTransaction::create(*this->mImpl->db);
        if (!transaction.has_value())
            return ll::Unexpected(transaction.error());

        auto conn = transaction.value().connection();
        std::string saleKey = SystemUtils::getCurrentTimestamp();

        int total = SystemUtils::toInt(data.at("amount_total"), 0);
        int filled = SystemUtils::toInt(data.at("amount_filled"), 0) + amount;

        // 更新已成交量；完成则删除求购单（与商店购买"先摘牌再写成交"同构）
        auto updateResult = filled >= total
            ? this->mImpl->db->del(conn, "StoreWanted", id)
            : this->mImpl->db->set(conn, "StoreWanted", id, "amount_filled", std::to_string(filled));
        if (!updateResult.has_value())
            return ll::Unexpected(updateResult.error());

        std::unordered_map<std::string, std::string> sale = {
            { "store_id", data.at("wanted_uuid") },
            { "item_name", data.at("item_name") },
            { "price", std::to_string(pay) },
            { "tax", std::to_string(tax) },
            { "buyer_uuid", data.at("wanted_uuid") },
            { "buyer_name", data.at("wanted_name") },
            { "seller_uuid", seller.getUuid().asString() },
            { "time", SystemUtils::getNowTime() },
            { "source", "wanted" }
        };

        auto setResult = this->mImpl->db->set(conn, "StoreSale", saleKey, sale);
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());

        auto commitResult = transaction.value().commit();
        if (!commitResult.has_value())
            return ll::Unexpected(commitResult.error());

        return saleKey;
    }

    ll::Expected<void> MarketWanted::restoreWantedFill(
        const std::string& id,
        const std::unordered_map<std::string, std::string>& data,
        int amount,
        const std::string& saleKey
    ) {
        auto transaction = SQLiteStorageTransaction::create(*this->mImpl->db);
        if (!transaction.has_value())
            return ll::Unexpected(transaction.error());

        auto conn = transaction.value().connection();

        int total = SystemUtils::toInt(data.at("amount_total"), 0);
        int filled = SystemUtils::toInt(data.at("amount_filled"), 0);

        // 若求购单已完成（删除），整单恢复；否则回写 amount_filled
        auto updateResult = filled + amount >= total
            ? this->mImpl->db->set(conn, "StoreWanted", id, data)
            : this->mImpl->db->set(conn, "StoreWanted", id, "amount_filled", std::to_string(filled));
        if (!updateResult.has_value())
            return ll::Unexpected(updateResult.error());

        auto delResult = this->mImpl->db->del(conn, "StoreSale", saleKey);
        if (!delResult.has_value())
            return ll::Unexpected(delResult.error());

        return transaction.value().commit().transform([](bool) -> void {});
    }

    ll::Expected<void> MarketWanted::deleteWanted(const std::string& id) {
        auto transaction = SQLiteStorageTransaction::create(*this->mImpl->db);
        if (!transaction.has_value())
            return ll::Unexpected(transaction.error());

        auto delResult = this->mImpl->db->del(transaction.value().connection(), "StoreWanted", id);
        if (!delResult.has_value())
            return ll::Unexpected(delResult.error());

        return transaction.value().commit().transform([](bool) -> void {});
    }

    ll::Expected<void> MarketWanted::restoreWanted(const std::string& id, const std::unordered_map<std::string, std::string>& data) {
        return this->mImpl->db->set("StoreWanted", id, data);
    }

    ll::Expected<void> MarketWanted::refundBuyer(const std::string& buyerUuid, int score, const std::string& scoreboard) {
        if (score <= 0)
            return {};

        if (Player* buyer = ll::service::getLevel()->getPlayer(mce::UUID::fromString(buyerUuid)); buyer) {
            ScoreboardUtils::addScore(*buyer, scoreboard, score);

            return {};
        }

        // 离线买家：累加到暂存分，上线时结算（与离线收款同构，宁可迟发不可多发）
        return this->mImpl->settingsDb->get("Market", buyerUuid, "score", "0")
            .and_then([this, buyerUuid, score](const std::string& value) -> ll::Expected<void> {
                int mMarketScore = SystemUtils::toInt(value, 0);

                return this->mImpl->settingsDb->set("Market", buyerUuid, "score", std::to_string(mMarketScore + score));
            });
    }

    ll::Expected<void> MarketWanted::collectTax(int tax) {
        if (tax <= 0)
            return {};

        return this->mImpl->settingsDb->get("MarketTax", "total", "total", "0")
            .and_then([this, tax](const std::string& value) -> ll::Expected<void> {
                long long total = SystemUtils::toLongLong(value, 0) + tax;

                return this->mImpl->settingsDb->set("MarketTax", "total", "total", std::to_string(total));
            });
    }

    void MarketWanted::startSweep() {
        this->mImpl->timerManager.loopSchedule("MarketWantedSweep", std::chrono::hours(1), [this]() -> void {
            this->sweepExpired().or_else([this](ll::Error e) -> ll::Expected<void> {
                this->mImpl->logger->warn("MarketWanted: sweep failed: {}", e.message());

                return ll::Unexpected(e);
            });
        });
    }

    ll::Expected<void> MarketWanted::sweepExpired() {
        return this->mImpl->db->list("StoreWanted")
            .and_then([this](const std::vector<std::string>& keys) -> ll::Expected<void> {
                if (keys.empty())
                    return {};

                return this->mImpl->db->get("StoreWanted", keys)
                    .and_then([this](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> wanted) -> ll::Expected<void> {
                        std::string mScoreboard = this->mImpl->options.TargetScoreboard;

                        for (const auto& [id, data] : wanted) {
                            if (!SystemUtils::isPastOrPresent(data.at("expire_at")))
                                continue;

                            int total = SystemUtils::toInt(data.at("amount_total"), 0);
                            int filled = SystemUtils::toInt(data.at("amount_filled"), 0);
                            int remaining = std::max(0, total - filled);
                            if (remaining <= 0)
                                continue;

                            int refund = SystemUtils::toInt(data.at("unit_price"), 0) * remaining;

                            auto result = this->deleteWanted(id)
                                .and_then([this, id, data, refund, mScoreboard]() -> ll::Expected<void> {
                                    return this->refundBuyer(data.at("wanted_uuid"), refund, mScoreboard)
                                        .or_else([this, id, data](ll::Error e) -> ll::Expected<void> {
                                            this->mImpl->logger->warn(fmt::runtime(tr({}, "market.log21")), data.at("wanted_name"), id);

                                            return this->restoreWanted(id, data).and_then([e = std::move(e)]() mutable -> ll::Expected<void> {
                                                return ll::Unexpected(std::move(e));
                                            });
                                        });
                                })
                                .transform([this, data, refund]() -> void {
                                    this->mImpl->logger->info(fmt::runtime(tr({}, "market.log17")), data.at("item_name"), refund, data.at("wanted_name"));
                                });

                            if (!result.has_value())
                                this->mImpl->logger->warn(fmt::runtime(tr({}, "market.log21")), data.at("wanted_name"), id);
                        }

                        return {};
                    });
            });
    }

    ll::Expected<std::vector<std::string>> MarketWanted::getWantedList() {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        if (!this->mImpl->options.StoreWantedEnabled)
            return {};

        return this->mImpl->db->list("StoreWanted");
    }

    ll::Expected<std::vector<std::string>> MarketWanted::getWantedItems(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        if (!this->mImpl->options.StoreWantedEnabled)
            return {};

        return this->mImpl->db->find("StoreWanted", {
            { "wanted_uuid", player.getUuid().asString() }
        }, SQLiteStorage::FindCondition::AND);
    }

    ll::Expected<std::unordered_map<std::string, std::string>> MarketWanted::getWantedData(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->db->has("StoreWanted", id)
            .and_then([this, id](bool exists) -> ll::Expected<std::unordered_map<std::string, std::string>> {
                if (!exists)
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::WantedNotFound));

                return this->mImpl->db->get("StoreWanted", id);
            });
    }

    MarketWanted::MarketWanted(
        std::shared_ptr<SQLiteStorage> db,
        std::shared_ptr<SQLiteStorage> settingsDb,
        const Config::C_Market& options,
        std::shared_ptr<ll::io::Logger> logger,
        TimerManager& timerManager,
        BlacklistProvider blacklistProvider
    ) : mImpl(std::make_unique<Impl>(
            std::move(db),
            std::move(settingsDb),
            options,
            std::move(logger),
            timerManager,
            std::move(blacklistProvider)
        )) {}

    MarketWanted::~MarketWanted() = default;

    bool MarketWanted::isValid() const {
        return mImpl != nullptr && this->mImpl->db != nullptr && this->mImpl->settingsDb != nullptr && this->mImpl->logger != nullptr;
    }
}
