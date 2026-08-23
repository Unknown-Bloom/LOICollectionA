#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <unordered_map>

#include <fmt/core.h>

#include <ll/api/Expected.h>
#include <ll/api/service/Bedrock.h>

#include <mc/world/actor/player/Player.h>
#include <mc/world/actor/player/PlayerInventory.h>
#include <mc/world/actor/player/Inventory.h>
#include <mc/world/item/ItemStack.h>
#include <mc/world/level/Level.h>
#include <mc/server/commands/CommandPermissionLevel.h>

#include "LOICollectionA/include/server/Plugins/LanguagePlugin.h"

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/frontend/AST.h"

#include "LOICollectionA/include/form/GUIManager.h"

#include "LOICollectionA/include/server/Plugins/market/MarketGui.h"
#include "LOICollectionA/include/server/Plugins/market/MarketGuiDetail.h"
#include "LOICollectionA/include/server/Plugins/market/MarketPlugin.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    ll::Expected<void> MarketGui::registerAll(MarketPlugin& owner) {
        this->registerCore(owner);
        this->registerTrade(owner);
        this->registerStore(owner);
        this->registerQuote(owner);
        this->registerWanted(owner);
        this->registerAuction(owner);

        return {};
    }

    void MarketGui::registerCore(MarketPlugin& owner) {
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

        form::GUIManager::getInstance().registerValue("market.players", [](Player& player) -> frontend::ArrayRef {
            auto values = std::make_shared<frontend::ArrayValue>();

            for (const auto& [name, uuid] : marketGui::listPlayers(player))
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

        form::GUIManager::getInstance().registerValue("market.sell.inventory", [&owner](Player& player) -> ll::Expected<frontend::ArrayRef> {
            return marketGui::listSellableInventory(owner, player)
                .transform([](const std::vector<std::pair<std::string, int>>& items) -> frontend::ArrayRef {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    for (const auto& [name, slot] : items)
                        values->elements.emplace_back(name);

                    return values;
                });
        });

        form::GUIManager::getInstance().registerValue("market.trade.items", [&owner](Player& player) -> ll::Expected<frontend::ArrayRef> {
            return marketGui::listSellableInventory(owner, player)
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

        form::GUIManager::getInstance().registerRequest("market.sell.slot", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                return ll::makeStringError("market.sell.slot: must take exactly one int parameter");

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

        form::GUIManager::getInstance().registerRequest("market.players.uuid", [](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                return ll::makeStringError("market.players.uuid: must take exactly one int parameter");

            int index = std::get<int>(args->elements[0]);
            auto players = marketGui::listPlayers(player);
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
    }
}
