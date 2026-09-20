#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
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

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/TableSchema.h"
#include "LOICollectionA/include/server/Plugins/market/MarketAuction.h"
#include "LOICollectionA/include/server/Plugins/market/MarketPlugin.h"

using I18nUtilsTools::tr;
using LOICollection::data::FindMode;

namespace LOICollection::server::Plugins {

    static long long nowEpochSeconds() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    struct MarketAuction::Impl {
        std::shared_ptr<BlockRepository> db;
        std::shared_ptr<BlockRepository> settingsDb;
        const Config::C_Market& options;
        std::shared_ptr<ll::io::Logger> logger;
        TimerManager& timerManager;
        BlacklistProvider blacklistProvider;
        TaxRateProvider taxRateProvider;
        std::optional<StoreAuctionTable> mTable;
        std::optional<StoreSaleTable> mSale;

        Impl(
            std::shared_ptr<BlockRepository> db_,
            std::shared_ptr<BlockRepository> settingsDb_,
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
        return StoreAuctionTable::open(*this->mImpl->db, "StoreAuction")
            .and_then([this](StoreAuctionTable table) -> ll::Expected<StoreSaleTable> {
                this->mImpl->mTable.emplace(std::move(table));

                return StoreSaleTable::open(*this->mImpl->db, "StoreSale");
            })
            .and_then([this](StoreSaleTable table) -> ll::Expected<void> {
                this->mImpl->mSale.emplace(std::move(table));

                return {};
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

        long long mEndAt = nowEpochSeconds() + static_cast<long long>(durationSeconds);
        std::string mId = SystemUtils::getCurrentTimestamp();

        auto& t = *this->mImpl->mTable;
        auto tx = t.tx();
        if (!tx.has_value())
            return ll::Unexpected(tx.error());

        auto r = tx->set(mId, StoreAuctionCol::seller_uuid, player.getUuid().asString())
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(mId, StoreAuctionCol::seller_name, player.getRealName());
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(mId, StoreAuctionCol::item_type, mItemStack.getTypeName());
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(mId, StoreAuctionCol::item_data, mItemStack.save(*SaveContextFactory::createCloneSaveContext())->toSnbt(SnbtFormat::Minimize, 0));
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(mId, StoreAuctionCol::item_name, mName);
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(mId, StoreAuctionCol::start_price, static_cast<long long>(startPrice));
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(mId, StoreAuctionCol::current_price, static_cast<long long>(startPrice));
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(mId, StoreAuctionCol::bidder_uuid, std::string(""));
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(mId, StoreAuctionCol::bidder_name, std::string(""));
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(mId, StoreAuctionCol::bid_count, 0LL);
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(mId, StoreAuctionCol::end_at, mEndAt);
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(mId, StoreAuctionCol::settled, false);
            });

        if (!r.has_value()) {
            (void)tx->rollback();

            return ll::Unexpected(r.error());
        }

        auto c = tx->commit();
        if (!c.has_value()) {
            (void)tx->rollback();

            return ll::Unexpected(c.error());
        }

        player.mInventory->mInventory->removeItem(slot, 64);
        player.refreshInventory();

        this->mImpl->logger->info(fmt::runtime(tr({}, "market.log25")), player.getRealName(), mName);

        return true;
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

                long long endAt = SystemUtils::toLongLong(data.at("end_at"), 0);
                if (nowEpochSeconds() >= endAt)
                    return false;
                if (data.at("settled") == "1")
                    return false;

                if (!data.at("bidder_uuid").empty() && data.at("bidder_uuid") == player.getUuid().asString())
                    return false;

                long long currentPrice = SystemUtils::toLongLong(data.at("current_price"), 0);
                bool firstBid = data.at("bidder_uuid").empty();

                long long minBid = firstBid
                    ? currentPrice
                    : static_cast<long long>(std::ceil(static_cast<double>(currentPrice) * this->mImpl->options.StoreAuctionMinBidIncrement));
                if (price < minBid)
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::AuctionBidTooLow));

