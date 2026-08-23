#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <unordered_map>

#include <fmt/core.h>

#include <ll/api/Expected.h>

#include <mc/world/actor/player/Player.h>

#include "LOICollectionA/include/server/Plugins/LanguagePlugin.h"

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/frontend/AST.h"

#include "LOICollectionA/include/form/GUIManager.h"

#include "LOICollectionA/include/server/Plugins/market/MarketGui.h"
#include "LOICollectionA/include/server/Plugins/market/MarketGuiDetail.h"
#include "LOICollectionA/include/server/Plugins/market/MarketPlugin.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    void MarketGui::registerWanted(MarketPlugin& owner) {
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

        form::GUIManager::getInstance().registerRequest("market.wanted.enabled", [&owner](frontend::ArrayRef, Player&) -> ll::Expected<frontend::ArrayRef> {
            auto values = std::make_shared<frontend::ArrayValue>();
            values->elements.emplace_back(owner.getOptions().StoreWantedEnabled);

            return values;
        });

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

        form::GUIManager::getInstance().registerRequest("market.wanted.slot", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                return ll::makeStringError("market.wanted.slot: must take exactly one int parameter");

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
    }
}
