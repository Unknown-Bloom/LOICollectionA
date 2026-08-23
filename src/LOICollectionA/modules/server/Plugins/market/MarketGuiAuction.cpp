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

#include "LOICollectionA/frontend/AST.h"

#include "LOICollectionA/include/form/GUIManager.h"

#include "LOICollectionA/include/server/Plugins/market/MarketGui.h"
#include "LOICollectionA/include/server/Plugins/market/MarketGuiDetail.h"
#include "LOICollectionA/include/server/Plugins/market/MarketPlugin.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    void MarketGui::registerAuction(MarketPlugin& owner) {
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

        form::GUIManager::getInstance().registerRequest("market.auction.slot", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                return ll::makeStringError("market.auction.slot: must take exactly one int parameter");

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
    }
}
