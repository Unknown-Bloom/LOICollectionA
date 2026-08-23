#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <unordered_map>

#include <fmt/core.h>

#include <ll/api/Expected.h>

#include <mc/world/actor/player/Player.h>
#include <mc/server/commands/CommandPermissionLevel.h>

#include "LOICollectionA/include/server/Plugins/LanguagePlugin.h"

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/data/SQLiteStorage.h"

#include "LOICollectionA/frontend/AST.h"

#include "LOICollectionA/include/form/GUIManager.h"

#include "LOICollectionA/include/server/Plugins/market/MarketGui.h"
#include "LOICollectionA/include/server/Plugins/market/MarketGuiDetail.h"
#include "LOICollectionA/include/server/Plugins/market/MarketPlugin.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    void MarketGui::registerStore(MarketPlugin& owner) {
        auto listVisibleStores = [&owner](Player& player) -> ll::Expected<std::vector<std::pair<std::string, std::string>>> {
            return owner.getStoreRanking()
                .and_then([&owner, &player](const std::vector<std::string>& ids) -> ll::Expected<std::vector<std::pair<std::string, std::string>>> {
                    return owner.getDatabase()->get("Store", ids)
                        .and_then([&owner, &player, ids](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> data) -> ll::Expected<std::vector<std::pair<std::string, std::string>>> {
                            std::vector<std::pair<std::string, std::string>> items;
                            std::string mUuid = player.getUuid().asString();

                            for (const std::string& storeId : ids) {
                                auto it = data.find(storeId);
                                if (it == data.end())
                                    continue;

                                auto blacklists = owner.getBlacklist(it->second.at("owner_uuid"));
                                if (!blacklists.has_value())
                                    return ll::Unexpected(blacklists.error());

                                if (std::find(blacklists.value().begin(), blacklists.value().end(), mUuid) != blacklists.value().end())
                                    continue;

                                items.emplace_back(it->second.at("name") + "\n" + it->second.at("owner_name"), storeId);
                            }

                            return items;
                        });
                });
        };

        auto listVisibleStoreItems = [&owner](Player& player, const std::string& storeId) -> ll::Expected<std::vector<std::pair<std::string, std::string>>> {
            return owner.getStore(storeId)
                .and_then([&owner, &player, storeId](std::unordered_map<std::string, std::string> store) -> ll::Expected<std::vector<std::pair<std::string, std::string>>> {
                    return owner.getBlacklist(store.at("owner_uuid"))
                        .and_then([&owner, &player, storeId](const std::vector<std::string>& blacklists) -> ll::Expected<std::vector<std::pair<std::string, std::string>>> {
                            if (std::find(blacklists.begin(), blacklists.end(), player.getUuid().asString()) != blacklists.end())
                                return std::vector<std::pair<std::string, std::string>>{};

                            return owner.getStoreItems(storeId)
                                .and_then([&owner](const std::vector<std::string>& ids) -> ll::Expected<std::vector<std::pair<std::string, std::string>>> {
                                    return owner.getDatabase()->get("StoreItem", ids)
                                        .transform([](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> data) -> std::vector<std::pair<std::string, std::string>> {
                                            std::vector<std::pair<std::string, std::string>> items;
                                            items.reserve(data.size());

                                            for (const auto& [id, row] : data)
                                                items.emplace_back(row.at("name"), id);

                                            std::sort(items.begin(), items.end(), [](const auto& left, const auto& right) -> bool {
                                                return left.second < right.second;
                                            });

                                            return items;
                                        });
                                });
                        });
                });
        };

        auto listMyStoreItems = [&owner](Player& player) -> ll::Expected<std::vector<std::pair<std::string, std::string>>> {
            return owner.getStoreItems(player.getUuid().asString())
                .and_then([&owner](const std::vector<std::string>& ids) -> ll::Expected<std::vector<std::pair<std::string, std::string>>> {
                    return owner.getDatabase()->get("StoreItem", ids)
                        .transform([](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> data) -> std::vector<std::pair<std::string, std::string>> {
                            std::vector<std::pair<std::string, std::string>> items;
                            items.reserve(data.size());

                            for (const auto& [id, row] : data)
                                items.emplace_back(row.at("name"), id);

                            std::sort(items.begin(), items.end(), [](const auto& left, const auto& right) -> bool {
                                return left.second < right.second;
                            });

                            return items;
                        });
                });
        };

        auto getStoreApprovedRating = [&owner](const std::string& storeId) -> ll::Expected<double> {
            return owner.getDatabase()->find("StoreReview", {
                { "store_id", storeId },
                { "status", "approved" }
            }, SQLiteStorage::FindCondition::AND)
                .and_then([&owner](const std::vector<std::string>& keys) -> ll::Expected<double> {
                    if (keys.empty())
                        return 0.0;

                    return owner.getDatabase()->get("StoreReview", keys)
                        .transform([](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> data) -> double {
                            double sum = 0.0;
                            int count = 0;

                            for (const auto& [key, row] : data) {
                                sum += SystemUtils::toInt(row.at("rating"), 0);
                                count++;
                            }

                            return count > 0 ? sum / count : 0.0;
                        });
                });
        };

        form::GUIManager::getInstance().registerValue("market.store.list", [listVisibleStores](Player& player) -> ll::Expected<frontend::ArrayRef> {
            return listVisibleStores(player)
                .transform([](const std::vector<std::pair<std::string, std::string>>& items) -> frontend::ArrayRef {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    for (const auto& [name, id] : items)
                        values->elements.emplace_back(name);

                    return values;
                });
        });

        form::GUIManager::getInstance().registerValue("market.store.inventory", [&owner](Player& player) -> ll::Expected<frontend::ArrayRef> {
            return marketGui::listSellableInventory(owner, player)
                .transform([](const std::vector<std::pair<std::string, int>>& items) -> frontend::ArrayRef {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    for (const auto& [name, slot] : items)
                        values->elements.emplace_back(name);

                    return values;
                });
        });

        form::GUIManager::getInstance().registerValue("market.store.mine.items", [listMyStoreItems](Player& player) -> ll::Expected<frontend::ArrayRef> {
            return listMyStoreItems(player)
                .transform([](const std::vector<std::pair<std::string, std::string>>& items) -> frontend::ArrayRef {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    for (const auto& [name, id] : items)
                        values->elements.emplace_back(name);

                    return values;
                });
        });

        form::GUIManager::getInstance().registerRequest("market.store.enabled", [&owner](frontend::ArrayRef, Player&) -> ll::Expected<frontend::ArrayRef> {
            auto values = std::make_shared<frontend::ArrayValue>();
            values->elements.emplace_back(owner.getOptions().StoreEnabled);

            return values;
        });

        form::GUIManager::getInstance().registerRequest("market.store.review.enabled", [&owner](frontend::ArrayRef, Player&) -> ll::Expected<frontend::ArrayRef> {
            auto values = std::make_shared<frontend::ArrayValue>();
            values->elements.emplace_back(owner.getOptions().StoreReviewEnabled);

            return values;
        });

        form::GUIManager::getInstance().registerRequest("market.store.create.cost", [&owner](frontend::ArrayRef, Player& player) -> ll::Expected<frontend::ArrayRef> {
            auto values = std::make_shared<frontend::ArrayValue>();

            return LanguagePlugin::getShared()->getLanguage(player)
                .transform([&owner, values](const std::string& language) -> frontend::ArrayRef {
                    int cost = owner.getOptions().StoreCreationCost;

                    if (cost <= 0)
                        values->elements.emplace_back(tr(language, "market.gui.store.mine.create.free"));
                    else
                        values->elements.emplace_back(fmt::format(fmt::runtime(tr(language, "market.gui.store.mine.create.cost")), cost));

                    return values;
                });
        });

        form::GUIManager::getInstance().registerRequest("market.store.mine.has", [&owner](frontend::ArrayRef, Player& player) -> ll::Expected<frontend::ArrayRef> {
            auto values = std::make_shared<frontend::ArrayValue>();

            return owner.getDatabase()->has("Store", player.getUuid().asString())
                .transform([values](bool exists) -> frontend::ArrayRef {
                    values->elements.emplace_back(exists);

                    return values;
                });
        });

        form::GUIManager::getInstance().registerRequest("market.store.mine.info", [&owner](frontend::ArrayRef, Player& player) -> ll::Expected<frontend::ArrayRef> {
            auto values = std::make_shared<frontend::ArrayValue>();
            std::string uuid = player.getUuid().asString();

            return owner.getDatabase()->has("Store", uuid)
                .and_then([&owner, uuid, values](bool exists) -> ll::Expected<frontend::ArrayRef> {
                    values->elements.emplace_back(exists);

                    if (!exists) {
                        values->elements.emplace_back("");
                        values->elements.emplace_back("");
                        values->elements.emplace_back("");
                        values->elements.emplace_back("");

                        return values;
                    }

                    return owner.getStore(uuid)
                        .transform([values](std::unordered_map<std::string, std::string> data) -> frontend::ArrayRef {
                            values->elements.emplace_back(data.at("name"));
                            values->elements.emplace_back(data.at("icon"));
                            values->elements.emplace_back(data.at("introduce"));
                            values->elements.emplace_back(data.at("owner_uuid"));

                            return values;
                        });
                });
        });

        form::GUIManager::getInstance().registerRequest("market.store.list.id", [listVisibleStores](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                return ll::makeStringError("market.store.list.id: must take exactly one int parameter");

            int index = std::get<int>(args->elements[0]);

            return listVisibleStores(player)
                .and_then([index](const std::vector<std::pair<std::string, std::string>>& items) -> ll::Expected<frontend::ArrayRef> {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    if (index < 0 || index >= static_cast<int>(items.size()))
                        return values;

                    values->elements.emplace_back(items.at(static_cast<size_t>(index)).second);

                    return values;
                });
        });

        form::GUIManager::getInstance().registerRequest("market.store.info", [&owner, getStoreApprovedRating](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                return ll::makeStringError("market.store.info: must take exactly one string parameter");

            std::string storeId = std::get<std::string>(args->elements[0]);
            auto values = std::make_shared<frontend::ArrayValue>();

            auto storeResult = owner.getStore(storeId);
            if (!storeResult.has_value()) {
                auto language = LanguagePlugin::getShared()->getLanguage(player);

                if (language.has_value())
                    player.sendMessage(tr(language.value(), "market.gui.error"));

                values->elements.emplace_back("");

                return values;
            }

            auto ratingResult = getStoreApprovedRating(storeId);
            if (!ratingResult.has_value())
                return ll::Unexpected(ratingResult.error());

            auto languageResult = LanguagePlugin::getShared()->getLanguage(player);
            if (!languageResult.has_value())
                return ll::Unexpected(languageResult.error());

            values->elements.emplace_back(fmt::format(fmt::runtime(tr(languageResult.value(), "market.gui.store.detail.label")),
                storeResult.value().at("name"),
                storeResult.value().at("owner_name"),
                storeResult.value().at("introduce"),
                storeResult.value().at("store_created_at"),
                ratingResult.value() > 0.0 ? fmt::format("{:.1f}", ratingResult.value()) : std::string("-")
            ));

            return values;
        });

        form::GUIManager::getInstance().registerRequest("market.store.item.list", [listVisibleStoreItems](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                return ll::makeStringError("market.store.item.list: must take exactly one string parameter");

            std::string storeId = std::get<std::string>(args->elements[0]);

            return listVisibleStoreItems(player, storeId)
                .transform([](const std::vector<std::pair<std::string, std::string>>& items) -> frontend::ArrayRef {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    for (const auto& [name, id] : items)
                        values->elements.emplace_back(name);

                    return values;
                })
                .or_else([](ll::Error e) -> ll::Expected<frontend::ArrayRef> {
                    if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreNotFound))
                        return std::make_shared<frontend::ArrayValue>();

                    return ll::Unexpected(e);
                });
        });

        form::GUIManager::getInstance().registerRequest("market.store.item.id", [listVisibleStoreItems](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 2 ||
                !std::holds_alternative<std::string>(args->elements[0]) ||
                !std::holds_alternative<int>(args->elements[1]))
                return ll::makeStringError("market.store.item.id: must take one string and one int parameter");

            std::string storeId = std::get<std::string>(args->elements[0]);
            int index = std::get<int>(args->elements[1]);

            return listVisibleStoreItems(player, storeId)
                .and_then([index](const std::vector<std::pair<std::string, std::string>>& items) -> ll::Expected<frontend::ArrayRef> {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    if (index < 0 || index >= static_cast<int>(items.size()))
                        return values;

                    values->elements.emplace_back(items.at(static_cast<size_t>(index)).second);

                    return values;
                });
        });

        form::GUIManager::getInstance().registerRequest("market.store.item.info", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                return ll::makeStringError("market.store.item.info: must take exactly one string parameter");

            std::string itemId = std::get<std::string>(args->elements[0]);

            return owner.getStoreItemData(itemId)
                .and_then([&owner, &player](std::unordered_map<std::string, std::string> data) -> ll::Expected<frontend::ArrayRef> {
                    return owner.getStore(data.at("store_id"))
                        .and_then([&player, data](std::unordered_map<std::string, std::string> store) -> ll::Expected<frontend::ArrayRef> {
                            return LanguagePlugin::getShared()->getLanguage(player)
                                .transform([store, data](const std::string& language) -> frontend::ArrayRef {
                                    auto values = std::make_shared<frontend::ArrayValue>();
                                    values->elements.emplace_back(fmt::format(fmt::runtime(tr(language, "market.gui.store.item.info")),
                                        data.at("introduce"),
                                        data.at("score"),
                                        data.at("data"),
                                        store.at("name")
                                    ));

                                    return values;
                                });
                        });
                })
                .or_else([&player](ll::Error e) -> ll::Expected<frontend::ArrayRef> {
                    if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreItemNotFound)) {
                        auto values = std::make_shared<frontend::ArrayValue>();
                        auto language = LanguagePlugin::getShared()->getLanguage(player);

                        if (language.has_value())
                            player.sendMessage(tr(language.value(), "market.gui.error"));

                        values->elements.emplace_back("");

                        return values;
                    }

                    return ll::Unexpected(e);
                });
        });

        form::GUIManager::getInstance().registerRequest("market.store.reviews", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                return ll::makeStringError("market.store.reviews: must take exactly one string parameter");

            std::string storeId = std::get<std::string>(args->elements[0]);

            return owner.getReviews(storeId, MarketStoreReviewStatus::approved)
                .and_then([&owner, &player](const std::vector<std::string>& keys) -> ll::Expected<frontend::ArrayRef> {
                    return owner.getDatabase()->get("StoreReview", keys)
                        .and_then([&player, keys](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> data) -> ll::Expected<frontend::ArrayRef> {
                            return LanguagePlugin::getShared()->getLanguage(player)
                                .transform([keys, data](const std::string& language) -> frontend::ArrayRef {
                                    auto values = std::make_shared<frontend::ArrayValue>();

                                    for (const std::string& key : keys) {
                                        auto it = data.find(key);
                                        if (it == data.end())
                                            continue;

                                        const auto& row = it->second;
                                        values->elements.emplace_back(fmt::format(fmt::runtime(tr(language, "market.gui.store.review.line")),
                                            row.at("buyer_name"),
                                            row.at("rating"),
                                            row.at("content"),
                                            row.at("time")
                                        ));
                                    }

                                    return values;
                                });
                        });
                });
        });

        form::GUIManager::getInstance().registerRequest("market.store.review.can", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                return ll::makeStringError("market.store.review.can: must take exactly one string parameter");

            std::string storeId = std::get<std::string>(args->elements[0]);
            auto values = std::make_shared<frontend::ArrayValue>();

            if (!owner.getOptions().StoreReviewEnabled) {
                values->elements.emplace_back(false);

                return values;
            }

            return owner.getDatabase()->has("Store", storeId)
                .and_then([&owner, storeId, &player, values](bool exists) -> ll::Expected<frontend::ArrayRef> {
                    if (!exists) {
                        values->elements.emplace_back(false);

                        return values;
                    }

                    return owner.hasPurchasedInStore(player, storeId)
                        .and_then([&owner, storeId, &player, values](bool purchased) -> ll::Expected<frontend::ArrayRef> {
                            if (!purchased) {
                                values->elements.emplace_back(false);

                                return values;
                            }

                            return owner.getDatabase()->find("StoreReview", {
                                { "store_id", storeId },
                                { "buyer_uuid", player.getUuid().asString() }
                            }, SQLiteStorage::FindCondition::AND)
                                .transform([values](const std::vector<std::string>& reviews) -> frontend::ArrayRef {
                                    values->elements.emplace_back(reviews.empty());

                                    return values;
                                });
                        });
                });
        });

        form::GUIManager::getInstance().registerRequest("market.store.reviews.pending", [&owner](frontend::ArrayRef, Player& player) -> ll::Expected<frontend::ArrayRef> {
            auto values = std::make_shared<frontend::ArrayValue>();

            if (player.getCommandPermissionLevel() < CommandPermissionLevel::GameDirectors)
                return values;

            return owner.getDatabase()->find("StoreReview", {
                { "status", "pending" }
            }, SQLiteStorage::FindCondition::AND)
                .and_then([&owner, &player, values](const std::vector<std::string>& keys) -> ll::Expected<frontend::ArrayRef> {
                    return owner.getDatabase()->get("StoreReview", keys)
                        .and_then([&player, keys, values](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> data) -> ll::Expected<frontend::ArrayRef> {
                            return LanguagePlugin::getShared()->getLanguage(player)
                                .transform([keys, data, values](const std::string& language) -> frontend::ArrayRef {
                                    for (const std::string& key : keys) {
                                        auto it = data.find(key);
                                        if (it == data.end())
                                            continue;

                                        const auto& row = it->second;
                                        values->elements.emplace_back(fmt::format(fmt::runtime(tr(language, "market.gui.store.review.line")),
                                            row.at("buyer_name"),
                                            row.at("rating"),
                                            row.at("content"),
                                            row.at("time")
                                        ));
                                    }

                                    return values;
                                });
                        });
                });
        });

        form::GUIManager::getInstance().registerRequest("market.store.reviews.pending.id", [&owner](frontend::ArrayRef args, Player&) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                return ll::makeStringError("market.store.reviews.pending.id: must take exactly one int parameter");

            int index = std::get<int>(args->elements[0]);

            return owner.getDatabase()->find("StoreReview", {
                { "status", "pending" }
            }, SQLiteStorage::FindCondition::AND)
                .and_then([index](const std::vector<std::string>& keys) -> ll::Expected<frontend::ArrayRef> {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    if (index < 0 || index >= static_cast<int>(keys.size()))
                        return values;

                    values->elements.emplace_back(keys.at(static_cast<size_t>(index)));

                    return values;
                });
        });

        form::GUIManager::getInstance().registerRequest("market.store.review.audit.info", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                return ll::makeStringError("market.store.review.audit.info: must take exactly one string parameter");

            std::string reviewId = std::get<std::string>(args->elements[0]);

            return owner.getReviewData(reviewId)
                .and_then([&player](std::unordered_map<std::string, std::string> data) -> ll::Expected<frontend::ArrayRef> {
                    return LanguagePlugin::getShared()->getLanguage(player)
                        .transform([data](const std::string& language) -> frontend::ArrayRef {
                            auto values = std::make_shared<frontend::ArrayValue>();
                            values->elements.emplace_back(fmt::format(fmt::runtime(tr(language, "market.gui.store.audit.label")),
                                data.at("buyer_name"),
                                data.at("rating"),
                                data.at("content"),
                                data.at("time")
                            ));

                            return values;
                        });
                })
                .or_else([&player](ll::Error e) -> ll::Expected<frontend::ArrayRef> {
                    if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreReviewNotFound)) {
                        auto values = std::make_shared<frontend::ArrayValue>();
                        auto language = LanguagePlugin::getShared()->getLanguage(player);

                        if (language.has_value())
                            player.sendMessage(tr(language.value(), "market.gui.error"));

                        values->elements.emplace_back("");

                        return values;
                    }

                    return ll::Unexpected(e);
                });
        });

        form::GUIManager::getInstance().registerRequest("market.store.mine.item.id", [listMyStoreItems](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                return ll::makeStringError("market.store.mine.item.id: must take exactly one int parameter");

            int index = std::get<int>(args->elements[0]);

            return listMyStoreItems(player)
                .and_then([index](const std::vector<std::pair<std::string, std::string>>& items) -> ll::Expected<frontend::ArrayRef> {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    if (index < 0 || index >= static_cast<int>(items.size()))
                        return values;

                    values->elements.emplace_back(items.at(static_cast<size_t>(index)).second);

                    return values;
                });
        });

        form::GUIManager::getInstance().registerRequest("market.store.create.submit", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 3 ||
                !std::holds_alternative<std::string>(args->elements[0]) ||
                !std::holds_alternative<std::string>(args->elements[1]) ||
                !std::holds_alternative<std::string>(args->elements[2]))
                return ll::makeStringError("market.store.create.submit: must take exactly three string parameters");

            auto values = std::make_shared<frontend::ArrayValue>();
            std::string mName = std::get<std::string>(args->elements[0]);
            std::string mIcon = std::get<std::string>(args->elements[1]);
            std::string mIntroduce = std::get<std::string>(args->elements[2]);

            if (mName.empty() || mIcon.empty() || mIntroduce.empty()) {
                return LanguagePlugin::getShared()->getLanguage(player)
                    .and_then([&player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                        player.sendMessage(tr(language, "generic.tips.noinput"));

                        values->elements.emplace_back(false);
                        return values;
                    });
            }

            return owner.createStore(player, mName, mIcon, mIntroduce)
                .or_else([&player](ll::Error e) -> ll::Expected<bool> {
                    if (e.isA<ll::ErrorCodeError>()) {
                        auto ec = e.as<ll::ErrorCodeError>().ec;

                        if (ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreAlreadyExists) ||
                            ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreCostInsufficient)) {
                            auto language = LanguagePlugin::getShared()->getLanguage(player);

                            if (language.has_value()) {
                                player.sendMessage(tr(language.value(),
                                    ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreAlreadyExists)
                                        ? "market.gui.store.mine.create.exists"
                                        : "market.gui.store.mine.create.insufficient"
                                ));
                            }

                            return false;
                        }

                        return ll::Unexpected(e);
                    }

                    return ll::Unexpected(e);
                })
                .transform([&player, values](bool ok) -> frontend::ArrayRef {
                    if (!ok) {
                        auto language = LanguagePlugin::getShared()->getLanguage(player);

                        if (language.has_value())
                            player.sendMessage(tr(language.value(), "market.gui.error"));
                    }

                    values->elements.emplace_back(ok);

                    return values;
                });
        });

        form::GUIManager::getInstance().registerRequest("market.store.dissolve.submit", [&owner](frontend::ArrayRef, Player& player) -> ll::Expected<frontend::ArrayRef> {
            auto values = std::make_shared<frontend::ArrayValue>();

            return owner.dissolveStore(player)
                .or_else([](ll::Error e) -> ll::Expected<bool> {
                    if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreNotFound))
                        return false;

                    return ll::Unexpected(e);
                })
                .transform([&player, values](bool ok) -> frontend::ArrayRef {
                    if (!ok) {
                        auto language = LanguagePlugin::getShared()->getLanguage(player);

                        if (language.has_value())
                            player.sendMessage(tr(language.value(), "market.gui.error"));
                    }

                    values->elements.emplace_back(ok);

                    return values;
                });
        });

        form::GUIManager::getInstance().registerRequest("market.store.upload.check", [&owner](frontend::ArrayRef, Player& player) -> ll::Expected<frontend::ArrayRef> {
            auto values = std::make_shared<frontend::ArrayValue>();
            std::string storeId = player.getUuid().asString();

            return owner.getDatabase()->has("Store", storeId)
                .and_then([&owner, storeId, &player, values](bool exists) -> ll::Expected<frontend::ArrayRef> {
                    if (!exists) {
                        values->elements.emplace_back(false);

                        return values;
                    }

                    return owner.getDatabase()->find("StoreItem", {
                        { "store_id", storeId }
                    }, SQLiteStorage::FindCondition::AND)
                        .and_then([&owner, &player, values](const std::vector<std::string>& items) -> ll::Expected<frontend::ArrayRef> {
                            if (static_cast<int>(items.size()) >= owner.getOptions().StoreMaximumItems) {
                                return LanguagePlugin::getShared()->getLanguage(player)
                                    .and_then([&owner, &player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                                        player.sendMessage(fmt::format(fmt::runtime(tr(language, "market.gui.store.mine.upload.full")), owner.getOptions().StoreMaximumItems));

                                        values->elements.emplace_back(false);
                                        return values;
                                    });
                            }

                            values->elements.emplace_back(true);
                            return values;
                        });
                });
        });

        form::GUIManager::getInstance().registerRequest("market.store.sell.slot", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                return ll::makeStringError("market.store.sell.slot: must take exactly one int parameter");

            int index = std::get<int>(args->elements[0]);

            return marketGui::listSellableInventory(owner, player)
                .and_then([index](const std::vector<std::pair<std::string, int>>& items) -> ll::Expected<frontend::ArrayRef> {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    if (index < 0 || index >= static_cast<int>(items.size()))
                        return values;

                    values->elements.emplace_back(items.at(static_cast<size_t>(index)).second);

                    return values;
                });
        });

        form::GUIManager::getInstance().registerRequest("market.store.sell.submit", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 5 ||
                !std::holds_alternative<int>(args->elements[0]) ||
                !std::holds_alternative<std::string>(args->elements[1]) ||
                !std::holds_alternative<std::string>(args->elements[2]) ||
                !std::holds_alternative<std::string>(args->elements[3]) ||
                !std::holds_alternative<std::string>(args->elements[4]))
                return ll::makeStringError("market.store.sell.submit: must take one int and four string parameters");

            auto values = std::make_shared<frontend::ArrayValue>();
            std::string mItemName = std::get<std::string>(args->elements[1]);
            std::string mItemIcon = std::get<std::string>(args->elements[2]);
            std::string mItemIntroduce = std::get<std::string>(args->elements[3]);

            if (mItemName.empty() || mItemIcon.empty() || mItemIntroduce.empty()) {
                return LanguagePlugin::getShared()->getLanguage(player)
                    .and_then([&player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                        player.sendMessage(tr(language, "generic.tips.noinput"));

                        values->elements.emplace_back(false);
                        return values;
                    });
            }

            return owner.uploadStoreItem(player,
                    std::get<int>(args->elements[0]),
                    mItemName,
                    mItemIcon,
                    mItemIntroduce,
                    SystemUtils::toInt(std::get<std::string>(args->elements[4]), 0))
                .or_else([](ll::Error e) -> ll::Expected<bool> {
                    if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreNotFound))
                        return false;

                    return ll::Unexpected(e);
                })
                .transform([&player, values](bool ok) -> frontend::ArrayRef {
                    if (!ok) {
                        auto language = LanguagePlugin::getShared()->getLanguage(player);

                        if (language.has_value())
                            player.sendMessage(tr(language.value(), "market.gui.error"));
                    }

                    values->elements.emplace_back(ok);

                    return values;
                });
        });

        form::GUIManager::getInstance().registerRequest("market.store.offshelf.submit", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                return ll::makeStringError("market.store.offshelf.submit: must take exactly one string parameter");

            auto values = std::make_shared<frontend::ArrayValue>();

            return owner.offshelfStoreItem(player, std::get<std::string>(args->elements[0]), true)
                .or_else([](ll::Error e) -> ll::Expected<bool> {
                    if (e.isA<ll::ErrorCodeError>() &&
                        (e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreItemNotFound) ||
                            e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreNotFound)))
                        return false;

                    return ll::Unexpected(e);
                })
                .transform([&player, values](bool ok) -> frontend::ArrayRef {
                    if (!ok) {
                        auto language = LanguagePlugin::getShared()->getLanguage(player);

                        if (language.has_value())
                            player.sendMessage(tr(language.value(), "market.gui.error"));
                    }

                    values->elements.emplace_back(ok);

                    return values;
                });
        });

        form::GUIManager::getInstance().registerRequest("market.store.buy.submit", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                return ll::makeStringError("market.store.buy.submit: must take exactly one string parameter");

            auto values = std::make_shared<frontend::ArrayValue>();

            return owner.buyStoreItem(player, std::get<std::string>(args->elements[0]))
                .or_else([](ll::Error e) -> ll::Expected<bool> {
                    if (e.isA<ll::ErrorCodeError>() &&
                        (e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreItemNotFound) ||
                            e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreNotFound)))
                        return false;

                    return ll::Unexpected(e);
                })
                .transform([values](bool ok) -> frontend::ArrayRef {
                    values->elements.emplace_back(ok);

                    return values;
                });
        });

        form::GUIManager::getInstance().registerRequest("market.store.review.submit", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 3 ||
                !std::holds_alternative<std::string>(args->elements[0]) ||
                !std::holds_alternative<std::string>(args->elements[2]) ||
                (!std::holds_alternative<int>(args->elements[1]) && !std::holds_alternative<float>(args->elements[1])))
                return ll::makeStringError("market.store.review.submit: must take one string, one number and one string parameter");

            auto values = std::make_shared<frontend::ArrayValue>();
            std::string storeId = std::get<std::string>(args->elements[0]);
            double ratingIndex = std::holds_alternative<int>(args->elements[1])
                ? static_cast<double>(std::get<int>(args->elements[1]))
                : static_cast<double>(std::get<float>(args->elements[1]));
            std::string content = std::get<std::string>(args->elements[2]);

            return owner.addReview(player, storeId, static_cast<int>(ratingIndex) + 1, content)
                .or_else([](ll::Error e) -> ll::Expected<bool> {
                    if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreNotFound))
                        return false;

                    return ll::Unexpected(e);
                })
                .transform([&player, values](bool ok) -> frontend::ArrayRef {
                    if (ok) {
                        auto language = LanguagePlugin::getShared()->getLanguage(player);

                        if (language.has_value())
                            player.sendMessage(tr(language.value(), "market.gui.store.review.waiting"));
                    }

                    values->elements.emplace_back(ok);

                    return values;
                });
        });

        form::GUIManager::getInstance().registerRequest("market.store.review.audit", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 2 ||
                !std::holds_alternative<std::string>(args->elements[0]) ||
                !std::holds_alternative<bool>(args->elements[1]))
                return ll::makeStringError("market.store.review.audit: must take one string and one bool parameter");

            auto values = std::make_shared<frontend::ArrayValue>();

            return owner.auditReview(player, std::get<std::string>(args->elements[0]), std::get<bool>(args->elements[1]))
                .or_else([](ll::Error e) -> ll::Expected<bool> {
                    if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::StoreReviewNotFound))
                        return false;

                    return ll::Unexpected(e);
                })
                .transform([&player, values](bool ok) -> frontend::ArrayRef {
                    if (!ok) {
                        auto language = LanguagePlugin::getShared()->getLanguage(player);

                        if (language.has_value())
                            player.sendMessage(tr(language.value(), "market.gui.error"));
                    }

                    values->elements.emplace_back(ok);

                    return values;
                });
        });
    }
}
