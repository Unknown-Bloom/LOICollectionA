#include <memory>
#include <string>
#include <vector>
#include <utility>

#include <fmt/core.h>

#include <ll/api/Expected.h>
#include <ll/api/service/Bedrock.h>

#include <mc/world/actor/player/Player.h>
#include <mc/world/actor/player/PlayerInventory.h>
#include <mc/world/actor/player/Inventory.h>
#include <mc/world/item/ItemStack.h>
#include <mc/world/item/SaveContext.h>
#include <mc/world/item/SaveContextFactory.h>

#include "LOICollectionA/include/server/Plugins/LanguagePlugin.h"

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/frontend/AST.h"

#include "LOICollectionA/include/CallbackUtils.h"

#include "LOICollectionA/include/form/GUIManager.h"

#include "LOICollectionA/include/server/Plugins/market/MarketGui.h"
#include "LOICollectionA/include/server/Plugins/market/MarketGuiDetail.h"
#include "LOICollectionA/include/server/Plugins/market/MarketPlugin.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    void MarketGui::registerTrade(MarketPlugin& owner) {
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

        form::GUIManager::getInstance().registerRequest("market.trade.slot", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                return ll::makeStringError("market.trade.slot: must take exactly one int parameter");

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
    }
}
