#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <unordered_map>

#include <fmt/core.h>

#include <ll/api/Expected.h>
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

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/data/SQLiteStorage.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/frontend/AST.h"

#include "LOICollectionA/include/CallbackUtils.h"

#include "LOICollectionA/include/form/GUIManager.h"

#include "LOICollectionA/include/server/Plugins/market/MarketGui.h"
#include "LOICollectionA/include/server/Plugins/market/MarketPlugin.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {

    ll::Expected<void> MarketGui::registerAll(MarketPlugin& owner) {
            auto listPlayers = [](Player& player) -> std::vector<std::pair<std::string, std::string>> {
                std::vector<std::pair<std::string, std::string>> players;

                ll::service::getLevel()->forEachPlayer([&player, &players](Player& target) -> bool {
                    if (target.isSimulatedPlayer() || target.getUuid() == player.getUuid())
                        return true;

                    players.emplace_back(target.getRealName(), target.getUuid().asString());
                    return true;
                });

                return players;
            };

            auto listSellableInventory = [&owner](Player& player) -> ll::Expected<std::vector<std::pair<std::string, int>>> {
                return LanguagePlugin::getShared()->getLanguage(player)
                    .transform([&owner, &player](const std::string& language) -> std::vector<std::pair<std::string, int>> {
                        std::vector<std::pair<std::string, int>> items;
                        std::vector<std::string> prohibitedItems = owner.getProhibitedItems();

                        for (int i = 0; i < player.mInventory->mInventory->getContainerSize(); i++) {
                            ItemStack mItemStack = player.mInventory->mInventory->getItem(i);

                            if (!mItemStack || mItemStack.isNull() ||
                                std::find(prohibitedItems.begin(), prohibitedItems.end(), mItemStack.getTypeName()) != prohibitedItems.end())
                                continue;

                            items.emplace_back(fmt::format(fmt::runtime(tr(language, "market.gui.sell.item.text")),
                                mItemStack.getName(), std::to_string(mItemStack.mCount)
                            ), i);
                        }

                        return items;
                    });
            };

            auto listBuyItems = [&owner](Player& player) -> ll::Expected<std::vector<std::pair<std::string, std::string>>> {
                return owner.getItems()
                    .and_then([&owner, &player](const std::vector<std::string>& ids) -> ll::Expected<std::vector<std::pair<std::string, std::string>>> {
                        return owner.getItemsData(ids)
                            .and_then([&owner, &player](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> data) -> ll::Expected<std::vector<std::pair<std::string, std::string>>> {
                                std::vector<std::pair<std::string, std::string>> items;
                                std::string mUuid = player.getUuid().asString();

                                for (auto& item : data) {
                                    auto blacklists = owner.getBlacklist(item.second.at("player_uuid"));
                                    if (!blacklists.has_value())
                                        return ll::Unexpected(blacklists.error());

                                    if (std::find(blacklists.value().begin(), blacklists.value().end(), mUuid) != blacklists.value().end())
                                        continue;

                                    items.emplace_back(item.second.at("name"), item.first);
                                }

                                return items;
                            });
                    });
            };

            auto listMyItems = [&owner](Player& player) -> ll::Expected<std::vector<std::pair<std::string, std::string>>> {
                return owner.getItems(player)
                    .and_then([&owner](const std::vector<std::string>& ids) -> ll::Expected<std::vector<std::pair<std::string, std::string>>> {
                        return owner.getItemsData(ids)
                            .transform([](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> data) -> std::vector<std::pair<std::string, std::string>> {
                                std::vector<std::pair<std::string, std::string>> items;

                                items.reserve(data.size());
                                for (auto& item : data)
                                    items.emplace_back(item.second.at("name"), item.first);

                                return items;
                            });
                    });
            };

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

            form::GUIManager::getInstance().registerValue("market.players", [listPlayers](Player& player) -> frontend::ArrayRef {
                auto values = std::make_shared<frontend::ArrayValue>();

                for (const auto& [name, uuid] : listPlayers(player))
                    values->elements.emplace_back(name);

                return values;
            });

            form::GUIManager::getInstance().registerValue("market.buy.items", [listBuyItems](Player& player) -> ll::Expected<frontend::ArrayRef> {
                return listBuyItems(player)
                    .transform([](const std::vector<std::pair<std::string, std::string>>& items) -> frontend::ArrayRef {
                        auto values = std::make_shared<frontend::ArrayValue>();

                        for (const auto& [name, id] : items)
                            values->elements.emplace_back(name);

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerValue("market.my.items", [listMyItems](Player& player) -> ll::Expected<frontend::ArrayRef> {
                return listMyItems(player)
                    .transform([](const std::vector<std::pair<std::string, std::string>>& items) -> frontend::ArrayRef {
                        auto values = std::make_shared<frontend::ArrayValue>();

                        for (const auto& [name, id] : items)
                            values->elements.emplace_back(name);

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerValue("market.sell.inventory", [listSellableInventory](Player& player) -> ll::Expected<frontend::ArrayRef> {
                return listSellableInventory(player)
                    .transform([](const std::vector<std::pair<std::string, int>>& items) -> frontend::ArrayRef {
                        auto values = std::make_shared<frontend::ArrayValue>();

                        for (const auto& [name, slot] : items)
                            values->elements.emplace_back(name);

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerValue("market.trade.items", [listSellableInventory](Player& player) -> ll::Expected<frontend::ArrayRef> {
                return listSellableInventory(player)
                    .transform([](const std::vector<std::pair<std::string, int>>& items) -> frontend::ArrayRef {
                        auto values = std::make_shared<frontend::ArrayValue>();

                        for (const auto& [name, slot] : items)
                            values->elements.emplace_back(name);

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerValue("market.blacklists", [&owner](Player& player) -> ll::Expected<frontend::ArrayRef> {
                return owner.getBlacklist(player)
                    .transform([](const std::vector<std::string>& blacklists) -> frontend::ArrayRef {
                        auto values = std::make_shared<frontend::ArrayValue>();

                        for (const std::string& id : blacklists)
                            values->elements.emplace_back(id);

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.self.uuid", [](frontend::ArrayRef, Player& player) -> ll::Expected<frontend::ArrayRef> {
                auto values = std::make_shared<frontend::ArrayValue>();
                values->elements.emplace_back(player.getUuid().asString());

                return values;
            });

            form::GUIManager::getInstance().registerRequest("market.isAdmin", [](frontend::ArrayRef, Player& player) -> ll::Expected<frontend::ArrayRef> {
                auto values = std::make_shared<frontend::ArrayValue>();
                values->elements.emplace_back(player.getCommandPermissionLevel() >= CommandPermissionLevel::GameDirectors);

                return values;
            });

            form::GUIManager::getInstance().registerRequest("market.item.info", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                    return ll::makeStringError("market.item.info: must take exactly one string parameter");

                auto id = std::get<std::string>(args->elements[0]);

                return LanguagePlugin::getShared()->getLanguage(player)
                    .and_then([&owner, id, &player](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                        return owner.hasItem(id)
                            .and_then([&owner, language, id, &player](bool exists) -> ll::Expected<frontend::ArrayRef> {
                                auto values = std::make_shared<frontend::ArrayValue>();

                                if (!exists) {
                                    player.sendMessage(tr(language, "market.gui.error"));

                                    return values;
                                }

                                auto data = owner.getItemData(id);
                                if (!data.has_value())
                                    return ll::Unexpected(data.error());

                                values->elements.emplace_back(fmt::format(fmt::runtime(tr(language, "market.gui.item.introduce")),
                                    data.value().at("introduce"),
                                    data.value().at("score"),
                                    data.value().at("data"),
                                    data.value().at("player_name")
                                ));

                                return values;
                            });
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.buy.id", [listBuyItems](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                    return ll::makeStringError("market.buy.id: must take exactly one int parameter");

                int index = std::get<int>(args->elements[0]);

                return listBuyItems(player)
                    .and_then([index](const std::vector<std::pair<std::string, std::string>>& items) -> ll::Expected<frontend::ArrayRef> {
                        auto values = std::make_shared<frontend::ArrayValue>();

                        if (index < 0 || index >= static_cast<int>(items.size()))
                            return values;

                        values->elements.emplace_back(items.at(static_cast<size_t>(index)).second);

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.my.item.id", [listMyItems](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                    return ll::makeStringError("market.my.item.id: must take exactly one int parameter");

                int index = std::get<int>(args->elements[0]);

                return listMyItems(player)
                    .and_then([index](const std::vector<std::pair<std::string, std::string>>& items) -> ll::Expected<frontend::ArrayRef> {
                        auto values = std::make_shared<frontend::ArrayValue>();

                        if (index < 0 || index >= static_cast<int>(items.size()))
                            return values;

                        values->elements.emplace_back(items.at(static_cast<size_t>(index)).second);

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.sell.slot", [listSellableInventory](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                    return ll::makeStringError("market.sell.slot: must take exactly one int parameter");

                int index = std::get<int>(args->elements[0]);

                return listSellableInventory(player)
                    .and_then([index](const std::vector<std::pair<std::string, int>>& items) -> ll::Expected<frontend::ArrayRef> {
                        auto values = std::make_shared<frontend::ArrayValue>();

                        if (index < 0 || index >= static_cast<int>(items.size()))
                            return values;

                        values->elements.emplace_back(items.at(static_cast<size_t>(index)).second);

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.players.uuid", [listPlayers](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                    return ll::makeStringError("market.players.uuid: must take exactly one int parameter");

                int index = std::get<int>(args->elements[0]);
                auto players = listPlayers(player);
                auto values = std::make_shared<frontend::ArrayValue>();

                if (index < 0 || index >= static_cast<int>(players.size()))
                    return values;

                values->elements.emplace_back(players.at(static_cast<size_t>(index)).second);

                return values;
            });

            form::GUIManager::getInstance().registerRequest("market.sell.content.check", [&owner](frontend::ArrayRef, Player& player) -> ll::Expected<frontend::ArrayRef> {
                return owner.getItems(player)
                    .and_then([&owner, &player](const std::vector<std::string>& items) -> ll::Expected<frontend::ArrayRef> {
                        auto values = std::make_shared<frontend::ArrayValue>();

                        if (static_cast<int>(items.size()) >= owner.getMaximumUpload()) {
                            return LanguagePlugin::getShared()->getLanguage(player)
                                .and_then([&owner, &player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                                    player.sendMessage(fmt::format(fmt::runtime(tr(language, "market.gui.sell.sellItem.tips4")), owner.getMaximumUpload()));

                                    values->elements.emplace_back(false);
                                    return values;
                                });
                        }

                        values->elements.emplace_back(true);
                        return values;
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.blacklist.check", [&owner](frontend::ArrayRef, Player& player) -> ll::Expected<frontend::ArrayRef> {
                return owner.getBlacklist(player)
                    .and_then([&owner, &player](const std::vector<std::string>& ids) -> ll::Expected<frontend::ArrayRef> {
                        auto values = std::make_shared<frontend::ArrayValue>();

                        if (static_cast<int>(ids.size()) >= owner.getBlacklistUpload()) {
                            return LanguagePlugin::getShared()->getLanguage(player)
                                .and_then([&owner, &player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                                    player.sendMessage(fmt::format(fmt::runtime(tr(language, "market.gui.sell.sellItem.tips5")), owner.getBlacklistUpload()));

                                    values->elements.emplace_back(false);
                                    return values;
                                });
                        }

                        values->elements.emplace_back(true);
                        return values;
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.blacklist.info", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                    return ll::makeStringError("market.blacklist.info: must take exactly one string parameter");

                auto id = std::get<std::string>(args->elements[0]);

                return LanguagePlugin::getShared()->getLanguage(player)
                    .and_then([&owner, id, &player](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                        return owner.hasBlacklist(player, id)
                            .and_then([&owner, language, id, &player](bool exists) -> ll::Expected<frontend::ArrayRef> {
                                auto values = std::make_shared<frontend::ArrayValue>();

                                if (!exists) {
                                    player.sendMessage(tr(language, "market.gui.error"));

                                    return values;
                                }

                                auto data = owner.getBlacklistData(id);
                                if (!data.has_value())
                                    return ll::Unexpected(data.error());

                                values->elements.emplace_back(fmt::format(fmt::runtime(tr(language, "market.gui.sell.blacklist.set.label")),
                                    data.value().at("target"),
                                    data.value().at("name"),
                                    SystemUtils::toFormatTime(data.value().at("time"), "None")
                                ));

                                return values;
                            });
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.blacklist.add", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                    return ll::makeStringError("market.blacklist.add: must take exactly one string parameter");

                auto values = std::make_shared<frontend::ArrayValue>();

                Player* target = ll::service::getLevel()->getPlayer(mce::UUID::fromString(std::get<std::string>(args->elements[0])));
                if (!target) {
                    return LanguagePlugin::getShared()->getLanguage(player)
                        .and_then([&player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                            player.sendMessage(tr(language, "market.gui.error"));

                            values->elements.emplace_back(false);
                            return values;
                        });
                }

                return owner.addBlacklist(player, *target)
                    .transform([values]() -> frontend::ArrayRef {
                        values->elements.emplace_back(true);
                        return values;
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.sell.submit", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 5 ||
                    !std::holds_alternative<int>(args->elements[0]) ||
                    !std::holds_alternative<std::string>(args->elements[1]) ||
                    !std::holds_alternative<std::string>(args->elements[2]) ||
                    !std::holds_alternative<std::string>(args->elements[3]) ||
                    !std::holds_alternative<std::string>(args->elements[4]))
                    return ll::makeStringError("market.sell.submit: must take one int and four string parameters");

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

                if (!owner.sellItem(player, std::get<int>(args->elements[0]), mItemName, mItemIcon, mItemIntroduce,
                    SystemUtils::toInt(std::get<std::string>(args->elements[4]), 0))) {
                    return LanguagePlugin::getShared()->getLanguage(player)
                        .and_then([&player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                            player.sendMessage(tr(language, "market.gui.error"));

                            values->elements.emplace_back(false);
                            return values;
                        });
                }

                values->elements.emplace_back(true);
                return values;
            });

            form::GUIManager::getInstance().registerRequest("market.buy.submit", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                    return ll::makeStringError("market.buy.submit: must take exactly one string parameter");

                auto values = std::make_shared<frontend::ArrayValue>();

                return owner.buyItem(player, std::get<std::string>(args->elements[0]))
                    .or_else([](ll::Error e) -> ll::Expected<bool> {
                        if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::ItemNotFound))
                            return false;

                        return ll::Unexpected(e);
                    })
                    .transform([values](bool ok) -> frontend::ArrayRef {
                        values->elements.emplace_back(ok);

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.item.offshelf", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                    return ll::makeStringError("market.item.offshelf: must take exactly one string parameter");

                auto values = std::make_shared<frontend::ArrayValue>();

                return owner.offshelfItem(player, std::get<std::string>(args->elements[0]), false)
                    .or_else([](ll::Error e) -> ll::Expected<bool> {
                        if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::ItemNotFound))
                            return false;

                        return ll::Unexpected(e);
                    })
                    .transform([values](bool ok) -> frontend::ArrayRef {
                        values->elements.emplace_back(ok);

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.item.offshelf.return", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                    return ll::makeStringError("market.item.offshelf.return: must take exactly one string parameter");

                auto values = std::make_shared<frontend::ArrayValue>();

                return owner.offshelfItem(player, std::get<std::string>(args->elements[0]), true)
                    .or_else([](ll::Error e) -> ll::Expected<bool> {
                        if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::ItemNotFound))
                            return false;

                        return ll::Unexpected(e);
                    })
                    .transform([values](bool ok) -> frontend::ArrayRef {
                        values->elements.emplace_back(ok);

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.trade.request.info", [](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 2 ||
                    !std::holds_alternative<std::string>(args->elements[0]) ||
                    !std::holds_alternative<std::string>(args->elements[1]))
                    return ll::makeStringError("market.trade.request.info: must take exactly two string parameters");

                auto values = std::make_shared<frontend::ArrayValue>();

                Player* requester = ll::service::getLevel()->getPlayer(mce::UUID::fromString(std::get<std::string>(args->elements[0])));
                if (!requester)
                    return values;

                std::string type = std::get<std::string>(args->elements[1]);

                return LanguagePlugin::getShared()->getLanguage(player)
                    .transform([&requester, type, values](const std::string& language) -> frontend::ArrayRef {
                        values->elements.emplace_back(LOICollectionAPI::CallbackUtils::getInstance().translate(
                            tr(language, type == "sell" ? "market.sell" : "market.buy"), *requester
                        ));

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.trade.request.accept", [&owner](frontend::ArrayRef, Player& player) -> ll::Expected<frontend::ArrayRef> {
                auto values = std::make_shared<frontend::ArrayValue>();

                return owner.acceptRequest(player)
                    .or_else([](ll::Error e) -> ll::Expected<bool> {
                        if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::RequestNotFound))
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

            form::GUIManager::getInstance().registerRequest("market.trade.item.info", [](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 2 ||
                    !std::holds_alternative<std::string>(args->elements[0]) ||
                    !std::holds_alternative<int>(args->elements[1]))
                    return ll::makeStringError("market.trade.item.info: must take exactly one string and one int parameter");

                auto values = std::make_shared<frontend::ArrayValue>();

                Player* seller = ll::service::getLevel()->getPlayer(mce::UUID::fromString(std::get<std::string>(args->elements[0])));
                if (!seller)
                    return values;

                int slot = std::get<int>(args->elements[1]);
                if (slot < 0 || slot >= seller->mInventory->mInventory->getContainerSize())
                    return values;

                ItemStack mItemStack = seller->mInventory->mInventory->getItem(slot);
                if (!mItemStack || mItemStack.isNull())
                    return values;

                return LanguagePlugin::getShared()->getLanguage(player)
                    .transform([&seller, &mItemStack, values](const std::string& language) -> frontend::ArrayRef {
                        values->elements.emplace_back(fmt::format(fmt::runtime(tr(language, "market.gui.trade.introduce")),
                            seller->getRealName(),
                            mItemStack.getName(),
                            mItemStack.mCount,
                            mItemStack.save(*SaveContextFactory::createCloneSaveContext())->toSnbt(SnbtFormat::Minimize, 0)
                        ));

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.trade.confirm.info", [](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 3 ||
                    !std::holds_alternative<std::string>(args->elements[0]) ||
                    !std::holds_alternative<int>(args->elements[1]) ||
                    !std::holds_alternative<int>(args->elements[2]))
                    return ll::makeStringError("market.trade.confirm.info: must take one string and two int parameters");

                auto values = std::make_shared<frontend::ArrayValue>();

                Player* buyer = ll::service::getLevel()->getPlayer(mce::UUID::fromString(std::get<std::string>(args->elements[0])));
                if (!buyer)
                    return values;

                int slot = std::get<int>(args->elements[1]);
                int score = std::get<int>(args->elements[2]);

                if (slot < 0 || slot >= player.mInventory->mInventory->getContainerSize())
                    return values;

                ItemStack mItemStack = player.mInventory->mInventory->getItem(slot);
                if (!mItemStack || mItemStack.isNull())
                    return values;

                return LanguagePlugin::getShared()->getLanguage(player)
                    .transform([&buyer, &mItemStack, score, values](const std::string& language) -> frontend::ArrayRef {
                        values->elements.emplace_back(fmt::format(fmt::runtime(tr(language, "market.gui.trade.confirm.introduce")),
                            buyer->getRealName(),
                            mItemStack.getName(),
                            mItemStack.mCount,
                            mItemStack.save(*SaveContextFactory::createCloneSaveContext())->toSnbt(SnbtFormat::Minimize, 0),
                            score
                        ));

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.trade.slot", [listSellableInventory](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                    return ll::makeStringError("market.trade.slot: must take exactly one int parameter");

                int index = std::get<int>(args->elements[0]);

                return listSellableInventory(player)
                    .and_then([index](const std::vector<std::pair<std::string, int>>& items) -> ll::Expected<frontend::ArrayRef> {
                        auto values = std::make_shared<frontend::ArrayValue>();

                        if (index < 0 || index >= static_cast<int>(items.size()))
                            return values;

                        values->elements.emplace_back(items.at(static_cast<size_t>(index)).second);

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerCallback("market.blacklist.remove", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                    return ll::makeStringError("market.blacklist.remove: must take exactly one string parameter");

                return owner.delBlacklist(player, std::get<std::string>(args->elements[0]))
                    .or_else([](ll::Error e) -> ll::Expected<void> {
                        if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::BlacklistNotFound))
                            return {};

                        return ll::Unexpected(e);
                    });
            });

            form::GUIManager::getInstance().registerCallback("market.trade.request", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                if (args->elements.size() != 2 ||
                    !std::holds_alternative<std::string>(args->elements[0]) ||
                    !std::holds_alternative<std::string>(args->elements[1]))
                    return ll::makeStringError("market.trade.request: must take exactly two string parameters");

                auto targetUuid = std::get<std::string>(args->elements[0]);
                std::string type = std::get<std::string>(args->elements[1]);

                Player* target = ll::service::getLevel()->getPlayer(mce::UUID::fromString(targetUuid));
                if (!target) {
                    return LanguagePlugin::getShared()->getLanguage(player)
                        .and_then([&player](const std::string& language) -> ll::Expected<void> {
                            player.sendMessage(tr(language, "market.gui.error"));

                            return {};
                        });
                }

                return owner.hasTrade(player)
                    .and_then([&owner, &player, target, type](bool exists) -> ll::Expected<void> {
                        if (exists) {
                            return LanguagePlugin::getShared()->getLanguage(player)
                                .and_then([&player](const std::string& language) -> ll::Expected<void> {
                                    player.sendMessage(tr(language, "market.tips3"));

                                    return {};
                                });
                        }

                        return owner.sendRequest(player, *target, type == "sell" ? MarketTradeType::sell : MarketTradeType::buy)
                            .and_then([&player, target, type]() -> ll::Expected<void> {
                                auto ctx = std::make_shared<frontend::ArrayValue>();
                                ctx->elements.emplace_back(player.getUuid().asString());
                                ctx->elements.emplace_back("");
                                ctx->elements.emplace_back(-1);
                                ctx->elements.emplace_back(-1);
                                ctx->elements.emplace_back(type);

                                return form::GUIManager::getInstance().open("market.trade", "market.trade.request", form::GUIManagerType::CustomForm, *target, ctx);
                            });
                    });
            });

            form::GUIManager::getInstance().registerCallback("market.trade.request.reject", [&owner](frontend::ArrayRef, Player& player) -> ll::Expected<void> {
                return owner.rejectRequest(player)
                    .or_else([](ll::Error e) -> ll::Expected<bool> {
                        if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::RequestNotFound))
                            return false;

                        return ll::Unexpected(e);
                    })
                    .transform([](bool) -> void {});
            });

            form::GUIManager::getInstance().registerCallback("market.trade.open.content", [](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                if (args->elements.size() != 2 ||
                    !std::holds_alternative<std::string>(args->elements[0]) ||
                    !std::holds_alternative<std::string>(args->elements[1]))
                    return ll::makeStringError("market.trade.open.content: must take exactly two string parameters");

                auto destUuid = std::get<std::string>(args->elements[0]);
                auto buyerUuid = std::get<std::string>(args->elements[1]);

                Player* dest = ll::service::getLevel()->getPlayer(mce::UUID::fromString(destUuid));
                if (!dest) {
                    return LanguagePlugin::getShared()->getLanguage(player)
                        .and_then([&player](const std::string& language) -> ll::Expected<void> {
                            player.sendMessage(tr(language, "market.gui.error"));

                            return {};
                        });
                }

                auto ctx = std::make_shared<frontend::ArrayValue>();
                ctx->elements.emplace_back(buyerUuid);
                ctx->elements.emplace_back("");
                ctx->elements.emplace_back(-1);
                ctx->elements.emplace_back(-1);
                ctx->elements.emplace_back("");

                return form::GUIManager::getInstance().open("market.trade", "market.trade.content", form::GUIManagerType::PaginatedForm, *dest, ctx);
            });

            form::GUIManager::getInstance().registerCallback("market.trade.item", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                if (args->elements.size() != 2 ||
                    !std::holds_alternative<std::string>(args->elements[0]) ||
                    !std::holds_alternative<int>(args->elements[1]))
                    return ll::makeStringError("market.trade.item: must take exactly one string and one int parameter");

                auto buyerUuid = std::get<std::string>(args->elements[0]);
                int slot = std::get<int>(args->elements[1]);

                if (slot < 0 || slot >= player.mInventory->mInventory->getContainerSize()) {
                    return owner.cancelTrade(player)
                        .or_else([](ll::Error e) -> ll::Expected<bool> {
                            if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::TradeNotFound))
                                return false;

                            return ll::Unexpected(e);
                        })
                        .transform([](bool) -> void {});
                }

                ItemStack mItemStack = player.mInventory->mInventory->getItem(slot);
                if (!mItemStack || mItemStack.isNull()) {
                    return owner.cancelTrade(player)
                        .or_else([](ll::Error e) -> ll::Expected<bool> {
                            if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::TradeNotFound))
                                return false;

                            return ll::Unexpected(e);
                        })
                        .transform([](bool) -> void {});
                }

                Player* buyer = ll::service::getLevel()->getPlayer(mce::UUID::fromString(buyerUuid));
                if (!buyer) {
                    return LanguagePlugin::getShared()->getLanguage(player)
                        .and_then([&player](const std::string& language) -> ll::Expected<void> {
                            player.sendMessage(tr(language, "market.gui.error"));

                            return {};
                        });
                }

                auto ctx = std::make_shared<frontend::ArrayValue>();
                ctx->elements.emplace_back(buyerUuid);
                ctx->elements.emplace_back(player.getUuid().asString());
                ctx->elements.emplace_back(slot);
                ctx->elements.emplace_back(-1);
                ctx->elements.emplace_back("");

                return form::GUIManager::getInstance().open("market.trade", "market.trade.item", form::GUIManagerType::CustomForm, *buyer, ctx);
            });

            form::GUIManager::getInstance().registerCallback("market.trade.item.submit", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                if (args->elements.size() != 3 ||
                    !std::holds_alternative<std::string>(args->elements[0]) ||
                    !std::holds_alternative<int>(args->elements[1]) ||
                    !std::holds_alternative<std::string>(args->elements[2]))
                    return ll::makeStringError("market.trade.item.submit: must take two string and one int parameters");

                auto sellerUuid = std::get<std::string>(args->elements[0]);
                int slot = std::get<int>(args->elements[1]);
                int score = SystemUtils::toInt(std::get<std::string>(args->elements[2]), 0);

                Player* seller = ll::service::getLevel()->getPlayer(mce::UUID::fromString(sellerUuid));
                if (!seller) {
                    return owner.cancelTrade(player)
                        .or_else([](ll::Error e) -> ll::Expected<bool> {
                            if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::TradeNotFound))
                                return false;

                            return ll::Unexpected(e);
                        })
                        .transform([](bool) -> void {});
                }

                auto ctx = std::make_shared<frontend::ArrayValue>();
                ctx->elements.emplace_back(player.getUuid().asString());
                ctx->elements.emplace_back(sellerUuid);
                ctx->elements.emplace_back(slot);
                ctx->elements.emplace_back(score);
                ctx->elements.emplace_back("");

                return form::GUIManager::getInstance().open("market.trade", "market.trade.confirm", form::GUIManagerType::CustomForm, *seller, ctx);
            });

            form::GUIManager::getInstance().registerCallback("market.trade.accept", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                if (args->elements.size() != 2 ||
                    !std::holds_alternative<int>(args->elements[0]) ||
                    !std::holds_alternative<int>(args->elements[1]))
                    return ll::makeStringError("market.trade.accept: must take exactly two int parameters");

                int slot = std::get<int>(args->elements[0]);
                int score = std::get<int>(args->elements[1]);

                return owner.acceptTrade(player, slot, score)
                    .or_else([](ll::Error e) -> ll::Expected<bool> {
                        if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::TradeNotFound))
                            return false;

                        return ll::Unexpected(e);
                    })
                    .transform([&player](bool ok) -> void {
                        if (!ok) {
                            auto language = LanguagePlugin::getShared()->getLanguage(player);
                            if (language.has_value())
                                player.sendMessage(tr(language.value(), "market.gui.error"));
                        }
                    });
            });

            form::GUIManager::getInstance().registerCallback("market.trade.cancel", [&owner](frontend::ArrayRef, Player& player) -> ll::Expected<void> {
                return owner.cancelTrade(player)
                    .or_else([](ll::Error e) -> ll::Expected<bool> {
                        if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::TradeNotFound))
                            return false;

                        return ll::Unexpected(e);
                    })
                    .transform([](bool) -> void {});
            });

            form::GUIManager::getInstance().registerValue("market.store.list", [listVisibleStores](Player& player) -> ll::Expected<frontend::ArrayRef> {
                return listVisibleStores(player)
                    .transform([](const std::vector<std::pair<std::string, std::string>>& items) -> frontend::ArrayRef {
                        auto values = std::make_shared<frontend::ArrayValue>();

                        for (const auto& [name, id] : items)
                            values->elements.emplace_back(name);

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerValue("market.store.inventory", [listSellableInventory](Player& player) -> ll::Expected<frontend::ArrayRef> {
                return listSellableInventory(player)
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

            form::GUIManager::getInstance().registerRequest("market.quote.enabled", [&owner](frontend::ArrayRef, Player&) -> ll::Expected<frontend::ArrayRef> {
                auto values = std::make_shared<frontend::ArrayValue>();
                values->elements.emplace_back(owner.getOptions().StoreQuoteEnabled);

                return values;
            });

            form::GUIManager::getInstance().registerRequest("market.wanted.enabled", [&owner](frontend::ArrayRef, Player&) -> ll::Expected<frontend::ArrayRef> {
                auto values = std::make_shared<frontend::ArrayValue>();
                values->elements.emplace_back(owner.getOptions().StoreWantedEnabled);

                return values;
            });

            form::GUIManager::getInstance().registerRequest("market.auction.enabled", [&owner](frontend::ArrayRef, Player&) -> ll::Expected<frontend::ArrayRef> {
                auto values = std::make_shared<frontend::ArrayValue>();
                values->elements.emplace_back(owner.getOptions().StoreAuctionEnabled);

                return values;
            });

            form::GUIManager::getInstance().registerRequest("market.auction.duration.bounds", [&owner](frontend::ArrayRef, Player&) -> ll::Expected<frontend::ArrayRef> {
                auto values = std::make_shared<frontend::ArrayValue>();
                values->elements.emplace_back(owner.getOptions().StoreAuctionMinDurationMinutes);
                values->elements.emplace_back(owner.getOptions().StoreAuctionMaxDurationHours);

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

            form::GUIManager::getInstance().registerRequest("market.store.sell.slot", [listSellableInventory](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                    return ll::makeStringError("market.store.sell.slot: must take exactly one int parameter");

                int index = std::get<int>(args->elements[0]);

                return listSellableInventory(player)
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

            // ---- 行情（阶段 1）----
            auto listTopQuoteItems = [&owner](Player& player, int limit) -> ll::Expected<std::vector<std::pair<std::string, long long>>> {
                return owner.getTopVolume(limit, 7);
            };

            form::GUIManager::getInstance().registerValue("market.quote.items", [listTopQuoteItems](Player& player) -> ll::Expected<frontend::ArrayRef> {
                return LanguagePlugin::getShared()->getLanguage(player)
                    .and_then([listTopQuoteItems, &player](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                        return listTopQuoteItems(player, 50)
                            .transform([language](const std::vector<std::pair<std::string, long long>>& items) -> frontend::ArrayRef {
                                auto values = std::make_shared<frontend::ArrayValue>();

                                for (const auto& [name, count] : items)
                                    values->elements.emplace_back(fmt::format(fmt::runtime(tr(language, "market.gui.quote.line")), name, count));

                                return values;
                            });
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.quote.id", [listTopQuoteItems](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                    return ll::makeStringError("market.quote.id: must take exactly one int parameter");

                int index = std::get<int>(args->elements[0]);

                return listTopQuoteItems(player, 50)
                    .and_then([index](const std::vector<std::pair<std::string, long long>>& items) -> ll::Expected<frontend::ArrayRef> {
                        auto values = std::make_shared<frontend::ArrayValue>();

                        if (index < 0 || index >= static_cast<int>(items.size()))
                            return values;

                        values->elements.emplace_back(items.at(static_cast<size_t>(index)).first);

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.quote.info", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                    return ll::makeStringError("market.quote.info: must take exactly one string parameter");

                std::string itemName = std::get<std::string>(args->elements[0]);
                auto values = std::make_shared<frontend::ArrayValue>();

                return LanguagePlugin::getShared()->getLanguage(player)
                    .and_then([&owner, itemName, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                        return owner.getQuote(itemName)
                            .transform([language, itemName, values](const std::optional<QuoteInfo>& info) -> frontend::ArrayRef {
                                if (!info.has_value()) {
                                    values->elements.emplace_back(tr(language, "market.gui.quote.empty"));

                                    return values;
                                }

                                values->elements.emplace_back(fmt::format(
                                    fmt::runtime(tr(language, "market.gui.quote.info")),
                                    itemName,
                                    info->avg7d, info->avg30d, info->min30d, info->max30d, info->count30d, info->lastPrice
                                ));

                                return values;
                            });
                    });
            });

            // ---- 求购（阶段 2）----
            auto listVisibleWanted = [&owner](Player& player) -> ll::Expected<std::vector<std::pair<std::string, std::string>>> {
                return owner.getWantedList()
                    .and_then([&owner, &player](const std::vector<std::string>& ids) -> ll::Expected<std::vector<std::pair<std::string, std::string>>> {
                        std::vector<std::pair<std::string, std::string>> items;

                        for (const std::string& id : ids) {
                            auto dataResult = owner.getWantedData(id);
                            if (!dataResult.has_value())
                                continue;

                            const auto& data = dataResult.value();
                            auto blacklists = owner.getBlacklist(data.at("wanted_uuid"));
                            if (!blacklists.has_value())
                                return ll::Unexpected(blacklists.error());

                            if (std::find(blacklists.value().begin(), blacklists.value().end(), player.getUuid().asString()) != blacklists.value().end())
                                continue;

                            int remaining = SystemUtils::toInt(data.at("amount_total"), 0) - SystemUtils::toInt(data.at("amount_filled"), 0);
                            if (remaining <= 0)
                                continue;

                            auto language = LanguagePlugin::getShared()->getLanguage(player);
                            if (!language.has_value())
                                return ll::Unexpected(language.error());

                            items.emplace_back(fmt::format(
                                fmt::runtime(tr(language.value(), "market.gui.wanted.line")),
                                data.at("item_name"), data.at("unit_price"), remaining
                            ), id);
                        }

                        return items;
                    });
            };

            form::GUIManager::getInstance().registerValue("market.wanted.items", [listVisibleWanted](Player& player) -> ll::Expected<frontend::ArrayRef> {
                return listVisibleWanted(player)
                    .transform([](const std::vector<std::pair<std::string, std::string>>& items) -> frontend::ArrayRef {
                        auto values = std::make_shared<frontend::ArrayValue>();

                        for (const auto& [name, id] : items)
                            values->elements.emplace_back(name);

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.wanted.id", [listVisibleWanted](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                    return ll::makeStringError("market.wanted.id: must take exactly one int parameter");

                int index = std::get<int>(args->elements[0]);

                return listVisibleWanted(player)
                    .and_then([index](const std::vector<std::pair<std::string, std::string>>& items) -> ll::Expected<frontend::ArrayRef> {
                        auto values = std::make_shared<frontend::ArrayValue>();

                        if (index < 0 || index >= static_cast<int>(items.size()))
                            return values;

                        values->elements.emplace_back(items.at(static_cast<size_t>(index)).second);

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.wanted.info", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                    return ll::makeStringError("market.wanted.info: must take exactly one string parameter");

                std::string id = std::get<std::string>(args->elements[0]);
                auto values = std::make_shared<frontend::ArrayValue>();

                return LanguagePlugin::getShared()->getLanguage(player)
                    .and_then([&owner, id, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                        return owner.getWantedData(id)
                            .transform([language, id, values](const std::unordered_map<std::string, std::string>& data) -> frontend::ArrayRef {
                                values->elements.emplace_back(fmt::format(
                                    fmt::runtime(tr(language, "market.gui.wanted.info")),
                                    data.at("item_name"), data.at("wanted_name"), data.at("unit_price"),
                                    data.at("amount_filled"), data.at("amount_total"), data.at("expire_at")
                                ));

                                return values;
                            });
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.wanted.fill.submit", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 2 ||
                    !std::holds_alternative<std::string>(args->elements[0]) ||
                    !std::holds_alternative<std::string>(args->elements[1]))
                    return ll::makeStringError("market.wanted.fill.submit: must take one string and one string parameter");

                auto values = std::make_shared<frontend::ArrayValue>();

                return owner.fillWanted(player, std::get<std::string>(args->elements[0]), SystemUtils::toInt(std::get<std::string>(args->elements[1]), 0))
                    .or_else([](ll::Error e) -> ll::Expected<bool> {
                        if (e.isA<ll::ErrorCodeError>() &&
                            (e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::WantedExpired) ||
                                e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::WantedFilled)))
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

            auto listMyWanted = [&owner](Player& player) -> ll::Expected<std::vector<std::pair<std::string, std::string>>> {
                return owner.getWantedItems(player)
                    .and_then([&owner, &player](const std::vector<std::string>& ids) -> ll::Expected<std::vector<std::pair<std::string, std::string>>> {
                        std::vector<std::pair<std::string, std::string>> items;

                        for (const std::string& id : ids) {
                            auto dataResult = owner.getWantedData(id);
                            if (!dataResult.has_value())
                                continue;

                            const auto& data = dataResult.value();

                            auto language = LanguagePlugin::getShared()->getLanguage(player);
                            if (!language.has_value())
                                return ll::Unexpected(language.error());

                            items.emplace_back(fmt::format(
                                fmt::runtime(tr(language.value(), "market.gui.wanted.line")),
                                data.at("item_name"), data.at("unit_price"),
                                SystemUtils::toInt(data.at("amount_total"), 0) - SystemUtils::toInt(data.at("amount_filled"), 0)
                            ), id);
                        }

                        return items;
                    });
            };

            form::GUIManager::getInstance().registerValue("market.my.wanted", [listMyWanted](Player& player) -> ll::Expected<frontend::ArrayRef> {
                return listMyWanted(player)
                    .transform([](const std::vector<std::pair<std::string, std::string>>& items) -> frontend::ArrayRef {
                        auto values = std::make_shared<frontend::ArrayValue>();

                        for (const auto& [name, id] : items)
                            values->elements.emplace_back(name);

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.my.wanted.id", [listMyWanted](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                    return ll::makeStringError("market.my.wanted.id: must take exactly one int parameter");

                int index = std::get<int>(args->elements[0]);

                return listMyWanted(player)
                    .and_then([index](const std::vector<std::pair<std::string, std::string>>& items) -> ll::Expected<frontend::ArrayRef> {
                        auto values = std::make_shared<frontend::ArrayValue>();

                        if (index < 0 || index >= static_cast<int>(items.size()))
                            return values;

                        values->elements.emplace_back(items.at(static_cast<size_t>(index)).second);

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.wanted.cancel.submit", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                    return ll::makeStringError("market.wanted.cancel.submit: must take exactly one string parameter");

                auto values = std::make_shared<frontend::ArrayValue>();

                return owner.cancelWanted(player, std::get<std::string>(args->elements[0]))
                    .or_else([](ll::Error e) -> ll::Expected<bool> {
                        if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::WantedNotFound))
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

            form::GUIManager::getInstance().registerRequest("market.wanted.slot", [listSellableInventory](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                    return ll::makeStringError("market.wanted.slot: must take exactly one int parameter");

                int index = std::get<int>(args->elements[0]);

                return listSellableInventory(player)
                    .and_then([index](const std::vector<std::pair<std::string, int>>& items) -> ll::Expected<frontend::ArrayRef> {
                        auto values = std::make_shared<frontend::ArrayValue>();

                        if (index < 0 || index >= static_cast<int>(items.size()))
                            return values;

                        values->elements.emplace_back(items.at(static_cast<size_t>(index)).second);

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.wanted.create.submit", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 4)
                    return ll::makeStringError("market.wanted.create.submit: must take four parameters");

                auto values = std::make_shared<frontend::ArrayValue>();

                int slot = std::holds_alternative<int>(args->elements[0])
                    ? std::get<int>(args->elements[0])
                    : SystemUtils::toInt(std::get<std::string>(args->elements[0]), -1);

                return owner.createWanted(
                    player,
                    slot,
                    std::get<std::string>(args->elements[1]),
                    SystemUtils::toInt(std::get<std::string>(args->elements[2]), 0),
                    SystemUtils::toInt(std::get<std::string>(args->elements[3]), 0)
                )
                    .or_else([](ll::Error e) -> ll::Expected<bool> {
                        if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::WantedFrozenFundFailed))
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

            // ---- 拍卖（阶段 3）----
            auto listVisibleAuctions = [&owner](Player& player) -> ll::Expected<std::vector<std::pair<std::string, std::string>>> {
                return owner.getAuctionList()
                    .and_then([&owner, &player](const std::vector<std::string>& ids) -> ll::Expected<std::vector<std::pair<std::string, std::string>>> {
                        std::vector<std::pair<std::string, std::string>> items;

                        for (const std::string& id : ids) {
                            auto dataResult = owner.getAuctionData(id);
                            if (!dataResult.has_value())
                                continue;

                            const auto& data = dataResult.value();
                            if (data.at("settled") == "1")
                                continue;

                            auto language = LanguagePlugin::getShared()->getLanguage(player);
                            if (!language.has_value())
                                return ll::Unexpected(language.error());

                            std::string bidder = data.at("bidder_name");
                            items.emplace_back(fmt::format(
                                fmt::runtime(tr(language.value(), "market.gui.auction.line")),
                                data.at("item_name"), data.at("current_price"), bidder.empty() ? "-" : bidder
                            ), id);
                        }

                        return items;
                    });
            };

            form::GUIManager::getInstance().registerValue("market.auction.items", [listVisibleAuctions](Player& player) -> ll::Expected<frontend::ArrayRef> {
                return listVisibleAuctions(player)
                    .transform([](const std::vector<std::pair<std::string, std::string>>& items) -> frontend::ArrayRef {
                        auto values = std::make_shared<frontend::ArrayValue>();

                        for (const auto& [name, id] : items)
                            values->elements.emplace_back(name);

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.auction.id", [listVisibleAuctions](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                    return ll::makeStringError("market.auction.id: must take exactly one int parameter");

                int index = std::get<int>(args->elements[0]);

                return listVisibleAuctions(player)
                    .and_then([index](const std::vector<std::pair<std::string, std::string>>& items) -> ll::Expected<frontend::ArrayRef> {
                        auto values = std::make_shared<frontend::ArrayValue>();

                        if (index < 0 || index >= static_cast<int>(items.size()))
                            return values;

                        values->elements.emplace_back(items.at(static_cast<size_t>(index)).second);

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.auction.info", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                    return ll::makeStringError("market.auction.info: must take exactly one string parameter");

                std::string id = std::get<std::string>(args->elements[0]);
                auto values = std::make_shared<frontend::ArrayValue>();

                return LanguagePlugin::getShared()->getLanguage(player)
                    .and_then([&owner, id, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                        return owner.getAuctionData(id)
                            .transform([language, id, values](const std::unordered_map<std::string, std::string>& data) -> frontend::ArrayRef {
                                values->elements.emplace_back(fmt::format(
                                    fmt::runtime(tr(language, "market.gui.auction.info")),
                                    data.at("item_name"), data.at("seller_name"), data.at("start_price"),
                                    data.at("current_price"), data.at("bidder_name"), data.at("end_at")
                                ));

                                return values;
                            });
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.auction.bid.submit", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 2 ||
                    !std::holds_alternative<std::string>(args->elements[0]) ||
                    !std::holds_alternative<std::string>(args->elements[1]))
                    return ll::makeStringError("market.auction.bid.submit: must take one string and one string parameter");

                auto values = std::make_shared<frontend::ArrayValue>();

                return owner.bidAuction(player, std::get<std::string>(args->elements[0]), SystemUtils::toInt(std::get<std::string>(args->elements[1]), 0))
                    .or_else([](ll::Error e) -> ll::Expected<bool> {
                        if (e.isA<ll::ErrorCodeError>() &&
                            (e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::AuctionBidTooLow) ||
                                e.as<ll::ErrorCodeError>().ec == MarketPlugin::makeErrorCode(MarketPluginErrorCode::AuctionNotFound)))
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

            auto listMyAuctions = [&owner](Player& player) -> ll::Expected<std::vector<std::pair<std::string, std::string>>> {
                return owner.getAuctionItems(player)
                    .and_then([&owner, &player](const std::vector<std::string>& ids) -> ll::Expected<std::vector<std::pair<std::string, std::string>>> {
                        std::vector<std::pair<std::string, std::string>> items;

                        for (const std::string& id : ids) {
                            auto dataResult = owner.getAuctionData(id);
                            if (!dataResult.has_value())
                                continue;

                            const auto& data = dataResult.value();

                            auto language = LanguagePlugin::getShared()->getLanguage(player);
                            if (!language.has_value())
                                return ll::Unexpected(language.error());

                            items.emplace_back(fmt::format(
                                fmt::runtime(tr(language.value(), "market.gui.auction.line")),
                                data.at("item_name"), data.at("current_price"), data.at("bidder_name")
                            ), id);
                        }

                        return items;
                    });
            };

            form::GUIManager::getInstance().registerValue("market.my.auction", [listMyAuctions](Player& player) -> ll::Expected<frontend::ArrayRef> {
                return listMyAuctions(player)
                    .transform([](const std::vector<std::pair<std::string, std::string>>& items) -> frontend::ArrayRef {
                        auto values = std::make_shared<frontend::ArrayValue>();

                        for (const auto& [name, id] : items)
                            values->elements.emplace_back(name);

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.my.auction.id", [listMyAuctions](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                    return ll::makeStringError("market.my.auction.id: must take exactly one int parameter");

                int index = std::get<int>(args->elements[0]);

                return listMyAuctions(player)
                    .and_then([index](const std::vector<std::pair<std::string, std::string>>& items) -> ll::Expected<frontend::ArrayRef> {
                        auto values = std::make_shared<frontend::ArrayValue>();

                        if (index < 0 || index >= static_cast<int>(items.size()))
                            return values;

                        values->elements.emplace_back(items.at(static_cast<size_t>(index)).second);

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.auction.slot", [listSellableInventory](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                    return ll::makeStringError("market.auction.slot: must take exactly one int parameter");

                int index = std::get<int>(args->elements[0]);

                return listSellableInventory(player)
                    .and_then([index](const std::vector<std::pair<std::string, int>>& items) -> ll::Expected<frontend::ArrayRef> {
                        auto values = std::make_shared<frontend::ArrayValue>();

                        if (index < 0 || index >= static_cast<int>(items.size()))
                            return values;

                        values->elements.emplace_back(items.at(static_cast<size_t>(index)).second);

                        return values;
                    });
            });

            form::GUIManager::getInstance().registerRequest("market.auction.create.submit", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                if (args->elements.size() != 4)
                    return ll::makeStringError("market.auction.create.submit: must take four parameters");

                auto values = std::make_shared<frontend::ArrayValue>();

                int slot = std::holds_alternative<int>(args->elements[0])
                    ? std::get<int>(args->elements[0])
                    : SystemUtils::toInt(std::get<std::string>(args->elements[0]), -1);

                return owner.createAuction(
                    player,
                    slot,
                    std::get<std::string>(args->elements[1]),
                    SystemUtils::toInt(std::get<std::string>(args->elements[2]), 0),
                    SystemUtils::toInt(std::get<std::string>(args->elements[3]), 0)
                )
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
        return {};
    }
}