                return this->mImpl->blacklistProvider(sellerUuid)
                    .and_then([this, id, price, &player, data, endAt](const std::vector<std::string>& blacklists) -> ll::Expected<bool> {
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

                        long long bidCount = SystemUtils::toLongLong(data.at("bid_count"), 0);

                        int antiSnipe = this->mImpl->options.StoreAuctionAntiSnipeSeconds;
                        long long newEndAt = endAt;
                        if (antiSnipe > 0 &&
                            nowEpochSeconds() >= endAt - static_cast<long long>(antiSnipe)) {
                            newEndAt = endAt + static_cast<long long>(antiSnipe);
                        }

                        auto& t = *this->mImpl->mTable;

                        return t.set(id, StoreAuctionCol::current_price, static_cast<long long>(price))
                            .and_then([&]() -> ll::Expected<void> {
                                return t.set(id, StoreAuctionCol::bidder_uuid, player.getUuid().asString());
                            })
                            .and_then([&]() -> ll::Expected<void> {
                                return t.set(id, StoreAuctionCol::bidder_name, player.getRealName());
                            })
                            .and_then([&]() -> ll::Expected<void> {
                                return t.set(id, StoreAuctionCol::bid_count, bidCount + 1);
                            })
                            .and_then([&]() -> ll::Expected<void> {
                                return t.set(id, StoreAuctionCol::end_at, newEndAt);
                            })
                            .and_then([this, id, data, price, mScoreboard, &player]() -> ll::Expected<bool> {
                                std::string oldBidder = data.at("bidder_uuid");
                                if (oldBidder.empty()) {
                                    this->mImpl->logger->info(fmt::runtime(tr({}, "market.log26")), player.getRealName(), data.at("item_name"), price);

                                    return true;
                                }

                                long long oldPrice = SystemUtils::toLongLong(data.at("current_price"), 0);
                                return this->refundScore(oldBidder, static_cast<int>(oldPrice), mScoreboard)
                                    .or_else([this, id, data, oldBidder, mScoreboard, price, &player](ll::Error e) -> ll::Expected<void> {
                                        this->mImpl->logger->warn(fmt::runtime(tr({}, "market.log27")), data.at("item_name"), oldBidder);

                                        ScoreboardUtils::addScore(player, mScoreboard, price);

                                        auto& t = *this->mImpl->mTable;
                                        long long oldPrice = SystemUtils::toLongLong(data.at("current_price"), 0);
                                        long long oldBidCount = SystemUtils::toLongLong(data.at("bid_count"), 0);
                                        long long oldEndAt = SystemUtils::toLongLong(data.at("end_at"), 0);

                                        auto restore = t.set(id, StoreAuctionCol::current_price, oldPrice)
                                            .and_then([&]() -> ll::Expected<void> {
                                                return t.set(id, StoreAuctionCol::bidder_uuid, data.at("bidder_uuid"));
                                            })
                                            .and_then([&]() -> ll::Expected<void> {
                                                return t.set(id, StoreAuctionCol::bidder_name, data.at("bidder_name"));
                                            })
                                            .and_then([&]() -> ll::Expected<void> {
                                                return t.set(id, StoreAuctionCol::bid_count, oldBidCount);
                                            })
                                            .and_then([&]() -> ll::Expected<void> {
                                                return t.set(id, StoreAuctionCol::end_at, oldEndAt);
                                            });

                                        return std::move(restore).and_then([e = std::move(e)]() mutable -> ll::Expected<void> {
                                            return ll::Unexpected(std::move(e));
                                        });
                                    })
                                    .transform([this, &player, data, price, oldBidder, oldPrice]() -> bool {
                                        if (Player* outbid = ll::service::getLevel()->getPlayer(mce::UUID::fromString(oldBidder)); outbid) {
                                            auto language = LanguagePlugin::getShared()->getLanguage(*outbid);
                                            if (language.has_value())
                                                outbid->sendMessage(fmt::format(fmt::runtime(tr(language.value(), "market.gui.auction.outbid.tips")), data.at("item_name"), static_cast<int>(oldPrice)));
                                        }

                                        this->mImpl->logger->info(fmt::runtime(tr({}, "market.log26")), player.getRealName(), data.at("item_name"), price);

                                        return true;
                                    });
                            });
                    });
            });
    }

    ll::Expected<void> MarketAuction::finalizeWin(const std::string& id, const std::unordered_map<std::string, std::string>& data) {
        int price = SystemUtils::toInt(data.at("current_price"), 0);
        int tax = static_cast<int>(std::floor(price * this->mImpl->effectiveTaxRate()));
        int sellerAmount = price - tax;

        std::string bidderUuid = data.at("bidder_uuid");

        std::string saleKey = SystemUtils::getCurrentTimestamp();

        auto tx = this->mImpl->mTable->tx();
        if (!tx.has_value())
            return ll::Unexpected(tx.error());

        auto delAuction = tx->del(id);
        if (!delAuction.has_value()) {
            (void)tx->rollback();

            return ll::Unexpected(delAuction.error());
        }

        auto commitResult = tx->commit();
        if (!commitResult.has_value()) {
            (void)tx->rollback();

            return ll::Unexpected(commitResult.error());
        }

        auto saleTx = this->mImpl->mSale->tx();
        if (!saleTx.has_value())
            return ll::Unexpected(saleTx.error());

        auto& saleBatch = saleTx.value();
        auto setResult = saleBatch.set(saleKey, StoreSaleCol::store_id, data.at("seller_uuid"));
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());
        setResult = saleBatch.set(saleKey, StoreSaleCol::item_name, data.at("item_name"));
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());
        setResult = saleBatch.set(saleKey, StoreSaleCol::price, static_cast<long long>(price));
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());
        setResult = saleBatch.set(saleKey, StoreSaleCol::tax, static_cast<long long>(tax));
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());
        setResult = saleBatch.set(saleKey, StoreSaleCol::buyer_uuid, bidderUuid);
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());
        setResult = saleBatch.set(saleKey, StoreSaleCol::buyer_name, data.at("bidder_name"));
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());
        setResult = saleBatch.set(saleKey, StoreSaleCol::seller_uuid, data.at("seller_uuid"));
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());
        setResult = saleBatch.set(saleKey, StoreSaleCol::time, SystemUtils::getNowTime());
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());
        setResult = saleBatch.set(saleKey, StoreSaleCol::source, std::string("auction"));
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());

        auto saleCommit = saleBatch.commit();
        if (!saleCommit.has_value())
            return ll::Unexpected(saleCommit.error());

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
        auto tx = this->mImpl->mTable->tx();
        if (!tx.has_value())
            return ll::Unexpected(tx.error());

        auto delResult = tx->del(id);
        if (!delResult.has_value()) {
            (void)tx->rollback();

            return ll::Unexpected(delResult.error());
        }

        auto commitResult = tx->commit();
        if (!commitResult.has_value()) {
            (void)tx->rollback();

            return ll::Unexpected(commitResult.error());
        }

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
        return this->mImpl->mTable->list()
            .and_then([this](const std::vector<std::string>& keys) -> ll::Expected<void> {
                for (const auto& id : keys) {
                    auto row = this->mImpl->mTable->getRow(id);
                    if (!row.has_value()) {
                        this->mImpl->logger->warn("MarketAuction: load auction {} failed: {}", id, row.error().message());

                        continue;
                    }

                    auto data = std::move(row.value());
                    if (data.empty())
                        continue;

                    if (data.at("settled") == "1")
                        continue;

                    if (nowEpochSeconds() < SystemUtils::toLongLong(data.at("end_at"), 0))
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
    }

    ll::Expected<void> MarketAuction::restoreAuction(const std::string& id, const std::unordered_map<std::string, std::string>& data, const std::string& saleKey) {
        if (!saleKey.empty()) {
            auto delSale = this->mImpl->mSale->del(saleKey);
            if (!delSale.has_value())
                return ll::Unexpected(delSale.error());
        }

        auto& t = *this->mImpl->mTable;
        auto tx = t.tx();
        if (!tx.has_value())
            return ll::Unexpected(tx.error());

        auto r = tx->set(id, StoreAuctionCol::seller_uuid, data.at("seller_uuid"))
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(id, StoreAuctionCol::seller_name, data.at("seller_name"));
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(id, StoreAuctionCol::item_type, data.at("item_type"));
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(id, StoreAuctionCol::item_data, data.at("item_data"));
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(id, StoreAuctionCol::item_name, data.at("item_name"));
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(id, StoreAuctionCol::start_price, SystemUtils::toLongLong(data.at("start_price"), 0));
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(id, StoreAuctionCol::current_price, SystemUtils::toLongLong(data.at("current_price"), 0));
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(id, StoreAuctionCol::bidder_uuid, data.at("bidder_uuid"));
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(id, StoreAuctionCol::bidder_name, data.at("bidder_name"));
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(id, StoreAuctionCol::bid_count, SystemUtils::toLongLong(data.at("bid_count"), 0));
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(id, StoreAuctionCol::end_at, SystemUtils::toLongLong(data.at("end_at"), 0));
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(id, StoreAuctionCol::settled, false);
            });

        if (!r.has_value()) {
            (void)tx->rollback();

            return ll::Unexpected(r.error());
        }

        auto c = tx->commit();
        if (!c.has_value()) {
            (void)tx->rollback();

            return ll::Unexpected(c.error());
        }

        return {};
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

        return this->mImpl->mTable->list();
    }

    ll::Expected<std::vector<std::string>> MarketAuction::getAuctionItems(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        if (!this->mImpl->options.StoreAuctionEnabled)
            return {};

        return this->mImpl->mTable->find(FindMode::And, {
            { StoreAuctionCol::seller_uuid, player.getUuid().asString() }
        });
    }

    ll::Expected<std::unordered_map<std::string, std::string>> MarketAuction::getAuctionData(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->mTable->has(id)
            .and_then([this, id](bool exists) -> ll::Expected<std::unordered_map<std::string, std::string>> {
                if (!exists)
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::AuctionNotFound));

                return this->mImpl->mTable->getRow(id);
            });
    }

    MarketAuction::MarketAuction(
        std::shared_ptr<BlockRepository> db,
        std::shared_ptr<BlockRepository> settingsDb,
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
        return mImpl != nullptr && this->mImpl->db != nullptr && this->mImpl->settingsDb != nullptr && this->mImpl->logger != nullptr && this->mImpl->mTable.has_value();
    }
}
