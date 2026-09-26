#include <cmath>
#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
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

#include "LOICollectionA/base/Cache.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/types/market/MarketSchema.h"
#include "LOICollectionA/include/server/Plugins/market/MarketStore.h"
#include "LOICollectionA/include/server/Plugins/market/MarketPlugin.h"

using I18nUtilsTools::tr;
using LOICollection::data::FindMode;

namespace LOICollection::server::Plugins {
    struct MarketStore::RankData {
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> stores;
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> items;
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> sales;
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> reviews;
    };

    struct MarketStore::Impl {
        std::shared_ptr<BlockRepository> db;
        std::shared_ptr<BlockRepository> settingsDb;
        const Config::C_Market& options;
        std::shared_ptr<ll::io::Logger> logger;
        TimerManager& timerManager;
        BlacklistProvider blacklistProvider;
        TaxRateProvider taxRateProvider;
        LRUKCache<std::string, std::vector<std::string>> rankCache;

        std::optional<StoreTable> store;
        std::optional<StoreItemTable> item;
        std::optional<StoreSaleTable> sale;
        std::optional<StoreReviewTable> review;
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
            taxRateProvider(std::move(taxRateProvider_)),
            rankCache(100, 100) {}

        double effectiveTaxRate() const {
            return taxRateProvider ? taxRateProvider() : options.StoreTransactionTaxRate;
        }
    };

