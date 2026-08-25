#include <memory>
#include <string>
#include <vector>
#include <utility>

#include <fmt/core.h>

#include <ll/api/Expected.h>
#include <ll/api/service/Bedrock.h>

#include <mc/world/level/Level.h>
#include <mc/world/actor/player/Player.h>

#include "LOICollectionA/include/server/Plugins/LanguagePlugin.h"

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/mc-server/ScoreboardUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/frontend/AST.h"

#include "LOICollectionA/include/form/GUIManager.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/wallet/WalletGui.h"
#include "LOICollectionA/include/server/Plugins/wallet/WalletGuiDetail.h"
#include "LOICollectionA/include/server/Plugins/wallet/WalletPlugin.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    ll::Expected<void> WalletGui::registerAll(WalletPlugin& owner) {
        this->registerInfo(owner);
        this->registerTransfer(owner);
        this->registerRedEnvelope(owner);
        this->registerRank(owner);
        this->registerBank(owner);

        return {};
    }

    void WalletGui::registerInfo(WalletPlugin& owner) {
        form::GUIManager::getInstance().registerValue("wallet.players.online", [](Player&) -> frontend::ArrayRef {
            auto values = std::make_shared<frontend::ArrayValue>();

            ll::service::getLevel()->forEachPlayer([&values](Player& target) -> bool {
                if (!target.isSimulatedPlayer())
                    values->elements.emplace_back(target.getRealName());

                return true;
            });

            return values;
        });

        form::GUIManager::getInstance().registerValue("wallet.players.offline", [&owner](Player&) -> ll::Expected<frontend::ArrayRef> {
            return owner.getPlayerInfo()
                .transform([](const std::vector<std::pair<std::string, std::string>>& players) -> frontend::ArrayRef {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    for (const auto& [uuid, name] : players)
                        values->elements.emplace_back(name);

                    return values;
                });
        });

        form::GUIManager::getInstance().registerValue("wallet.history", [&owner](Player& player) -> ll::Expected<frontend::ArrayRef> {
            return owner.getPlayerLedger(player.getUuid().asString(), 50)
                .transform([](const std::vector<std::string>& lines) -> frontend::ArrayRef {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    for (const auto& line : lines)
                        values->elements.emplace_back(line);

                    return values;
                });
        });

        form::GUIManager::getInstance().registerRequest("wallet.info", [&owner](frontend::ArrayRef, Player& player) -> ll::Expected<frontend::ArrayRef> {
            auto values = std::make_shared<frontend::ArrayValue>();
            values->elements.emplace_back(ScoreboardUtils::getScore(player, owner.getTargetScoreboard()));
            values->elements.emplace_back(std::to_string(owner.getExchangeRate() * 100) + "%%");

            return values;
        });

        form::GUIManager::getInstance().registerCallback("wallet.wealth", [&owner](frontend::ArrayRef, Player& player) -> ll::Expected<void> {
            return owner.wealth(player);
        });
    }

    void WalletGui::registerTransfer(WalletPlugin& owner) {
        form::GUIManager::getInstance().registerRequest("wallet.transfer.info", [&owner](frontend::ArrayRef args, Player&) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 2 ||
                !std::holds_alternative<std::string>(args->elements[0]) ||
                !std::holds_alternative<int>(args->elements[1]))
                return ll::makeStringError("wallet.transfer.info: must take a string and an int parameter");

            auto type = std::get<std::string>(args->elements[0]);
            int index = std::get<int>(args->elements[1]);

            std::vector<std::pair<std::string, std::string>> players;
            if (type == "online") {
                ll::service::getLevel()->forEachPlayer([&players](Player& target) -> bool {
                    if (!target.isSimulatedPlayer())
                        players.emplace_back(target.getUuid().asString(), target.getRealName());

                    return true;
                });
            } else if (type == "offline") {
                auto result = owner.getPlayerInfo();
                if (!result.has_value())
                    return ll::Unexpected(result.error());

                players = result.value();
            } else {
                return ll::makeStringError("wallet.transfer.info: unknown transfer type");
            }

            if (index < 0 || index >= static_cast<int>(players.size()))
                return ll::makeStringError("wallet.transfer.info: index out of range");

            auto values = std::make_shared<frontend::ArrayValue>();
            values->elements.emplace_back(players.at(static_cast<size_t>(index)).first);
            values->elements.emplace_back(players.at(static_cast<size_t>(index)).second);

            return values;
        });

        form::GUIManager::getInstance().registerRequest("wallet.transfer.submit", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 4 ||
                !std::holds_alternative<std::string>(args->elements[0]) ||
                !std::holds_alternative<std::string>(args->elements[1]) ||
                !std::holds_alternative<std::string>(args->elements[2]) ||
                !std::holds_alternative<std::string>(args->elements[3]))
                return ll::makeStringError("wallet.transfer.submit: must take four string parameters");

            auto uuid = std::get<std::string>(args->elements[0]);
            auto name = std::get<std::string>(args->elements[1]);
            int money = SystemUtils::toInt(std::get<std::string>(args->elements[3]), 0);

            auto result = owner.forTransfer(player, uuid, name, money);
            auto values = std::make_shared<frontend::ArrayValue>();

            if (!result.has_value()) {
                auto code = static_cast<WalletPluginErrorCode>(result.error().as<ll::ErrorCodeError>().ec.value());

                if (code == WalletPluginErrorCode::ConfirmRequired) {
                    long long fee = static_cast<long long>(money * owner.getExchangeRate());

                    values->elements.emplace_back(false);
                    values->elements.emplace_back(true);
                    values->elements.emplace_back(static_cast<int>(fee));
                    values->elements.emplace_back(static_cast<int>(money - fee));
                    return values;
                }

                if (code == WalletPluginErrorCode::BelowMinimum || code == WalletPluginErrorCode::DailyLimitExceeded || code == WalletPluginErrorCode::CooldownActive) {
                    return LanguagePlugin::getShared()->getLanguage(player)
                        .and_then([ec = result.error().as<ll::ErrorCodeError>().ec, &player](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                            player.sendMessage(walletGui::walletLimitMessage(language, ec));

                            auto fail = std::make_shared<frontend::ArrayValue>();
                            fail->elements.emplace_back(false);
                            fail->elements.emplace_back(false);
                            return fail;
                        });
                }

                modules::defaultErrorHandler<WalletPlugin>(result.error());

                values->elements.emplace_back(false);
                values->elements.emplace_back(false);
                return values;
            }

            if (!result.value()) {
                return LanguagePlugin::getShared()->getLanguage(player)
                    .and_then([&player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                        player.sendMessage(tr(language, "wallet.tips.transfer"));

                        values->elements.emplace_back(false);
                        values->elements.emplace_back(false);
                        return values;
                    });
            }

            values->elements.emplace_back(true);
            values->elements.emplace_back(false);
            return values;
        });

        form::GUIManager::getInstance().registerCallback("wallet.transfer.confirm", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
            if (args->elements.size() != 3 ||
                !std::holds_alternative<std::string>(args->elements[0]) ||
                !std::holds_alternative<std::string>(args->elements[1]) ||
                !std::holds_alternative<std::string>(args->elements[2]))
                return ll::makeStringError("wallet.transfer.confirm: must take three string parameters");

            auto uuid = std::get<std::string>(args->elements[0]);
            auto name = std::get<std::string>(args->elements[1]);
            int money = SystemUtils::toInt(std::get<std::string>(args->elements[2]), 0);

            auto result = owner.forTransfer(player, uuid, name, money, true);
            if (!result.has_value()) {
                return LanguagePlugin::getShared()->getLanguage(player)
                    .and_then([ec = result.error().as<ll::ErrorCodeError>().ec, &player](const std::string& language) -> ll::Expected<void> {
                        player.sendMessage(walletGui::walletLimitMessage(language, ec));

                        return {};
                    });
            }

            if (!result.value()) {
                return LanguagePlugin::getShared()->getLanguage(player)
                    .and_then([&player](const std::string& language) -> ll::Expected<void> {
                        player.sendMessage(tr(language, "wallet.tips.transfer"));

                        return {};
                    });
            }

            return {};
        });
    }

    void WalletGui::registerRedEnvelope(WalletPlugin& owner) {
        form::GUIManager::getInstance().registerRequest("wallet.redenvelope.submit", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 4 ||
                !std::holds_alternative<std::string>(args->elements[0]) ||
                !std::holds_alternative<std::string>(args->elements[1]) ||
                !std::holds_alternative<std::string>(args->elements[2]) ||
                !std::holds_alternative<std::string>(args->elements[3]))
                return ll::makeStringError("wallet.redenvelope.submit: must take four string parameters");

            auto values = std::make_shared<frontend::ArrayValue>();
            auto key = std::get<std::string>(args->elements[2]);

            if (key.empty()) {
                return LanguagePlugin::getShared()->getLanguage(player)
                    .and_then([&player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                        player.sendMessage(tr(language, "generic.tips.noinput"));

                        values->elements.emplace_back(false);
                        return values;
                    });
            }

            int score = SystemUtils::toInt(std::get<std::string>(args->elements[0]), 0);
            int count = SystemUtils::toInt(std::get<std::string>(args->elements[1]), 0);

            std::vector<std::string> targets;
            std::string targetsText = std::get<std::string>(args->elements[3]);
            if (!targetsText.empty()) {
                size_t start = 0;
                while (start <= targetsText.size()) {
                    size_t comma = targetsText.find(',', start);
                    std::string token = targetsText.substr(start, comma == std::string::npos ? std::string::npos : comma - start);

                    size_t first = token.find_first_not_of(" \t");
                    size_t last = token.find_last_not_of(" \t");
                    if (first != std::string::npos)
                        token = token.substr(first, last - first + 1);

                    if (!token.empty())
                        targets.emplace_back(token);

                    if (comma == std::string::npos)
                        break;
                    start = comma + 1;
                }
            }

            if (score <= 0 || count <= 0 || ScoreboardUtils::getScore(player, owner.getTargetScoreboard()) < score * count) {
                return LanguagePlugin::getShared()->getLanguage(player)
                    .and_then([&player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                        player.sendMessage(tr(language, "wallet.tips.redenvelope"));

                        values->elements.emplace_back(false);
                        return values;
                    });
            }

            auto result = owner.redenvelope(player, key, score, count, targets);
            if (!result.has_value()) {
                return LanguagePlugin::getShared()->getLanguage(player)
                    .and_then([ec = result.error().as<ll::ErrorCodeError>().ec, &player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                        player.sendMessage(walletGui::walletLimitMessage(language, ec));

                        values->elements.emplace_back(false);
                        return values;
                    });
            }

            values->elements.emplace_back(true);
            return values;
        });
    }

    void WalletGui::registerRank(WalletPlugin& owner) {
        form::GUIManager::getInstance().registerValue("wallet.rank", [&owner](Player&) -> ll::Expected<frontend::ArrayRef> {
            return owner.getWealthRanking(owner.getOptions().WealthTopSize > 0 ? owner.getOptions().WealthTopSize : 50)
                .transform([](const std::vector<std::pair<std::string, long long>>& ranking) -> frontend::ArrayRef {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    for (size_t i = 0; i < ranking.size(); ++i) {
                        const auto& [name, balance] = ranking.at(i);

                        values->elements.emplace_back(fmt::format(fmt::runtime(tr({}, "wallet.rank.row")),
                            i + 1, name, balance));
                    }

                    return values;
                });
        });

        form::GUIManager::getInstance().registerRequest("wallet.rank.self", [&owner](frontend::ArrayRef, Player& player) -> ll::Expected<frontend::ArrayRef> {
            auto values = std::make_shared<frontend::ArrayValue>();

            auto result = owner.getWealthRank(player.getUuid().asString());
            if (!result.has_value())
                return ll::Unexpected(result.error());

            values->elements.emplace_back(result.value().first);
            values->elements.emplace_back(static_cast<int>(result.value().second));

            return values;
        });
    }

    void WalletGui::registerBank(WalletPlugin& owner) {
        form::GUIManager::getInstance().registerRequest("wallet.bank.info", [&owner](frontend::ArrayRef, Player& player) -> ll::Expected<frontend::ArrayRef> {
            auto values = std::make_shared<frontend::ArrayValue>();

            auto principal = owner.getBankPrincipal(player.getUuid().asString());
            if (!principal.has_value())
                return ll::Unexpected(principal.error());

            auto interest = owner.getBankInterest(player.getUuid().asString());
            if (!interest.has_value())
                return ll::Unexpected(interest.error());

            values->elements.emplace_back(static_cast<int>(principal.value()));
            values->elements.emplace_back(static_cast<int>(interest.value()));

            return values;
        });

        form::GUIManager::getInstance().registerRequest("wallet.bank.deposit", [&owner](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
            if (args->elements.size() != 1 ||
                !std::holds_alternative<std::string>(args->elements[0]))
                return ll::makeStringError("wallet.bank.deposit: must take one string parameter");

            auto values = std::make_shared<frontend::ArrayValue>();

            int amount = SystemUtils::toInt(std::get<std::string>(args->elements[0]), 0);

            auto result = owner.bankDeposit(player, amount);
            if (!result.has_value()) {
                return LanguagePlugin::getShared()->getLanguage(player)
                    .and_then([ec = result.error().as<ll::ErrorCodeError>().ec, &player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                        player.sendMessage(walletGui::walletLimitMessage(language, ec));

                        values->elements.emplace_back(false);
                        return values;
                    });
            }

            values->elements.emplace_back(true);
            return values;
        });

        form::GUIManager::getInstance().registerRequest("wallet.bank.withdraw", [&owner](frontend::ArrayRef, Player& player) -> ll::Expected<frontend::ArrayRef> {
            auto values = std::make_shared<frontend::ArrayValue>();

            auto result = owner.bankWithdraw(player);
            if (!result.has_value()) {
                return LanguagePlugin::getShared()->getLanguage(player)
                    .and_then([ec = result.error().as<ll::ErrorCodeError>().ec, &player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                        player.sendMessage(walletGui::walletLimitMessage(language, ec));

                        values->elements.emplace_back(false);
                        return values;
                    });
            }

            return LanguagePlugin::getShared()->getLanguage(player)
                .and_then([&player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                    player.sendMessage(tr(language, "wallet.bank.withdraw.success"));

                    values->elements.emplace_back(true);
                    return values;
                });
        });
    }
}
