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

#include <mc/server/commands/CommandPermissionLevel.h>

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
#include "LOICollectionA/include/server/Plugins/market/MarketWanted.h"
#include "LOICollectionA/include/server/Plugins/market/MarketPlugin.h"

using I18nUtilsTools::tr;
using LOICollection::data::FindMode;

namespace LOICollection::server::Plugins {

    static long long nowEpochSeconds() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    struct MarketWanted::Impl {
        std::shared_ptr<BlockRepository> db;
        std::shared_ptr<BlockRepository> settingsDb;
        const Config::C_Market& options;
        std::shared_ptr<ll::io::Logger> logger;
        TimerManager& timerManager;
        BlacklistProvider blacklistProvider;
        TaxRateProvider taxRateProvider;
        std::optional<StoreWantedTable> mTable;
        std::optional<StoreSaleTable> mSale;
        std::optional<MarketTable> market;
        std::optional<MarketTaxTable> tax;

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

    ll::Expected<void> MarketWanted::createTables() {
        return StoreWantedTable::open(*this->mImpl->db, "StoreWanted")
            .and_then([this](StoreWantedTable table) -> ll::Expected<StoreSaleTable> {
                this->mImpl->mTable.emplace(std::move(table));

                return StoreSaleTable::open(*this->mImpl->db, "StoreSale");
            })
            .and_then([this](StoreSaleTable table) -> ll::Expected<void> {
                this->mImpl->mSale.emplace(std::move(table));

                return MarketTable::open(*this->mImpl->settingsDb, "Market")
                    .and_then([this](MarketTable table) -> ll::Expected<MarketTaxTable> {
                        this->mImpl->market.emplace(std::move(table));

                        return MarketTaxTable::open(*this->mImpl->settingsDb, "MarketTax");
                    })
                    .and_then([this](MarketTaxTable table) -> ll::Expected<void> {
                        this->mImpl->tax.emplace(std::move(table));

                        return {};
                    });
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

        return this->mImpl->mTable->find(FindMode::And, {
            { StoreWantedCol::wanted_uuid, mUuid }
        })
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

                ScoreboardUtils::reduceScore(player, mScoreboard, static_cast<int>(mFrozen));

                long long mExpireAt = nowEpochSeconds() + static_cast<long long>(this->mImpl->options.StoreWantedExpireDays) * 86'400LL;
                std::string mId = SystemUtils::getCurrentTimestamp();

                auto& t = *this->mImpl->mTable;
                auto tx = t.tx();
                if (!tx.has_value())
                    return ll::Unexpected(tx.error());

                auto r = tx->set(mId, StoreWantedCol::wanted_uuid, mUuid)
                    .and_then([&]() -> ll::Expected<void> {
                        return tx->set(mId, StoreWantedCol::wanted_name, player.getRealName());
                    })
                    .and_then([&]() -> ll::Expected<void> {
                        return tx->set(mId, StoreWantedCol::item_type, mItemStack.getTypeName());
                    })
                    .and_then([&]() -> ll::Expected<void> {
                        return tx->set(mId, StoreWantedCol::item_data, mItemStack.save(*SaveContextFactory::createCloneSaveContext())->toSnbt(SnbtFormat::Minimize, 0));
                    })
                    .and_then([&]() -> ll::Expected<void> {
                        return tx->set(mId, StoreWantedCol::item_name, mName);
                    })
                    .and_then([&]() -> ll::Expected<void> {
                        return tx->set(mId, StoreWantedCol::unit_price, static_cast<long long>(unitPrice));
                    })
                    .and_then([&]() -> ll::Expected<void> {
                        return tx->set(mId, StoreWantedCol::amount_total, static_cast<long long>(amount));
                    })
                    .and_then([&]() -> ll::Expected<void> {
                        return tx->set(mId, StoreWantedCol::amount_filled, 0LL);
                    })
                    .and_then([&]() -> ll::Expected<void> {
                        return tx->set(mId, StoreWantedCol::expire_at, mExpireAt);
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

                this->mImpl->logger->info(fmt::runtime(tr({}, "market.log20")), player.getRealName(), mName);

                return true;
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

                long long expireAt = SystemUtils::toLongLong(data.at("expire_at"), 0);
                if (nowEpochSeconds() >= expireAt)
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::WantedExpired));

                int total = SystemUtils::toInt(data.at("amount_total"), 0);
                int filled = SystemUtils::toInt(data.at("amount_filled"), 0);
                int remaining = total - filled;
                if (remaining <= 0)
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::WantedFilled));
                if (amount > remaining)
                    return false;

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

                        ItemStack target = ItemStack::fromTag(CompoundTag::fromSnbt(data.at("item_data"))->mTags);

                        if (!InventoryUtils::isItemInInventory(player, target, amount))
                            return false;

                        std::string mScoreboard = this->mImpl->options.TargetScoreboard;
                        int unitPrice = SystemUtils::toInt(data.at("unit_price"), 0);
                        long long mPay = static_cast<long long>(unitPrice) * amount;
                        int pay = static_cast<int>(mPay);
                        int tax = static_cast<int>(std::floor(pay * this->mImpl->effectiveTaxRate()));
                        int sellerAmount = pay - tax;

                        return this->commitWantedFill(id, data, amount, pay, tax, player)
                            .and_then([this, data, amount, pay, sellerAmount, tax, buyerUuid, &player, buyer, target](const std::string& saleKey) -> ll::Expected<bool> {
                                InventoryUtils::clearItem(player, target, amount);

                                InventoryUtils::giveItem(*buyer, target, amount);

                                player.refreshInventory();
                                buyer->refreshInventory();

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
        int total = SystemUtils::toInt(data.at("amount_total"), 0);
        int filled = SystemUtils::toInt(data.at("amount_filled"), 0) + amount;

        auto& t = *this->mImpl->mTable;
        auto tx = t.tx();
        if (!tx.has_value())
            return ll::Unexpected(tx.error());

        auto updateResult = (filled >= total)
            ? tx->del(id)
            : tx->set(id, StoreWantedCol::amount_filled, static_cast<long long>(filled));
        if (!updateResult.has_value()) {
            (void)tx->rollback();

            return ll::Unexpected(updateResult.error());
        }

        auto commitResult = tx->commit();
        if (!commitResult.has_value()) {
            (void)tx->rollback();

            return ll::Unexpected(commitResult.error());
        }

        std::string saleKey = SystemUtils::getCurrentTimestamp();

        auto saleTx = this->mImpl->mSale->tx();
        if (!saleTx.has_value())
            return ll::Unexpected(saleTx.error());

        auto& saleBatch = saleTx.value();
        auto setResult = saleBatch.set(saleKey, StoreSaleCol::store_id, data.at("wanted_uuid"));
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());
        setResult = saleBatch.set(saleKey, StoreSaleCol::item_name, data.at("item_name"));
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());
        setResult = saleBatch.set(saleKey, StoreSaleCol::price, static_cast<long long>(pay));
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());
        setResult = saleBatch.set(saleKey, StoreSaleCol::tax, static_cast<long long>(tax));
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());
        setResult = saleBatch.set(saleKey, StoreSaleCol::buyer_uuid, data.at("wanted_uuid"));
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());
        setResult = saleBatch.set(saleKey, StoreSaleCol::buyer_name, data.at("wanted_name"));
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());
        setResult = saleBatch.set(saleKey, StoreSaleCol::seller_uuid, seller.getUuid().asString());
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());
        setResult = saleBatch.set(saleKey, StoreSaleCol::time, SystemUtils::getNowTime());
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());
        setResult = saleBatch.set(saleKey, StoreSaleCol::source, std::string("wanted"));
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());

        auto saleCommit = saleBatch.commit();
        if (!saleCommit.has_value())
            return ll::Unexpected(saleCommit.error());

        return saleKey;
    }

    ll::Expected<void> MarketWanted::restoreWantedFill(
        const std::string& id,
        const std::unordered_map<std::string, std::string>& data,
        int amount,
        const std::string& saleKey
    ) {
        auto delSale = this->mImpl->mSale->del(saleKey);
        if (!delSale.has_value())
            return ll::Unexpected(delSale.error());

        int total = SystemUtils::toInt(data.at("amount_total"), 0);
        int filled = SystemUtils::toInt(data.at("amount_filled"), 0);

        auto& t = *this->mImpl->mTable;
        auto tx = t.tx();
        if (!tx.has_value())
            return ll::Unexpected(tx.error());

        auto r = (filled + amount >= total)
            ? tx->set(id, StoreWantedCol::wanted_uuid, data.at("wanted_uuid"))
                .and_then([&]() -> ll::Expected<void> {
                    return tx->set(id, StoreWantedCol::wanted_name, data.at("wanted_name"));
                })
                .and_then([&]() -> ll::Expected<void> {
                    return tx->set(id, StoreWantedCol::item_type, data.at("item_type"));
                })
                .and_then([&]() -> ll::Expected<void> {
                    return tx->set(id, StoreWantedCol::item_data, data.at("item_data"));
                })
                .and_then([&]() -> ll::Expected<void> {
                    return tx->set(id, StoreWantedCol::item_name, data.at("item_name"));
                })
                .and_then([&]() -> ll::Expected<void> {
                    return tx->set(id, StoreWantedCol::unit_price, SystemUtils::toLongLong(data.at("unit_price"), 0));
                })
                .and_then([&]() -> ll::Expected<void> {
                    return tx->set(id, StoreWantedCol::amount_total, SystemUtils::toLongLong(data.at("amount_total"), 0));
                })
                .and_then([&]() -> ll::Expected<void> {
                    return tx->set(id, StoreWantedCol::amount_filled, SystemUtils::toLongLong(data.at("amount_filled"), 0));
                })
                .and_then([&]() -> ll::Expected<void> {
                    return tx->set(id, StoreWantedCol::expire_at, SystemUtils::toLongLong(data.at("expire_at"), 0));
                })
            : tx->set(id, StoreWantedCol::amount_filled, static_cast<long long>(filled));
        if (!r.has_value()) {
            (void)tx->rollback();

            return ll::Unexpected(r.error());
        }

        auto commitResult = tx->commit();
        if (!commitResult.has_value()) {
            (void)tx->rollback();

            return ll::Unexpected(commitResult.error());
        }

        return {};
    }

    ll::Expected<void> MarketWanted::deleteWanted(const std::string& id) {
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

        return {};
    }

    ll::Expected<void> MarketWanted::restoreWanted(const std::string& id, const std::unordered_map<std::string, std::string>& data) {
        auto& t = *this->mImpl->mTable;
        auto tx = t.tx();
        if (!tx.has_value())
            return ll::Unexpected(tx.error());

        auto r = tx->set(id, StoreWantedCol::wanted_uuid, data.at("wanted_uuid"))
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(id, StoreWantedCol::wanted_name, data.at("wanted_name"));
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(id, StoreWantedCol::item_type, data.at("item_type"));
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(id, StoreWantedCol::item_data, data.at("item_data"));
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(id, StoreWantedCol::item_name, data.at("item_name"));
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(id, StoreWantedCol::unit_price, SystemUtils::toLongLong(data.at("unit_price"), 0));
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(id, StoreWantedCol::amount_total, SystemUtils::toLongLong(data.at("amount_total"), 0));
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(id, StoreWantedCol::amount_filled, SystemUtils::toLongLong(data.at("amount_filled"), 0));
            })
            .and_then([&]() -> ll::Expected<void> {
                return tx->set(id, StoreWantedCol::expire_at, SystemUtils::toLongLong(data.at("expire_at"), 0));
            });

        if (!r.has_value()) {
            (void)tx->rollback();

            return ll::Unexpected(r.error());
        }

        auto commitResult = tx->commit();
        if (!commitResult.has_value()) {
            (void)tx->rollback();

            return ll::Unexpected(commitResult.error());
        }

        return {};
    }

    ll::Expected<void> MarketWanted::refundBuyer(const std::string& buyerUuid, int score, const std::string& scoreboard) {
        if (score <= 0)
            return {};

        if (Player* buyer = ll::service::getLevel()->getPlayer(mce::UUID::fromString(buyerUuid)); buyer) {
            ScoreboardUtils::addScore(*buyer, scoreboard, score);

            return {};
        }

        return this->mImpl->market->get<std::string>(buyerUuid, "score", "0")
            .and_then([this, buyerUuid, score](const std::string& value) -> ll::Expected<void> {
                int mMarketScore = SystemUtils::toInt(value, 0);

                return this->mImpl->market->set(buyerUuid, "score", std::to_string(mMarketScore + score));
            });
    }

    ll::Expected<void> MarketWanted::collectTax(int tax) {
        if (tax <= 0)
            return {};

        return this->mImpl->tax->get<std::string>("total", "total", "0")
            .and_then([this, tax](const std::string& value) -> ll::Expected<void> {
                long long total = SystemUtils::toLongLong(value, 0) + tax;

                return this->mImpl->tax->set("total", "total", std::to_string(total));
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
        return this->mImpl->mTable->list()
            .and_then([this](const std::vector<std::string>& keys) -> ll::Expected<void> {
                std::string mScoreboard = this->mImpl->options.TargetScoreboard;

                for (const auto& id : keys) {
                    auto row = this->mImpl->mTable->getRow(id);
                    if (!row.has_value()) {
                        this->mImpl->logger->warn("MarketWanted: load wanted {} failed: {}", id, row.error().message());

                        continue;
                    }

                    auto data = std::move(row.value());
                    if (data.empty())
                        continue;

                    if (nowEpochSeconds() < SystemUtils::toLongLong(data.at("expire_at"), 0))
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
    }

    ll::Expected<std::vector<std::string>> MarketWanted::getWantedList() {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        if (!this->mImpl->options.StoreWantedEnabled)
            return {};

        return this->mImpl->mTable->list();
    }

    ll::Expected<std::vector<std::string>> MarketWanted::getWantedItems(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        if (!this->mImpl->options.StoreWantedEnabled)
            return {};

        return this->mImpl->mTable->find(FindMode::And, {
            { StoreWantedCol::wanted_uuid, player.getUuid().asString() }
        });
    }

    ll::Expected<std::unordered_map<std::string, std::string>> MarketWanted::getWantedData(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->mTable->has(id)
            .and_then([this, id](bool exists) -> ll::Expected<std::unordered_map<std::string, std::string>> {
                if (!exists)
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::WantedNotFound));

                return this->mImpl->mTable->getRow(id);
            });
    }

    MarketWanted::MarketWanted(
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

    MarketWanted::~MarketWanted() = default;

    bool MarketWanted::isValid() const {
        return mImpl != nullptr && this->mImpl->db != nullptr && this->mImpl->settingsDb != nullptr && this->mImpl->logger != nullptr && this->mImpl->mTable.has_value();
    }
}