    ll::Expected<void> MarketStore::createTables() {
        return StoreTable::open(*this->mImpl->db, "Store")
            .and_then([this](StoreTable table) -> ll::Expected<StoreItemTable> {
                this->mImpl->store.emplace(std::move(table));
                return StoreItemTable::open(*this->mImpl->db, "StoreItem");
            })
            .and_then([this](StoreItemTable table) -> ll::Expected<StoreSaleTable> {
                this->mImpl->item.emplace(std::move(table));
                return StoreSaleTable::open(*this->mImpl->db, "StoreSale");
            })
            .and_then([this](StoreSaleTable table) -> ll::Expected<StoreReviewTable> {
                this->mImpl->sale.emplace(std::move(table));
                return StoreReviewTable::open(*this->mImpl->db, "StoreReview");
            })
            .and_then([this](StoreReviewTable table) -> ll::Expected<void> {
                this->mImpl->review.emplace(std::move(table));
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

    ll::Expected<std::vector<std::string>> MarketStore::getStoreRanking() {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        if (auto cached = this->mImpl->rankCache.get("global"); cached.has_value())
            return *cached.value();

        auto loadAll = [](auto& table) -> ll::Expected<std::unordered_map<std::string, std::unordered_map<std::string, std::string>>> {
            auto keys = table.list();
            if (!keys.has_value())
                return ll::makeStringError(keys.error().message());
            std::unordered_map<std::string, std::unordered_map<std::string, std::string>> out;
            out.reserve(keys.value().size());
            for (const auto& k : keys.value()) {
                auto row = table.getRow(k);
                if (!row.has_value())
                    return ll::makeStringError(row.error().message());
                out.emplace(k, std::move(row.value()));
            }
            return out;
        };

        return loadAll(*this->mImpl->store)
            .and_then([this, &loadAll](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> stores) -> ll::Expected<RankData> {
                RankData data;
                data.stores = std::move(stores);
                return loadAll(*this->mImpl->item).transform([data](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> items) mutable -> RankData {
                    data.items = std::move(items);
                    return data;
                });
            })
            .and_then([this, &loadAll](RankData data) -> ll::Expected<RankData> {
                return loadAll(*this->mImpl->sale).transform([data](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> sales) mutable -> RankData {
                    data.sales = std::move(sales);
                    return data;
                });
            })
            .and_then([this, &loadAll](RankData data) -> ll::Expected<RankData> {
                return loadAll(*this->mImpl->review).transform([data](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> reviews) mutable -> RankData {
                    data.reviews = std::move(reviews);
                    return data;
                });
            })
            .transform([this](RankData data) -> std::vector<std::string> {
                const Config::C_Market& options = this->mImpl->options;
                static constexpr long long DAY_SECONDS = 86'400LL;

                std::string nowTime = SystemUtils::getNowTime();
                long long txWindow = static_cast<long long>(std::max(0, options.StoreTransactionWindowDays)) * DAY_SECONDS;
                long long ratingWindow = static_cast<long long>(std::max(0, options.StoreRatingWindowDays)) * DAY_SECONDS;
                long long riskWindow = static_cast<long long>(std::max(0, options.StoreRiskWindowDays)) * DAY_SECONDS;

                std::unordered_map<std::string, int> itemCount;
                for (const auto& [key, row] : data.items)
                    itemCount[row.at("store_id")]++;

                std::unordered_map<std::string, int> saleCount;
                std::unordered_map<std::string, long long> saleVolume;
                std::unordered_map<std::string, int> pairTradeCount;
                for (const auto& [key, row] : data.sales) {
                    long long t = SystemUtils::toLongLong(SystemUtils::getTimeSpan(nowTime, row.at("time"), ""), 0);
                    if (t > txWindow)
                        continue;

                    const std::string& storeId = row.at("store_id");
                    long long price = SystemUtils::toLongLong(row.at("price"), 0);

                    std::string seller = row.contains("seller_uuid") ? row.at("seller_uuid") : "";
                    if (seller.empty()) {
                        auto ownerIt = data.stores.find(storeId);
                        if (ownerIt != data.stores.end())
                            seller = ownerIt->second.at("owner_uuid");
                    }
                    std::string buyer = row.contains("buyer_uuid") ? row.at("buyer_uuid") : "";
                    if (!seller.empty() && !buyer.empty() && seller == buyer)
                        continue;

                    int repeatLimit = std::max(0, options.StoreRepeatTradeLimit);
                    if (repeatLimit > 0 && !seller.empty() && !buyer.empty()) {
                        std::string pair = buyer + "|" + seller;
                        if (++pairTradeCount[pair] > repeatLimit)
                            continue;
                    }

                    saleCount[storeId]++;
                    saleVolume[storeId] += price;
                }

                std::unordered_map<std::string, int> approvedCount;
                std::unordered_map<std::string, double> approvedSum;
                std::unordered_map<std::string, int> approved180;
                std::unordered_map<std::string, int> riskCount;
                std::unordered_map<std::string, int> riskBad;
                double globalSum = 0.0;
                int globalCount = 0;

                for (const auto& [key, row] : data.reviews) {
                    if (row.at("status") != "approved")
                        continue;

                    long long rating = SystemUtils::toLongLong(row.at("rating"), 0);
                    long long t = SystemUtils::toLongLong(SystemUtils::getTimeSpan(nowTime, row.at("time"), ""), 0);
                    const std::string& storeId = row.at("store_id");

                    approvedCount[storeId]++;
                    approvedSum[storeId] += rating;
                    globalSum += rating;
                    globalCount++;

                    if (t <= ratingWindow)
                        approved180[storeId]++;

                    if (t <= riskWindow) {
                        riskCount[storeId]++;
                        if (rating <= 2)
                            riskBad[storeId]++;
                    }
                }

                double globalAverage = globalCount > 0 ? globalSum / globalCount : 0.0;

                std::vector<std::pair<double, std::string>> ranked;
                ranked.reserve(data.stores.size());

                for (const auto& [storeId, row] : data.stores) {
                    if (itemCount[storeId] <= 0)
                        continue;

                    StoreScoreInput input;
                    long long ageSeconds = SystemUtils::toLongLong(SystemUtils::getTimeSpan(nowTime, row.at("store_created_at"), ""), 0);
                    input.ageDays = ageSeconds / static_cast<double>(DAY_SECONDS);
                    input.transactions30 = saleCount[storeId];
                    input.volume30 = saleVolume[storeId];
                    input.approvedReviews = approvedCount[storeId];
                    input.approvedAverage = approvedCount[storeId] > 0
                        ? approvedSum[storeId] / approvedCount[storeId]
                        : 0.0;
                    input.approved180 = approved180[storeId];
                    input.badReviews30 = riskBad[storeId];
                    input.reviews30 = riskCount[storeId];
                    input.globalApprovedAverage = globalAverage;

                    ranked.emplace_back(this->computeStoreScore(input, options), storeId);
                }

                std::sort(ranked.begin(), ranked.end(), [&data](const auto& left, const auto& right) -> bool {
                    if (left.first != right.first)
                        return left.first > right.first;

                    const std::string& leftCreated = data.stores.at(left.second).at("store_created_at");
                    const std::string& rightCreated = data.stores.at(right.second).at("store_created_at");
                    if (leftCreated != rightCreated)
                        return leftCreated < rightCreated;

                    return left.second < right.second;
                });

                std::vector<std::string> result;
                result.reserve(ranked.size());
                for (const auto& [score, storeId] : ranked)
                    result.emplace_back(storeId);

                this->mImpl->rankCache.put("global", result);

                return result;
            });
    }

    double MarketStore::computeStoreScore(const StoreScoreInput& input, const Config::C_Market& options) {
        auto log1p = [](double value) -> double {
            return std::log1p(value);
        };

        double age = std::max(0.0, input.ageDays);
        double heatFactor = std::min(1.0, age / 30.0);

        double transactionHeat = (
            options.StoreSalesWeight * log1p(static_cast<double>(input.transactions30)) +
            options.StoreVolumeWeight * log1p(static_cast<double>(input.volume30))
        ) * heatFactor;

        double globalAverage = input.globalApprovedAverage > 0.0 ? input.globalApprovedAverage : 3.0;
        double bayesianRating = input.approvedReviews > 0
            ? (input.approvedAverage * input.approvedReviews + globalAverage * options.StoreRatingSmoothing) /
              (input.approvedReviews + options.StoreRatingSmoothing)
            : globalAverage;
        double ratingQuality = options.StoreRatingWeight * (bayesianRating - 3.0) * log1p(static_cast<double>(input.approved180));

        double coldStart = options.StoreColdStartDays > 0
            ? options.StoreColdStartWeight * std::max(0.0, 1.0 - age / options.StoreColdStartDays)
            : 0.0;

        double riskPenalty = options.StoreBadReviewPenalty *
            (input.reviews30 > 0 ? static_cast<double>(input.badReviews30) / input.reviews30 : 0.0);

        return transactionHeat + ratingQuality + coldStart - riskPenalty;
    }

    ll::Expected<std::unordered_map<std::string, std::string>> MarketStore::getStore(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->store->has(id)
            .and_then([this, id](bool exists) -> ll::Expected<std::unordered_map<std::string, std::string>> {
                if (!exists)
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreNotFound));

                return this->mImpl->store->getRow(id);
            });
    }

    ll::Expected<std::unordered_map<std::string, std::string>> MarketStore::getStore(Player& player) {
        return this->getStore(player.getUuid().asString());
    }

    ll::Expected<std::vector<std::string>> MarketStore::getStoreItems(const std::string& storeId) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        if (!this->mImpl->options.StoreEnabled)
            return {};

        return this->mImpl->item->find(FindMode::And, {{StoreItemCol::store_id, storeId}});
    }

