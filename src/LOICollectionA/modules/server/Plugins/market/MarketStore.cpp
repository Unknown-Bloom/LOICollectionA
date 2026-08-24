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

#include "LOICollectionA/data/SQLiteStorage.h"

#include "LOICollectionA/base/Cache.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/market/MarketStore.h"
#include "LOICollectionA/include/server/Plugins/market/MarketPlugin.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    struct MarketStore::RankData {
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> stores;
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> items;
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> sales;
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> reviews;
    };

    struct MarketStore::Impl {
        std::shared_ptr<SQLiteStorage> db;
        std::shared_ptr<SQLiteStorage> settingsDb;
        const Config::C_Market& options;
        std::shared_ptr<ll::io::Logger> logger;
        TimerManager& timerManager;
        BlacklistProvider blacklistProvider;
        TaxRateProvider taxRateProvider;
        LRUKCache<std::string, std::vector<std::string>> rankCache;

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
            taxRateProvider(std::move(taxRateProvider_)),
            rankCache(100, 100) {}

        double effectiveTaxRate() const {
            return taxRateProvider ? taxRateProvider() : options.StoreTransactionTaxRate;
        }
    };

    ll::Expected<void> MarketStore::createTables() {
        return this->mImpl->db->create("Store", [](SQLiteStorage::ColumnCallback ctor) -> void {
            ctor("name");
            ctor("introduce");
            ctor("icon");
            ctor("owner_uuid");
            ctor("owner_name");
            ctor("store_created_at");
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->db->create("StoreItem", [](SQLiteStorage::ColumnCallback ctor) -> void {
                ctor("store_id");
                ctor("name");
                ctor("icon");
                ctor("introduce");
                ctor("score");
                ctor("data");
            });
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->db->create("StoreSale", [](SQLiteStorage::ColumnCallback ctor) -> void {
                ctor("store_id");
                ctor("item_name");
                ctor("price");
                ctor("buyer_uuid");
                ctor("buyer_name");
                ctor("time");
            });
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->db->create("StoreReview", [](SQLiteStorage::ColumnCallback ctor) -> void {
                ctor("store_id");
                ctor("buyer_uuid");
                ctor("buyer_name");
                ctor("rating");
                ctor("content");
                ctor("status");
                ctor("time");
            });
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->db->exec("CREATE INDEX IF NOT EXISTS idx_StoreSale_time ON StoreSale(time);")
                .and_then([this]() -> ll::Expected<void> {
                    return this->mImpl->db->exec("CREATE INDEX IF NOT EXISTS idx_StoreSale_item_name ON StoreSale(item_name);");
                })
                .and_then([this]() -> ll::Expected<void> {
                    return this->mImpl->db->exec("CREATE INDEX IF NOT EXISTS idx_StoreItem_name ON StoreItem(name);");
                });
        });
    }

    ll::Expected<std::vector<std::string>> MarketStore::getStoreRanking() {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        if (auto cached = this->mImpl->rankCache.get("global"); cached.has_value())
            return *cached.value();

        auto loadTable = [this](const std::string& table) -> ll::Expected<std::unordered_map<std::string, std::unordered_map<std::string, std::string>>> {
            return this->mImpl->db->list(table)
                .and_then([this, table](const std::vector<std::string>& keys) -> ll::Expected<std::unordered_map<std::string, std::unordered_map<std::string, std::string>>> {
                    return this->mImpl->db->get(table, keys);
                });
        };

        return loadTable("Store")
            .and_then([](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> stores) -> ll::Expected<RankData> {
                RankData data;
                data.stores = std::move(stores);

                return data;
            })
            .and_then([&loadTable](RankData data) -> ll::Expected<RankData> {
                return loadTable("StoreItem").transform([data](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> items) mutable -> RankData {
                    data.items = std::move(items);
                    return data;
                });
            })
            .and_then([&loadTable](RankData data) -> ll::Expected<RankData> {
                return loadTable("StoreSale").transform([data](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> sales) mutable -> RankData {
                    data.sales = std::move(sales);
                    return data;
                });
            })
            .and_then([&loadTable](RankData data) -> ll::Expected<RankData> {
                return loadTable("StoreReview").transform([data](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> reviews) mutable -> RankData {
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
                    long long t = SystemUtils::toInt(SystemUtils::getTimeSpan(nowTime, row.at("time"), ""), 0);
                    if (t > txWindow)
                        continue;

                    const std::string& storeId = row.at("store_id");
                    int price = SystemUtils::toInt(row.at("price"), 0);

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

                    int rating = SystemUtils::toInt(row.at("rating"), 0);
                    long long t = SystemUtils::toInt(SystemUtils::getTimeSpan(nowTime, row.at("time"), ""), 0);
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
                    long long ageSeconds = SystemUtils::toInt(SystemUtils::getTimeSpan(nowTime, row.at("store_created_at"), ""), 0);
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

        return this->mImpl->db->has("Store", id)
            .and_then([this, id](bool exists) -> ll::Expected<std::unordered_map<std::string, std::string>> {
                if (!exists)
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreNotFound));

                return this->mImpl->db->get("Store", id);
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

        return this->mImpl->db->find("StoreItem", {
            { "store_id", storeId }
        }, SQLiteStorage::FindCondition::AND);
    }

    ll::Expected<std::unordered_map<std::string, std::string>> MarketStore::getStoreItemData(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->db->has("StoreItem", id)
            .and_then([this, id](bool exists) -> ll::Expected<std::unordered_map<std::string, std::string>> {
                if (!exists)
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreItemNotFound));

                return this->mImpl->db->get("StoreItem", id);
            });
    }

    ll::Expected<bool> MarketStore::hasPurchasedInStore(Player& player, const std::string& storeId) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        if (!this->mImpl->options.StoreEnabled)
            return false;

        return this->mImpl->db->find("StoreSale", {
            { "store_id", storeId },
            { "buyer_uuid", player.getUuid().asString() }
        }, SQLiteStorage::FindCondition::AND)
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

        return this->mImpl->db->has("Store", mUuid)
            .and_then([this, mUuid, mCost, &player, name, icon, introduce](bool exists) -> ll::Expected<bool> {
                if (exists)
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreAlreadyExists));

                if (mCost > 0 && ScoreboardUtils::getScore(player, this->mImpl->options.TargetScoreboard) < mCost)
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreCostInsufficient));

                if (mCost > 0)
                    ScoreboardUtils::reduceScore(player, this->mImpl->options.TargetScoreboard, mCost);

                std::unordered_map<std::string, std::string> data = {
                    { "name", name },
                    { "introduce", introduce },
                    { "icon", icon },
                    { "owner_uuid", mUuid },
                    { "owner_name", player.getRealName() },
                    { "store_created_at", SystemUtils::getNowTime() }
                };

                return this->mImpl->db->set("Store", mUuid, data)
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

        return this->mImpl->db->has("Store", mUuid)
            .and_then([this, mUuid, &player](bool exists) -> ll::Expected<bool> {
                if (!exists)
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreNotFound));

                return this->mImpl->db->find("StoreItem", {
                    { "store_id", mUuid }
                }, SQLiteStorage::FindCondition::AND)
                    .and_then([this, mUuid, &player](const std::vector<std::string>& items) -> ll::Expected<bool> {
                        if (!items.empty())
                            return false;

                        return this->mImpl->db->del("Store", mUuid)
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

        return this->mImpl->db->has("Store", mUuid)
            .and_then([this, mUuid, slot, &player, name, icon, intr, score, &mItemStack](bool exists) -> ll::Expected<bool> {
                if (!exists)
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreNotFound));

                return this->mImpl->db->find("StoreItem", {
                    { "store_id", mUuid }
                }, SQLiteStorage::FindCondition::AND)
                    .and_then([this, mUuid, slot, &player, name, icon, intr, score, &mItemStack](const std::vector<std::string>& items) -> ll::Expected<bool> {
                        if (static_cast<int>(items.size()) >= this->mImpl->options.StoreMaximumItems)
                            return false;

                        std::unordered_map<std::string, std::string> data = {
                            { "store_id", mUuid },
                            { "name", name },
                            { "icon", icon },
                            { "introduce", intr },
                            { "score", std::to_string(score) },
                            { "data", mItemStack.save(*SaveContextFactory::createCloneSaveContext())->toSnbt(SnbtFormat::Minimize, 0) }
                        };

                        return this->mImpl->db->set("StoreItem", SystemUtils::getCurrentTimestamp(), data)
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

                        return this->mImpl->db->del("StoreItem", id)
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

                                                return this->mImpl->settingsDb->get("MarketTax", "total", "total", "0")
                                                    .and_then([this, tax](const std::string& value) -> ll::Expected<bool> {
                                                        long long total = SystemUtils::toLongLong(value, 0) + tax;

                                                        return this->mImpl->settingsDb->set("MarketTax", "total", "total", std::to_string(total))
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
        auto transaction = SQLiteStorageTransaction::create(*this->mImpl->db);
        if (!transaction.has_value())
            return ll::Unexpected(transaction.error());

        auto conn = transaction.value().connection();
        std::string saleKey = SystemUtils::getCurrentTimestamp();

        std::unordered_map<std::string, std::string> sale = {
            { "store_id", storeId },
            { "item_name", data.at("name") },
            { "price", data.at("score") },
            { "tax", std::to_string(tax) },
            { "buyer_uuid", player.getUuid().asString() },
            { "buyer_name", player.getRealName() },
            { "seller_uuid", ownerUuid },
            { "time", SystemUtils::getNowTime() }
        };

        auto setResult = this->mImpl->db->set(conn, "StoreSale", saleKey, sale);
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());

        bool isPartial = !remainingData.empty();
        std::unordered_map<std::string, std::string> updated = data;
        if (isPartial)
            updated["data"] = remainingData;

        auto updateResult = isPartial
            ? this->mImpl->db->set(conn, "StoreItem", id, updated)
            : this->mImpl->db->del(conn, "StoreItem", id);
        if (!updateResult.has_value())
            return ll::Unexpected(updateResult.error());

        auto commitResult = transaction.value().commit();
        if (!commitResult.has_value())
            return ll::Unexpected(commitResult.error());

        this->clearRankCache();

        return saleKey;
    }

    ll::Expected<void> MarketStore::restoreStoreSale(
        const std::string& id,
        const std::unordered_map<std::string, std::string>& data,
        const std::string& saleKey
    ) {
        auto transaction = SQLiteStorageTransaction::create(*this->mImpl->db);
        if (!transaction.has_value())
            return ll::Unexpected(transaction.error());

        auto conn = transaction.value().connection();

        auto setResult = this->mImpl->db->set(conn, "StoreItem", id, data);
        if (!setResult.has_value())
            return ll::Unexpected(setResult.error());

        auto delResult = this->mImpl->db->del(conn, "StoreSale", saleKey);
        if (!delResult.has_value())
            return ll::Unexpected(delResult.error());

        return transaction.value().commit().transform([this](bool) -> void {
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

        return this->mImpl->settingsDb->get("Market", ownerUuid, "score", "0")
            .and_then([this, ownerUuid, score](const std::string& value) -> ll::Expected<void> {
                int mMarketScore = SystemUtils::toInt(value, 0);

                return this->mImpl->settingsDb->set("Market", ownerUuid, "score", std::to_string(mMarketScore + score));
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

        return this->mImpl->db->has("Store", storeId)
            .and_then([this, storeId, &player, rating, mContent, mUuid](bool exists) -> ll::Expected<bool> {
                if (!exists)
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreNotFound));

                return this->mImpl->db->find("StoreSale", {
                    { "store_id", storeId },
                    { "buyer_uuid", mUuid }
                }, SQLiteStorage::FindCondition::AND)
                    .and_then([this, storeId, &player, rating, mContent, mUuid](const std::vector<std::string>& purchases) -> ll::Expected<bool> {
                        if (purchases.empty()) {
                            return LanguagePlugin::getShared()->getLanguage(player)
                                .transform([&player](const std::string& language) -> bool {
                                    player.sendMessage(tr(language, "market.gui.store.review.error.nopurchase"));

                                    return false;
                                });
                        }

                        return this->mImpl->db->find("StoreReview", {
                            { "store_id", storeId },
                            { "buyer_uuid", mUuid }
                        }, SQLiteStorage::FindCondition::AND)
                            .and_then([this, storeId, &player, rating, mContent](const std::vector<std::string>& reviews) -> ll::Expected<bool> {
                                if (!reviews.empty()) {
                                    return LanguagePlugin::getShared()->getLanguage(player)
                                        .transform([&player](const std::string& language) -> bool {
                                            player.sendMessage(tr(language, "market.gui.store.review.error.duplicate"));

                                            return false;
                                        });
                                }

                                std::unordered_map<std::string, std::string> data = {
                                    { "store_id", storeId },
                                    { "buyer_uuid", player.getUuid().asString() },
                                    { "buyer_name", player.getRealName() },
                                    { "rating", std::to_string(rating) },
                                    { "content", mContent },
                                    { "status", "pending" },
                                    { "time", SystemUtils::getNowTime() }
                                };

                                return this->mImpl->db->set("StoreReview", SystemUtils::getCurrentTimestamp(), data)
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

                return this->mImpl->db->set("StoreReview", id, "status", approve ? "approved" : "rejected")
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

        return this->mImpl->db->find("StoreReview", {
            { "store_id", storeId },
            { "status", mStatus }
        }, SQLiteStorage::FindCondition::AND);
    }

    ll::Expected<std::unordered_map<std::string, std::string>> MarketStore::getReviewData(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->db->has("StoreReview", id)
            .and_then([this, id](bool exists) -> ll::Expected<std::unordered_map<std::string, std::string>> {
                if (!exists)
                    return ll::makeErrorCodeError(MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreReviewNotFound));

                return this->mImpl->db->get("StoreReview", id);
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


    MarketStore::MarketStore(
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

    MarketStore::~MarketStore() = default;

    bool MarketStore::isValid() const {
        return mImpl != nullptr && this->mImpl->db != nullptr && this->mImpl->settingsDb != nullptr && this->mImpl->logger != nullptr;
    }
}
