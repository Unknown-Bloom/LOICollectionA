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

#include "LOICollectionA/include/server/Plugins/LanguagePlugin.h"
#include "LOICollectionA/include/server/Events/modules/MarketItemSoldEvent.h"

#include "LOICollectionA/coro/TimerManager.h"

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/mc-server/InventoryUtils.h"
#include "LOICollectionA/utils/mc-server/ScoreboardUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/data/SQLiteStorage.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/market/MarketAuction.h"
#include "LOICollectionA/include/server/Plugins/market/MarketPlugin.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    struct MarketAuction::Impl {
        std::shared_ptr<SQLiteStorage> db;
        std::shared_ptr<SQLiteStorage> settingsDb;
        const Config::C_Market& options;
        std::shared_ptr<ll::io::Logger> logger;
        TimerManager& timerManager;
        BlacklistProvider blacklistProvider;
        TaxRateProvider taxRateProvider;

        Impl(
            std::shared_ptr<SQLiteStorage> db_,
            std::shared_ptr<SQLiteStorage> settingsDb_,
            const Config::C_Market& options_,
            std::shared_ptr<ll::io::Logger> logger_,
            TimerManager& timerManager_,
            BlacklistProvider blacklistProvider_,
            TaxRateProvider taxRateProvider_
        ) : db(std::move(db_)),
            settingsDb(std::move(settingsDb_)),
            options(options_),
            logger(std::move(logger_)),
            timerManager(timerManager_),
            blacklistProvider(std::move(blacklistProvider_)),
            taxRateProvider(std::move(taxRateProvider_)) {}

        double effectiveTaxRate() const {
            return taxRateProvider ? taxRateProvider() : options.StoreTransactionTaxRate;
        }
    };

    ll::Expected<void> MarketAuction::createTables() {
        return this->mImpl->db->create("StoreAuction", [](SQLiteStorage::ColumnCallback ctor) -> void {
            ctor("seller_uuid");
            ctor("seller_name");
            ctor("item_type");
            ctor("item_data");
            ctor("item_name");
            ctor("start_price");
            ctor("current_price");
            ctor("bidder_uuid");
            ctor("bidder_name");
            ctor("bid_count");
            ctor("end_at");
            ctor("settled");
        });
    }

    ll::Expected<bool> MarketAuction::createAuction(Player& player, int slot, const std::string& name, int startPrice, int durationSeconds) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        if (!this->mImpl->options.StoreAuctionEnabled)
            return false;

        if (startPrice <= 0)
            return false;

        int minDuration = this->mImpl->options.StoreAuctionMinDurationMinutes * 60;
        int maxDuration = this->mImpl->options.StoreAuctionMaxDurationHours * 3600;
        if (durationSeconds < minDuration || durationSeconds > maxDuration)
            return false;

        ItemStack mItemStack = player.mInventory->mInventory->getItem(slot);
        if (!mItemStack || mItemStack.isNull())
            return false;

        std::vector<std::string> prohibitedItems = this->mImpl->options.ProhibitedItems;
        if (std::find(prohibitedItems.begin(), prohibitedItems.end(), mItemStack.getTypeName()) != prohibitedItems.end())
            return false;

        std::string mName = name;
        size_t mBegin = mName.find_first_not_of(" \t\r\n");
        if (mBegin == std::string::npos)
            return false;
        mName = mName.substr(mBegin, mName.find_last_not_of(" \t\r\n") - mBegin + 1);
        if (mName.empty())
            return false;

        std::unordered_map<std::string, std::string> data = {
            { "seller_uuid", player.getUuid().asString() },
            { "seller_name", player.getRealName() },
            { "item_type", mItemStack.getTypeName() },
            { "item_data", mItemStack.save(*SaveContextFactory::createCloneSaveContext())->toSnbt(SnbtFormat::Minimize, 0) },
            { "item_name", mName },
            { "start_price", std::to_string(startPrice) },
            { "current_price", std::to_string(startPrice) },
            { "bidder_uuid", "" },
            { "bidder_name", "" },
            { "bid_count", "0" },
            { "created_at", SystemUtils::getNowTime() },
            { "end_at", SystemUtils::toTimeCalculate(SystemUtils::getNowTime(), durationSeconds) },
            { "settled", "0" }
        };

        return this->mImpl->db->set("StoreAuction", SystemUtils::getCurrentTimestamp(), data)
            .or_else([&player, &mItemStack](ll::Error e) -> ll::Expected<void> {
                player.mInventory->mInventory->addItem(mItemStack);
                player.refreshInventory();

                return ll::Unexpected(std::move(e));
            })
            .transform([this, slot, &player, mName]() -> bool {
                player.mInventory->mInventory->removeItem(slot, 64);
                player.refreshInventory();

                this->mImpl->logger->info(fmt::runtime(tr({}, "market.log25")), player.getRealName(), mName);

                return true;
            });
    }

    ll::Expected<bool> MarketAuction::bidAuction(Player& player, const std::string& id, int price) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        if (!this->mImpl->options.StoreAuctionEnabled)
            return false;

        if (price <= 0)
            return false;

        return this->getAuctionData(id)
            .and_then([this, id, price, &player](std::unordered_map<std::string, std::string> data) -> ll::Expected<bool> {
                std::string sellerUuid = data.at("seller_uuid");
                if (sellerUuid == player.getUuid().asString())
                    return false;

                if (SystemUtils::isPastOrPresent(data.at("end_at")))
                    return false;
                if (data.at("settled") == "1")
                    return false;

                if (!data.at("bidder_uuid").empty() && data.at("bidder_uuid") == player.getUuid().asString())
                    return false;

                int currentPrice = SystemUtils::toInt(data.at("current_price"), 0);

                int minBid = static_cast<int>(std::ceil(currentPrice * this->mImpl->options.StoreAuctionMinBidIncrement));
                if (price < minBid)
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::AuctionBidTooLow));

                return this->mImpl->blacklistProvider(sellerUuid)
                    .and_then([this, id, price, &player, data](const std::vector<std::string>& blacklists) -> ll::Expected<bool> {
                        if (std::find(blacklists.begin(), blacklists.end(), player.getUuid().asString()) != blacklists.end()) {
                            return LanguagePlugin::getShared()->getLanguage(player)
                                .transform([&player](const std::string& language) -> bool {
                                    player.sendMessage(tr(language, "market.gui.error"));

                                    return false;
                                });
                        }

                        std::string mScoreboard = this->mImpl->options.TargetScoreboard;
                        if (ScoreboardUtils::getScore(player, mScoreboard) < price)
                            return false;

                        ScoreboardUtils::reduceScore(player, mScoreboard, price);

                        std::unordered_map<std::string, std::string> updated = data;
                        updated["current_price"] = std::to_string(price);
                        updated["bidder_uuid"] = player.getUuid().asString();
                        updated["bidder_name"] = player.getRealName();
                        updated["bid_count"] = std::to_string(SystemUtils::toInt(data.at("bid_count"), 0) + 1);

                        int antiSnipe = this->mImpl->options.StoreAuctionAntiSnipeSeconds;
                        if (antiSnipe > 0 &&
                            SystemUtils::isPastOrPresent(SystemUtils::toTimeCalculate(data.at("end_at"), -antiSnipe))) {
                            updated["end_at"] = SystemUtils::toTimeCalculate(data.at("end_at"), antiSnipe);
                        }

                        return this->mImpl->db->set("StoreAuction", id, updated)
                            .or_else([mScoreboard, price, &player](ll::Error e) -> ll::Expected<void> {
                                ScoreboardUtils::addScore(player, mScoreboard, price);

                                return ll::Unexpected(e);
                            })
                            .and_then([this, id, data, price, mScoreboard, &player]() -> ll::Expected<bool> {
                                std::string oldBidder = data.at("bidder_uuid");
                                if (oldBidder.empty()) {
                                    this->mImpl->logger->info(fmt::runtime(tr({}, "market.log26")), player.getRealName(), data.at("item_name"), price);

                                    return true;
                                }

                                int oldPrice = SystemUtils::toInt(data.at("current_price"), 0);
                                return this->refundScore(oldBidder, oldPrice, mScoreboard)
                                    .or_else([this, id, data, oldBidder, mScoreboard, price, &player](ll::Error e) -> ll::Expected<void> {
                                        this->mImpl->logger->warn(fmt::runtime(tr({}, "market.log27")), data.at("item_name"), oldBidder);

                                        auto restore = this->mImpl->db->set("StoreAuction", id, data)
                                            .and_then([mScoreboard, price, &player]() -> ll::Expected<void> {
                                                ScoreboardUtils::addScore(player, mScoreboard, price);

                                                return {};
                                            });

                                        return std::move(restore).and_then([e = std::move(e)]() mutable -> ll::Expected<void> {
                                            return ll::Unexpected(std::move(e));
                                        });
                                    })
                                    .transform([this, &player, data, price, oldBidder, oldPrice]() -> bool {
                                        if (Player* outbid = ll::service::getLevel()->getPlayer(mce::UUID::fromString(oldBidder)); outbid) {
                                            auto language = LanguagePlugin::getShared()->getLanguage(*outbid);
                                            if (language.has_value())
                                                outbid->sendMessage(fmt::format(fmt::runtime(tr(language.value(), "market.gui.auction.outbid.tips")), data.at("item_name"), oldPrice));
                                        }

                                        this->mImpl->logger->info(fmt::runtime(tr({}, "market.log26")), player.getRealName(), data.at("item_name"), price);

                                        return true;
                                    });
                            });
                    });
            });
    }

    ll::Expected<void> MarketAuction::finalizeWin(const std::string& id, const std::unordered_map<std::string, std::string>& data) {
        std::string bidderUuid = data.at("bidder_uuid");

        int price = SystemUtils::toInt(data.at("current_price"), 0);
        int tax = static_cast<int>(std::floor(price * this->mImpl->effectiveTaxRate()));
        int sellerAmount = price - tax;

        auto transaction = SQLiteStorageTransaction::create(*this->mImpl->db);
        if (!transaction.has_value())
            return ll::Unexpected(transaction.error());

        auto conn = transaction.value().connection();
        std::string saleKey = SystemUtils::getCurrentTimestamp();

        std::unordered_map<std::string, std::string> sale = {
            { "store_id", data.at("seller_uuid") },
            { "item_name", data.at("item_name") },
            { "price", std::to_string(price) },
            { "tax", std::to_string(tax) },
            { "buyer_uuid", bidderUuid },
            { "buyer_name", data.at("bidder_name") },
            { "seller_uuid", data.at("seller_uuid") },
            { "time", SystemUtils::getNowTime() },
            { "source", "auction" }
        };

        auto setSale = this->mImpl->db->set(conn, "StoreSale", saleKey, sale);
        if (!setSale.has_value())
            return ll::Unexpected(setSale.error());

        auto delAuction = this->mImpl->db->del(conn, "StoreAuction", id);
        if (!delAuction.has_value())
            return ll::Unexpected(delAuction.error());

        auto commitResult = transaction.value().commit();
        if (!commitResult.has_value())
            return ll::Unexpected(commitResult.error());

        auto compensate = [this, id, data, saleKey]() -> ll::Expected<void> {
            this->mImpl->logger->warn(fmt::runtime(tr({}, "market.log16")), data.at("seller_name"), id);

            return this->restoreAuction(id, data, saleKey);
        };

        Player* bidder = ll::service::getLevel()->getPlayer(mce::UUID::fromString(bidderUuid));
        if (!bidder)
            return compensate();

        ItemStack mItemStack = ItemStack::fromTag(CompoundTag::fromSnbt(data.at("item_data"))->mTags);
        InventoryUtils::giveItem(*bidder, mItemStack, static_cast<int>(mItemStack.mCount));
        bidder->refreshInventory();

        std::string mScoreboard = this->mImpl->options.TargetScoreboard;

        ll::Expected<void> settle = [&]() -> ll::Expected<void> {
            if (Player* seller = ll::service::getLevel()->getPlayer(mce::UUID::fromString(data.at("seller_uuid"))); seller) {
                return LanguagePlugin::getShared()->getLanguage(*seller)
                    .transform([seller, &data, mScoreboard, sellerAmount](const std::string& language) -> void {
                        seller->sendMessage(fmt::format(fmt::runtime(tr(language, "market.gui.auction.sold.tips")), data.at("item_name"), sellerAmount));

                        ScoreboardUtils::addScore(*seller, mScoreboard, sellerAmount);
                    });
            }

            return this->mImpl->settingsDb->get("Market", data.at("seller_uuid"), "score", "0")
                .and_then([this, &data, sellerAmount](const std::string& value) -> ll::Expected<void> {
                    int mMarketScore = SystemUtils::toInt(value, 0);

                    return this->mImpl->settingsDb->set("Market", data.at("seller_uuid"), "score", std::to_string(mMarketScore + sellerAmount));
                });
        }();

        return std::move(settle).or_else([&compensate](ll::Error e) -> ll::Expected<void> {
            return compensate().and_then([e = std::move(e)]() mutable -> ll::Expected<void> {
                return ll::Unexpected(std::move(e));
            });
        }).and_then([this, &data, price, tax, saleKey, bidderUuid, id]() -> ll::Expected<void> {
            return this->collectTax(tax)
                .or_else([this, id](ll::Error e) -> ll::Expected<void> {
                    this->mImpl->logger->warn("MarketAuction: tax collect failed for auction {}: {}", id, e.message());

                    return ll::Unexpected(e);
                })
                .transform([this, &data, price, tax, saleKey, bidderUuid]() -> void {
                    this->mImpl->logger->info(fmt::runtime(tr({}, "market.log18")), data.at("item_name"), data.at("bidder_name"));

                    ll::event::EventBus::getInstance().publish(LOICollection::server::Events::MarketItemSoldEvent(
                        data.at("item_name"),
                        price,
                        tax,
                        bidderUuid,
                        data.at("seller_uuid"),
                        SystemUtils::toLongLong(saleKey, 0)
                    ));
                });
        });
    }

    ll::Expected<void> MarketAuction::finalizeLose(const std::string& id, const std::unordered_map<std::string, std::string>& data) {
        auto transaction = SQLiteStorageTransaction::create(*this->mImpl->db);
        if (!transaction.has_value())
            return ll::Unexpected(transaction.error());

        auto delResult = this->mImpl->db->del(transaction.value().connection(), "StoreAuction", id);
        if (!delResult.has_value())
            return ll::Unexpected(delResult.error());

        auto commitResult = transaction.value().commit();
        if (!commitResult.has_value())
            return ll::Unexpected(commitResult.error());

        Player* seller = ll::service::getLevel()->getPlayer(mce::UUID::fromString(data.at("seller_uuid")));
        if (!seller)
            return this->restoreAuction(id, data, "");

        ItemStack mItemStack = ItemStack::fromTag(CompoundTag::fromSnbt(data.at("item_data"))->mTags);
        InventoryUtils::giveItem(*seller, mItemStack, static_cast<int>(mItemStack.mCount));
        seller->refreshInventory();

        this->mImpl->logger->info(fmt::runtime(tr({}, "market.log19")), data.at("item_name"), data.at("seller_name"));

        return {};
    }

    ll::Expected<void> MarketAuction::sweepExpired() {
        return this->mImpl->db->list("StoreAuction")
            .and_then([this](const std::vector<std::string>& keys) -> ll::Expected<void> {
                if (keys.empty())
                    return {};

                return this->mImpl->db->get("StoreAuction", keys)
                    .and_then([this](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> auctions) -> ll::Expected<void> {
                        for (const auto& [id, data] : auctions) {
                            if (data.at("settled") == "1")
                                continue;

                            if (!SystemUtils::isPastOrPresent(data.at("end_at")))
                                continue;

                            if (!data.at("bidder_uuid").empty()) {
                                Player* bidder = ll::service::getLevel()->getPlayer(mce::UUID::fromString(data.at("bidder_uuid")));
                                if (!bidder)
                                    continue;

                                auto result = this->finalizeWin(id, data);
                                if (!result.has_value())
                                    this->mImpl->logger->warn("MarketAuction: settle auction {} failed: {}", id, result.error().message());
                            }
                            else {
                                Player* seller = ll::service::getLevel()->getPlayer(mce::UUID::fromString(data.at("seller_uuid")));
                                if (!seller)
                                    continue;

                                auto result = this->finalizeLose(id, data);
                                if (!result.has_value())
                                    this->mImpl->logger->warn("MarketAuction: settle auction {} failed: {}", id, result.error().message());
                            }
                        }

                        return {};
                    });
            });
    }

    ll::Expected<void> MarketAuction::restoreAuction(const std::string& id, const std::unordered_map<std::string, std::string>& data, const std::string& saleKey) {
        auto transaction = SQLiteStorageTransaction::create(*this->mImpl->db);
        if (!transaction.has_value())
            return ll::Unexpected(transaction.error());

        auto conn = transaction.value().connection();

        std::unordered_map<std::string, std::string> restored = data;
        restored["settled"] = "0";

        auto setResult = this->mImpl->db->set(conn, "StoreAuction", id, restored);
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());

        if (!saleKey.empty()) {
            auto delResult = this->mImpl->db->del(conn, "StoreSale", saleKey);
            if (!delResult.has_value())
                return ll::Unexpected(delResult.error());
        }

        return transaction.value().commit().transform([](bool) -> void {});
    }

    ll::Expected<void> MarketAuction::refundScore(const std::string& uuid, int score, const std::string& scoreboard) {
        if (score <= 0)
            return {};

        if (Player* player = ll::service::getLevel()->getPlayer(mce::UUID::fromString(uuid)); player) {
            ScoreboardUtils::addScore(*player, scoreboard, score);

            return {};
        }

        return this->mImpl->settingsDb->get("Market", uuid, "score", "0")
            .and_then([this, uuid, score](const std::string& value) -> ll::Expected<void> {
                int mMarketScore = SystemUtils::toInt(value, 0);

                return this->mImpl->settingsDb->set("Market", uuid, "score", std::to_string(mMarketScore + score));
            });
    }

    ll::Expected<void> MarketAuction::collectTax(int tax) {
        if (tax <= 0)
            return {};

        return this->mImpl->settingsDb->get("MarketTax", "total", "total", "0")
            .and_then([this, tax](const std::string& value) -> ll::Expected<void> {
                long long total = SystemUtils::toLongLong(value, 0) + tax;

                return this->mImpl->settingsDb->set("MarketTax", "total", "total", std::to_string(total));
            });
    }

    void MarketAuction::startSweep() {
        this->sweepExpired().or_else([this](ll::Error e) -> ll::Expected<void> {
            this->mImpl->logger->warn("MarketAuction: startup sweep failed: {}", e.message());

            return ll::Unexpected(e);
        });

        this->mImpl->timerManager.loopSchedule("MarketAuctionSweep", std::chrono::minutes(1), [this]() -> void {
            this->sweepExpired().or_else([this](ll::Error e) -> ll::Expected<void> {
                this->mImpl->logger->warn("MarketAuction: sweep failed: {}", e.message());

                return ll::Unexpected(e);
            });
        });
    }

    ll::Expected<std::vector<std::string>> MarketAuction::getAuctionList() {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        if (!this->mImpl->options.StoreAuctionEnabled)
            return {};

        return this->mImpl->db->list("StoreAuction");
    }

    ll::Expected<std::vector<std::string>> MarketAuction::getAuctionItems(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        if (!this->mImpl->options.StoreAuctionEnabled)
            return {};

        return this->mImpl->db->find("StoreAuction", {
            { "seller_uuid", player.getUuid().asString() }
        }, SQLiteStorage::FindCondition::AND);
    }

    ll::Expected<std::unordered_map<std::string, std::string>> MarketAuction::getAuctionData(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->db->has("StoreAuction", id)
            .and_then([this, id](bool exists) -> ll::Expected<std::unordered_map<std::string, std::string>> {
                if (!exists)
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::AuctionNotFound));

                return this->mImpl->db->get("StoreAuction", id);
            });
    }

    MarketAuction::MarketAuction(
        std::shared_ptr<SQLiteStorage> db,
        std::shared_ptr<SQLiteStorage> settingsDb,
        const Config::C_Market& options,
        std::shared_ptr<ll::io::Logger> logger,
        TimerManager& timerManager,
        BlacklistProvider blacklistProvider,
        TaxRateProvider taxRateProvider
    ) : mImpl(std::make_unique<Impl>(
            std::move(db),
            std::move(settingsDb),
            options,
            std::move(logger),
            timerManager,
            std::move(blacklistProvider),
            std::move(taxRateProvider)
        )) {}

    MarketAuction::~MarketAuction() = default;

    bool MarketAuction::isValid() const {
        return mImpl != nullptr && this->mImpl->db != nullptr && this->mImpl->settingsDb != nullptr && this->mImpl->logger != nullptr;
    }
}