    ll::Expected<std::unordered_map<std::string, std::string>> MarketStore::getStoreItemData(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->item->has(id)
            .and_then([this, id](bool exists) -> ll::Expected<std::unordered_map<std::string, std::string>> {
                if (!exists)
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreItemNotFound));

                return this->mImpl->item->getRow(id);
            });
    }

    ll::Expected<bool> MarketStore::hasPurchasedInStore(Player& player, const std::string& storeId) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        if (!this->mImpl->options.StoreEnabled)
            return false;

        return this->mImpl->sale->find(FindMode::And, {
            {StoreSaleCol::store_id, storeId},
            {StoreSaleCol::buyer_uuid, player.getUuid().asString()}
        })
            .transform([](const std::vector<std::string>& keys) -> bool {
                return !keys.empty();
            });
    }

    ll::Expected<bool> MarketStore::createStore(Player& player, const std::string& name, const std::string& icon, const std::string& introduce) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        if (!this->mImpl->options.StoreEnabled)
            return false;

        std::string mUuid = player.getUuid().asString();
        int mCost = this->mImpl->options.StoreCreationCost;

        return this->mImpl->store->has(mUuid)
            .and_then([this, mUuid, mCost, &player, name, icon, introduce](bool exists) -> ll::Expected<bool> {
                if (exists)
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreAlreadyExists));

                if (mCost > 0 && ScoreboardUtils::getScore(player, this->mImpl->options.TargetScoreboard) < mCost)
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreCostInsufficient));

                if (mCost > 0)
                    ScoreboardUtils::reduceScore(player, this->mImpl->options.TargetScoreboard, mCost);

                std::unordered_map<std::string, std::string> data = {
                    {"name", name},
                    {"introduce", introduce},
                    {"icon", icon},
                    {"owner_uuid", mUuid},
                    {"owner_name", player.getRealName()},
                    {"store_created_at", SystemUtils::getNowTime()}
                };

                return this->mImpl->store->setRow(mUuid, data)
                    .or_else([this, mCost, &player](ll::Error e) -> ll::Expected<void> {
                        if (mCost > 0)
                            ScoreboardUtils::addScore(player, this->mImpl->options.TargetScoreboard, mCost);

                        return ll::Unexpected(e);
                    })
                    .transform([this, mUuid, &player, name]() -> bool {
                        this->clearRankCache();
                        this->mImpl->logger->info(fmt::runtime(tr({}, "market.log9")), player.getRealName(), name);

                        return true;
                    });
            });
    }

    ll::Expected<bool> MarketStore::dissolveStore(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        if (!this->mImpl->options.StoreEnabled)
            return false;

        std::string mUuid = player.getUuid().asString();

        return this->mImpl->store->has(mUuid)
            .and_then([this, mUuid, &player](bool exists) -> ll::Expected<bool> {
                if (!exists)
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreNotFound));

                return this->mImpl->item->find(FindMode::And, {{StoreItemCol::store_id, mUuid}})
                    .and_then([this, mUuid, &player](const std::vector<std::string>& items) -> ll::Expected<bool> {
                        if (!items.empty())
                            return false;

                        return this->mImpl->store->del(mUuid)
                            .transform([this, &player]() -> bool {
                                this->clearRankCache();
                                this->mImpl->logger->info(fmt::runtime(tr({}, "market.log10")), player.getRealName());

                                return true;
                            });
                    });
            });
    }

    ll::Expected<bool> MarketStore::uploadStoreItem(Player& player, int slot, const std::string& name, const std::string& icon, const std::string& intr, int score) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        if (!this->mImpl->options.StoreEnabled)
            return false;

        ItemStack mItemStack = player.mInventory->mInventory->getItem(slot);
        if (!mItemStack || mItemStack.isNull())
            return false;

        std::vector<std::string> prohibitedItems = this->mImpl->options.ProhibitedItems;
        if (std::find(prohibitedItems.begin(), prohibitedItems.end(), mItemStack.getTypeName()) != prohibitedItems.end())
            return false;

        if (name.empty() || icon.empty() || intr.empty())
            return false;

        std::string mUuid = player.getUuid().asString();

        return this->mImpl->store->has(mUuid)
            .and_then([this, mUuid, slot, &player, name, icon, intr, score, &mItemStack](bool exists) -> ll::Expected<bool> {
                if (!exists)
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreNotFound));

                return this->mImpl->item->find(FindMode::And, {{StoreItemCol::store_id, mUuid}})
                    .and_then([this, mUuid, slot, &player, name, icon, intr, score, &mItemStack](const std::vector<std::string>& items) -> ll::Expected<bool> {
                        if (static_cast<int>(items.size()) >= this->mImpl->options.StoreMaximumItems)
                            return false;

                        std::string itemKey = SystemUtils::getCurrentTimestamp();
                        std::unordered_map<std::string, std::string> data = {
                            {"store_id", mUuid},
                            {"name", name},
                            {"icon", icon},
                            {"introduce", intr},
                            {"data", mItemStack.save(*SaveContextFactory::createCloneSaveContext())->toSnbt(SnbtFormat::Minimize, 0)}
                        };

                        return this->mImpl->item->setRow(itemKey, data)
                            .and_then([this, itemKey, score]() -> ll::Expected<void> {
                                return this->mImpl->item->set(itemKey, StoreItemCol::score, static_cast<long long>(score));
                            })
                            .transform([this, slot, &player, name]() -> bool {
                                player.mInventory->mInventory->removeItem(slot, 64);
                                player.refreshInventory();

                                this->clearRankCache();
                                this->mImpl->logger->info(fmt::runtime(tr({}, "market.log11")), player.getRealName(), name);

                                return true;
                            });
                    });
            });
    }

    ll::Expected<bool> MarketStore::offshelfStoreItem(Player& player, const std::string& id, bool returnItem) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        if (!this->mImpl->options.StoreEnabled)
            return false;

        return this->getStoreItemData(id)
            .and_then([this, id, returnItem, &player](std::unordered_map<std::string, std::string> data) -> ll::Expected<bool> {
                return this->getStore(data.at("store_id"))
                    .and_then([this, id, returnItem, &player, data](std::unordered_map<std::string, std::string> store) -> ll::Expected<bool> {
                        bool mIsAdmin = player.getCommandPermissionLevel() >= CommandPermissionLevel::GameDirectors;
                        if (store.at("owner_uuid") != player.getUuid().asString() && !mIsAdmin)
                            return false;

                        if (returnItem && store.at("owner_uuid") == player.getUuid().asString()) {
                            ItemStack mItemStack = ItemStack::fromTag(CompoundTag::fromSnbt(data.at("data"))->mTags);
                            InventoryUtils::giveItem(player, mItemStack, static_cast<int>(mItemStack.mCount));
                            player.refreshInventory();
                        }

                        return this->mImpl->item->del(id)
                            .transform([this, &player, data]() -> bool {
                                this->clearRankCache();
                                this->mImpl->logger->info(fmt::runtime(tr({}, "market.log12")), player.getRealName(), data.at("name"));

                                return true;
                            });
                    });
            });
    }

    ll::Expected<bool> MarketStore::buyStoreItem(Player& player, const std::string& id, int count) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        if (!this->mImpl->options.StoreEnabled)
            return false;

        if (count <= 0 || (count > 1 && !this->mImpl->options.StorePartialBuyEnabled))
            return false;

        return this->getStoreItemData(id)
            .and_then([this, id, count, &player](std::unordered_map<std::string, std::string> data) -> ll::Expected<bool> {
                std::string storeId = data.at("store_id");

                return this->getStore(storeId)
                    .and_then([this, id, count, &player, data, storeId](std::unordered_map<std::string, std::string> store) -> ll::Expected<bool> {
                        std::string ownerUuid = store.at("owner_uuid");

                        return this->mImpl->blacklistProvider(ownerUuid)
                            .and_then([this, id, count, &player, data, storeId, ownerUuid](const std::vector<std::string>& blacklists) -> ll::Expected<bool> {
                                if (std::find(blacklists.begin(), blacklists.end(), player.getUuid().asString()) != blacklists.end()) {
                                    return LanguagePlugin::getShared()->getLanguage(player)
                                        .transform([&player](const std::string& language) -> bool {
                                            player.sendMessage(tr(language, "market.gui.error"));

                                            return false;
                                        });
                                }

                                int unitScore = SystemUtils::toInt(data.at("score"), 0);
                                ItemStack mFullStack = ItemStack::fromTag(CompoundTag::fromSnbt(data.at("data"))->mTags);
                                int stackCount = static_cast<int>(mFullStack.mCount);
                                if (count > stackCount)
                                    return false;

                                bool isFull = count == stackCount;
                                int mScore = isFull
                                    ? unitScore
                                    : static_cast<int>(static_cast<long long>(unitScore) * count / stackCount);

                                std::string remainingData;
                                if (!isFull) {
                                    mFullStack.mCount = static_cast<unsigned char>(stackCount - count);
                                    remainingData = mFullStack.save(*SaveContextFactory::createCloneSaveContext())->toSnbt(SnbtFormat::Minimize, 0);
                                }

                                std::string mScoreboard = this->mImpl->options.TargetScoreboard;

                                if (ScoreboardUtils::getScore(player, mScoreboard) < mScore) {
                                    return LanguagePlugin::getShared()->getLanguage(player)
                                        .transform([&player](const std::string& language) -> bool {
                                            player.sendMessage(tr(language, "market.gui.sell.sellItem.tips3"));

                                            return false;
                                        });
                                }

                                int tax = static_cast<int>(std::floor(mScore * this->mImpl->effectiveTaxRate()));
                                int sellerAmount = mScore - tax;

                                return this->commitStoreSale(player, id, data, storeId, ownerUuid, tax, remainingData)
                                    .and_then([this, id, count, &player, data, ownerUuid, mScore, sellerAmount, tax, mScoreboard](const std::string& saleKey) -> ll::Expected<bool> {
                                        auto compensate = [this, id, &player, data, saleKey]() -> ll::Expected<void> {
                                            this->mImpl->logger->warn(fmt::runtime(tr({}, "market.log16")), player.getRealName(), id);

                                            return this->restoreStoreSale(id, data, saleKey);
                                        };

                                        ScoreboardUtils::reduceScore(player, mScoreboard, mScore);

                                        ItemStack mItemStack = ItemStack::fromTag(CompoundTag::fromSnbt(data.at("data"))->mTags);
                                        InventoryUtils::giveItem(player, mItemStack, count);

                                        player.refreshInventory();

                                        return this->settleSeller(ownerUuid, data.at("name"), sellerAmount, mScoreboard)
                                            .or_else([&compensate](ll::Error e) -> ll::Expected<void> {
                                                return compensate().and_then([e = std::move(e)]() mutable -> ll::Expected<void> {
                                                    return ll::Unexpected(std::move(e));
                                                });
                                            })
                                            .transform([]() -> bool {
                                                return true;
                                            })
                                            .and_then([this, tax](bool) -> ll::Expected<bool> {
                                                if (tax <= 0)
                                                    return true;

                                                return this->mImpl->tax->get<std::string>("total", "total", "0")
                                                    .and_then([this, tax](const std::string& value) -> ll::Expected<bool> {
                                                        long long total = SystemUtils::toLongLong(value, 0) + tax;

                                                        return this->mImpl->tax->set("total", "total", std::to_string(total))
                                                            .transform([]() -> bool {
                                                                return true;
                                                            });
                                                    });
                                            })
                                            .transform([this, &player, data, ownerUuid, saleKey, tax](bool) -> bool {
                                                this->mImpl->logger->info(fmt::runtime(tr({}, "market.log13")), data.at("name"));

                                                ll::event::EventBus::getInstance().publish(LOICollection::server::Events::MarketItemSoldEvent(
                                                    data.at("name"),
                                                    SystemUtils::toInt(data.at("score"), 0),
                                                    tax,
                                                    player.getUuid().asString(),
                                                    ownerUuid,
                                                    SystemUtils::toLongLong(saleKey, 0)
                                                ));

                                                return true;
                                            });
                                    });
                            });
                    });
            });
    }

    ll::Expected<std::string> MarketStore::commitStoreSale(
        Player& player,
        const std::string& id,
        const std::unordered_map<std::string, std::string>& data,
        const std::string& storeId,
        const std::string& ownerUuid,
        int tax,
        const std::string& remainingData
    ) {
        std::string saleKey = SystemUtils::getCurrentTimestamp();

        auto saleTx = this->mImpl->sale->tx();
        if (!saleTx.has_value())
            return ll::Unexpected(saleTx.error());

        auto& saleBatch = saleTx.value();
        auto setResult = saleBatch.set(saleKey, StoreSaleCol::store_id, storeId);
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());
        setResult = saleBatch.set(saleKey, StoreSaleCol::item_name, data.at("name"));
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());
        setResult = saleBatch.set(saleKey, StoreSaleCol::price, static_cast<long long>(SystemUtils::toInt(data.at("score"), 0)));
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());
        setResult = saleBatch.set(saleKey, StoreSaleCol::tax, static_cast<long long>(tax));
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());
        setResult = saleBatch.set(saleKey, StoreSaleCol::buyer_uuid, player.getUuid().asString());
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());
        setResult = saleBatch.set(saleKey, StoreSaleCol::buyer_name, player.getRealName());
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());
        setResult = saleBatch.set(saleKey, StoreSaleCol::seller_uuid, ownerUuid);
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());
        setResult = saleBatch.set(saleKey, StoreSaleCol::time, SystemUtils::getNowTime());
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());
        setResult = saleBatch.set(saleKey, StoreSaleCol::source, std::string("store"));
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());

        auto saleCommit = saleBatch.commit();
        if (!saleCommit.has_value())
            return ll::Unexpected(saleCommit.error());

        bool isPartial = !remainingData.empty();
        auto itemTx = this->mImpl->item->tx();
        if (!itemTx.has_value()) {
            auto cr = this->restoreStoreSale(id, data, saleKey);
            if (!cr.has_value())
                return ll::Unexpected(cr.error());
            return ll::Unexpected(itemTx.error());
        }
        auto& itemBatch = itemTx.value();
        auto itemResult = isPartial ? itemBatch.set(id, StoreItemCol::data, remainingData) : itemBatch.del(id);
        if (!itemResult.has_value()) {
            auto cr = this->restoreStoreSale(id, data, saleKey);
            if (!cr.has_value())
                return ll::Unexpected(cr.error());
            return ll::Unexpected(itemResult.error());
        }
        auto itemCommit = itemBatch.commit();
        if (!itemCommit.has_value()) {
            auto cr = this->restoreStoreSale(id, data, saleKey);
            if (!cr.has_value())
                return ll::Unexpected(cr.error());
            return ll::Unexpected(itemCommit.error());
        }

        this->clearRankCache();

        return saleKey;
    }

    ll::Expected<void> MarketStore::restoreStoreSale(
        const std::string& id,
        const std::unordered_map<std::string, std::string>& data,
        const std::string& saleKey
    ) {
        auto itemTx = this->mImpl->item->tx();
        if (!itemTx.has_value())
            return ll::Unexpected(itemTx.error());

        auto itemResult = itemTx.value().set(id, StoreItemCol::data, data.at("data"));
        if (!itemResult.has_value())
            return ll::Unexpected(itemResult.error());

        auto itemCommit = itemTx.value().commit();
        if (!itemCommit.has_value())
            return ll::Unexpected(itemCommit.error());

        auto saleTx = this->mImpl->sale->tx();
        if (!saleTx.has_value())
            return ll::Unexpected(saleTx.error());

        auto saleResult = saleTx.value().del(saleKey);
        if (!saleResult.has_value())
            return ll::Unexpected(saleResult.error());

        return saleTx.value().commit().transform([this](bool) -> void {
            this->clearRankCache();
        });
    }

    ll::Expected<void> MarketStore::settleSeller(const std::string& ownerUuid, const std::string& itemName, int score, const std::string& scoreboard) {
        if (Player* seller = ll::service::getLevel()->getPlayer(mce::UUID::fromString(ownerUuid)); seller) {
            return LanguagePlugin::getShared()->getLanguage(*seller)
                .transform([seller, itemName, scoreboard, score](const std::string& language) -> void {
                    seller->sendMessage(fmt::format(fmt::runtime(tr(language, "market.gui.sell.sellItem.tips1")), itemName));

                    ScoreboardUtils::addScore(*seller, scoreboard, score);
                });
        }

        return this->mImpl->market->get<std::string>(ownerUuid, "score", "0")
            .and_then([this, ownerUuid, score](const std::string& value) -> ll::Expected<void> {
                int mMarketScore = SystemUtils::toInt(value, 0);

                return this->mImpl->market->set(ownerUuid, "score", std::to_string(mMarketScore + score));
            });
    }

    ll::Expected<bool> MarketStore::addReview(Player& player, const std::string& storeId, int rating, const std::string& content) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        if (!this->mImpl->options.StoreEnabled || !this->mImpl->options.StoreReviewEnabled)
            return false;

        if (rating < 1 || rating > 5) {
            return LanguagePlugin::getShared()->getLanguage(player)
                .transform([&player](const std::string& language) -> bool {
                    player.sendMessage(tr(language, "market.gui.store.review.error.invalid"));

                    return false;
                });
        }

        std::string mContent = content;
        size_t mBegin = mContent.find_first_not_of(" \t\r\n");
        if (mBegin == std::string::npos)
            mContent.clear();
        else
            mContent = mContent.substr(mBegin, mContent.find_last_not_of(" \t\r\n") - mBegin + 1);

        if (mContent.empty()) {
            return LanguagePlugin::getShared()->getLanguage(player)
                .transform([&player](const std::string& language) -> bool {
                    player.sendMessage(tr(language, "market.gui.store.review.error.invalid"));

                    return false;
                });
        }

        std::string mUuid = player.getUuid().asString();

        return this->mImpl->store->has(storeId)
            .and_then([this, storeId, &player, rating, mContent, mUuid](bool exists) -> ll::Expected<bool> {
                if (!exists)
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreNotFound));

                return this->mImpl->sale->find(FindMode::And, {
                    {StoreSaleCol::store_id, storeId},
                    {StoreSaleCol::buyer_uuid, mUuid}
                })
                    .and_then([this, storeId, &player, rating, mContent, mUuid](const std::vector<std::string>& purchases) -> ll::Expected<bool> {
                        if (purchases.empty()) {
                            return LanguagePlugin::getShared()->getLanguage(player)
                                .transform([&player](const std::string& language) -> bool {
                                    player.sendMessage(tr(language, "market.gui.store.review.error.nopurchase"));

                                    return false;
                                });
                        }

                        return this->mImpl->review->find(FindMode::And, {
                            {StoreReviewCol::store_id, storeId},
                            {StoreReviewCol::buyer_uuid, mUuid}
                        })
                            .and_then([this, storeId, &player, rating, mContent](const std::vector<std::string>& reviews) -> ll::Expected<bool> {
                                if (!reviews.empty()) {
                                    return LanguagePlugin::getShared()->getLanguage(player)
                                        .transform([&player](const std::string& language) -> bool {
                                            player.sendMessage(tr(language, "market.gui.store.review.error.duplicate"));

                                            return false;
                                        });
                                }

                                std::string reviewKey = SystemUtils::getCurrentTimestamp();
                                std::unordered_map<std::string, std::string> data = {
                                    {"store_id", storeId},
                                    {"buyer_uuid", player.getUuid().asString()},
                                    {"buyer_name", player.getRealName()},
                                    {"content", mContent},
                                    {"status", "pending"},
                                    {"time", SystemUtils::getNowTime()}
                                };

                                return this->mImpl->review->setRow(reviewKey, data)
                                    .and_then([this, reviewKey, rating]() -> ll::Expected<void> {
                                        return this->mImpl->review->set(reviewKey, StoreReviewCol::rating, static_cast<long long>(rating));
                                    })
                                    .transform([this, &player]() -> bool {
                                        this->mImpl->logger->info(fmt::runtime(tr({}, "market.log14")), player.getRealName());

                                        return true;
                                    });
                            });
                    });
            });
    }

    ll::Expected<bool> MarketStore::auditReview(Player& player, const std::string& id, bool approve) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        if (!this->mImpl->options.StoreEnabled || !this->mImpl->options.StoreReviewEnabled)
            return false;

        if (player.getCommandPermissionLevel() < CommandPermissionLevel::GameDirectors)
            return false;

        return this->getReviewData(id)
            .and_then([this, id, approve](std::unordered_map<std::string, std::string> data) -> ll::Expected<bool> {
                if (data.at("status") != "pending")
                    return false;

                return this->mImpl->review->set(id, StoreReviewCol::status, approve ? std::string("approved") : std::string("rejected"))
                    .transform([this, approve]() -> bool {
                        if (approve)
                            this->clearRankCache();

                        this->mImpl->logger->info(fmt::runtime(tr({}, "market.log15")), approve ? "approved" : "rejected");

                        return true;
                    });
            });
    }

    ll::Expected<std::vector<std::string>> MarketStore::getReviews(const std::string& storeId, MarketStoreReviewStatus status) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        if (!this->mImpl->options.StoreEnabled || !this->mImpl->options.StoreReviewEnabled)
            return {};

        std::string mStatus = status == MarketStoreReviewStatus::approved
            ? "approved"
            : (status == MarketStoreReviewStatus::rejected ? "rejected" : "pending");

        return this->mImpl->review->find(FindMode::And, {
            {StoreReviewCol::store_id, storeId},
            {StoreReviewCol::status, mStatus}
        });
    }

    ll::Expected<std::unordered_map<std::string, std::string>> MarketStore::getReviewData(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->review->has(id)
            .and_then([this, id](bool exists) -> ll::Expected<std::unordered_map<std::string, std::string>> {
                if (!exists)
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreReviewNotFound));

                return this->mImpl->review->getRow(id);
            });
    }

    void MarketStore::clearRankCache() {
        this->mImpl->rankCache.erase("global");
    }

    void MarketStore::startRankRefresh() {
        if (this->mImpl->options.StoreRankRefreshMinutes <= 0)
            return;

        this->mImpl->timerManager.loopSchedule("StoreRankRefresh", std::chrono::minutes(this->mImpl->options.StoreRankRefreshMinutes), [this]() -> void {
            this->clearRankCache();
        });
    }

    StoreTable& MarketStore::stores() {
        return *this->mImpl->store;
    }

    StoreItemTable& MarketStore::items() {
        return *this->mImpl->item;
    }

    StoreSaleTable& MarketStore::sales() {
        return *this->mImpl->sale;
    }

    StoreReviewTable& MarketStore::reviews() {
        return *this->mImpl->review;
    }

    void MarketStore::unload() {
        this->mImpl->store.reset();
        this->mImpl->item.reset();
        this->mImpl->sale.reset();
        this->mImpl->review.reset();
    }

    MarketStore::MarketStore(
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

    MarketStore::~MarketStore() = default;

    bool MarketStore::isValid() const {
        return mImpl != nullptr && this->mImpl->db != nullptr && this->mImpl->settingsDb != nullptr && this->mImpl->logger != nullptr
            && this->mImpl->store.has_value() && this->mImpl->item.has_value()
            && this->mImpl->sale.has_value() && this->mImpl->review.has_value();
    }
}
