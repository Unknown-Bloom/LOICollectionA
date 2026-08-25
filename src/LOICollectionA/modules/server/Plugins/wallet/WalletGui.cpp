#include "LOICollectionA/include/server/Plugins/wallet/WalletDetail.h"

namespace LOICollection::server::Plugins {
    void WalletPlugin::registeryCommand() {
        ll::command::CommandHandle& command = ll::command::CommandRegistrar::getInstance(false)
            .getOrCreateCommand("wallet", tr({}, "commands.wallet.description"), CommandPermissionLevel::Any, CommandFlagValue::NotCheat | CommandFlagValue::Async);
        command.overload<operation>().text("transfer").required("Target").required("Score").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                Actor* entity = origin.getEntity();
                if (entity == nullptr || !entity->isType(ActorType::Player))
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
                Player& player = *static_cast<Player*>(entity);

                CommandSelectorResults<Player> results = param.Target.results(origin);
                if (results.empty())
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));

                std::string mScoreboard = this->mImpl->options.TargetScoreboard;

                if (this->mImpl->options.TransferMinAmount > 0 && param.Score < this->mImpl->options.TransferMinAmount)
                    return output.error(tr(origin.getLocaleCode(), "wallet.limit.min"), param.Score, this->mImpl->options.TransferMinAmount);

                if (this->mImpl->options.TransferConfirmThreshold > 0 && param.Score > this->mImpl->options.TransferConfirmThreshold)
                    return output.error(tr(origin.getLocaleCode(), "wallet.limit.confirm"));

                int mMoney = param.Score * static_cast<int>(results.size());
                if (this->mImpl->options.TransferDailyLimit > 0 || this->mImpl->options.TransferCooldownSeconds > 0) {
                    if (auto verification = this->validateTransfer(player.getUuid().asString(), mMoney); !verification.has_value())
                        return output.error(walletLimitMessage(origin.getLocaleCode(), verification.error()));
                }

                if (ScoreboardUtils::getScore(player, mScoreboard) < mMoney || param.Score < 0)
                    return output.error(tr(origin.getLocaleCode(), "commands.wallet.error.score"));

                ScoreboardUtils::reduceScore(player, mScoreboard, mMoney);

                int mTargetMoney = static_cast<int>(param.Score * (1 - this->mImpl->options.ExchangeRate));
                for (Player*& target : results) {
                    ScoreboardUtils::addScore(*target, mScoreboard, mTargetMoney);

                    long long perTargetFee = static_cast<long long>(param.Score) - mTargetMoney;
                    this->appendLedger(player.getUuid().asString(), player.getRealName(), target->getUuid().asString(), target->getRealName(), mTargetMoney, perTargetFee, "transfer")
                        .or_else(modules::defaultErrorHandler<WalletPlugin>);

                    this->emitWalletTransfer(player.getUuid().asString(), player.getRealName(), target->getUuid().asString(), target->getRealName(), mTargetMoney, perTargetFee, "transfer");
                }

                long long fee = static_cast<long long>(mMoney) - static_cast<long long>(mTargetMoney) * results.size();
                if (fee > 0)
                    this->accumulateFee(fee).or_else(modules::defaultErrorHandler<WalletPlugin>);

                this->updateTransferCooldown(player.getUuid().asString());

                output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.wallet.success.transfer")), param.Score, results.size());
            });
        command.overload().text("gui").execute([](CommandOrigin const& origin, CommandOutput& output) -> void {
            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);

            form::GUIManager::getInstance().open("wallet", "wallet.open", form::GUIManagerType::CustomForm, player)
                .or_else(modules::defaultErrorHandler<WalletPlugin>);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
        command.overload().text("wealth").execute([this](CommandOrigin const& origin, CommandOutput& output) -> void {
            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);

            this->wealth(player).or_else(modules::defaultErrorHandler<WalletPlugin>);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
        command.overload().text("history").execute([this](CommandOrigin const& origin, CommandOutput& output) -> void {
            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);

            if (!this->mImpl->options.WalletHistoryEnabled)
                return output.error(tr(origin.getLocaleCode(), "wallet.history.disabled"));

            this->sendHistory(player, player.getUuid().asString(), player.getRealName(), 20)
                .or_else(modules::defaultErrorHandler<WalletPlugin>);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
        command.overload().text("bank").execute([this](CommandOrigin const& origin, CommandOutput& output) -> void {
            if (!this->mImpl->options.WalletBankEnabled)
                return output.error(tr(origin.getLocaleCode(), "wallet.bank.disabled"));

            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);

            form::GUIManager::getInstance().open("wallet", "wallet.bank", form::GUIManagerType::CustomForm, player)
                .or_else(modules::defaultErrorHandler<WalletPlugin>);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
        command.overload().text("rank").execute([this](CommandOrigin const& origin, CommandOutput& output) -> void {
            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);

            form::GUIManager::getInstance().open("wallet", "wallet.rank", form::GUIManagerType::PaginatedForm, player)
                .or_else(modules::defaultErrorHandler<WalletPlugin>);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
        command.overload<operationQuery>().text("query").required("PlayerName").execute([this](CommandOrigin const& origin, CommandOutput& output, operationQuery const& param) -> void {
            if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors)
                return output.error(tr(origin.getLocaleCode(), "commands.generic.permission"));

            if (!this->mImpl->options.WalletHistoryEnabled)
                return output.error(tr(origin.getLocaleCode(), "wallet.history.disabled"));

            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& receiver = *static_cast<Player*>(entity);

            std::string name = param.PlayerName;

            std::string uuid;
            if (Player* candidate = ll::service::getLevel()->getPlayer(name); candidate)
                uuid = candidate->getUuid().asString();

            if (uuid.empty()) {
                auto result = this->getPlayerInfo();
                if (!result.has_value())
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.unknown"));
                for (const auto& [id, playerName] : result.value())
                    if (playerName == name) {
                        uuid = id;
                        break;
                    }
            }

            if (uuid.empty())
                return output.error(fmt::runtime(tr(origin.getLocaleCode(), "wallet.query.notfound")), name);

            this->sendHistory(receiver, uuid, name, 50).or_else(modules::defaultErrorHandler<WalletPlugin>);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), name);
        });
        command.overload<operationQueryId>().text("rinfo").required("EnvelopeId").execute([this](CommandOrigin const& origin, CommandOutput& output, operationQueryId const& param) -> void {
            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);

            auto lines = this->getEnvelopeStats(param.EnvelopeId);
            if (!lines.has_value())
                return output.error(tr(origin.getLocaleCode(), "commands.generic.unknown"));

            if (lines.value().empty())
                return output.error(fmt::runtime(tr(origin.getLocaleCode(), "wallet.rinfo.notfound")), param.EnvelopeId);

            for (const auto& line : lines.value())
                player.sendMessage(line);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
        command.overload().text("rstat").execute([this](CommandOrigin const& origin, CommandOutput& output) -> void {
            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);

            auto lines = this->getRedEnvelopeDailyStats();
            if (!lines.has_value())
                return output.error(tr(origin.getLocaleCode(), "commands.generic.unknown"));

            for (const auto& line : lines.value())
                player.sendMessage(line);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
        command.overload().text("reload").execute([this](CommandOrigin const& origin, CommandOutput& output) -> void {
            if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors)
                return output.error(tr(origin.getLocaleCode(), "commands.generic.permission"));

            output.success(tr(origin.getLocaleCode(), "commands.generic.reload"));
            
            form::GUIManager::getInstance().load("wallet", this->mImpl->mGuiPath)
                .transform([&origin, &output]() -> void {
                    output.success(tr(origin.getLocaleCode(), "commands.generic.reload.success"));
                })
                .or_else(modules::defaultErrorHandler<WalletPlugin>);
        });
    }

    ll::Expected<void> WalletPlugin::registeryUI() {
        return form::GUIManager::getInstance().load("wallet", this->mImpl->mGuiPath)
            .transform([this]() -> void {
                form::GUIManager::getInstance().registerValue("wallet.players.online", [](Player&) -> frontend::ArrayRef {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    ll::service::getLevel()->forEachPlayer([&values](Player& target) -> bool {
                        if (!target.isSimulatedPlayer())
                            values->elements.emplace_back(target.getRealName());

                        return true;
                    });

                    return values;
                });

                form::GUIManager::getInstance().registerValue("wallet.players.offline", [this](Player&) -> ll::Expected<frontend::ArrayRef> {
                    return this->getPlayerInfo()
                        .transform([](const std::vector<std::pair<std::string, std::string>>& players) -> frontend::ArrayRef {
                            auto values = std::make_shared<frontend::ArrayValue>();

                            for (const auto& [uuid, name] : players)
                                values->elements.emplace_back(name);

                            return values;
                        });
                });

                form::GUIManager::getInstance().registerValue("wallet.history", [this](Player& player) -> ll::Expected<frontend::ArrayRef> {
                    return this->getPlayerLedger(player.getUuid().asString(), 50)
                        .transform([](const std::vector<std::string>& lines) -> frontend::ArrayRef {
                            auto values = std::make_shared<frontend::ArrayValue>();

                            for (const auto& line : lines)
                                values->elements.emplace_back(line);

                            return values;
                        });
                });

                form::GUIManager::getInstance().registerRequest("wallet.info", [this](frontend::ArrayRef, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    auto values = std::make_shared<frontend::ArrayValue>();
                    values->elements.emplace_back(ScoreboardUtils::getScore(player, this->getTargetScoreboard()));
                    values->elements.emplace_back(std::to_string(this->getExchangeRate() * 100) + "%%");

                    return values;
                });

                form::GUIManager::getInstance().registerRequest("wallet.transfer.info", [this](frontend::ArrayRef args, Player&) -> ll::Expected<frontend::ArrayRef> {
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
                        auto result = this->getPlayerInfo();
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

                form::GUIManager::getInstance().registerRequest("wallet.transfer.submit", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    if (args->elements.size() != 4 ||
                        !std::holds_alternative<std::string>(args->elements[0]) ||
                        !std::holds_alternative<std::string>(args->elements[1]) ||
                        !std::holds_alternative<std::string>(args->elements[2]) ||
                        !std::holds_alternative<std::string>(args->elements[3]))
                        return ll::makeStringError("wallet.transfer.submit: must take four string parameters");

                    auto uuid = std::get<std::string>(args->elements[0]);
                    auto name = std::get<std::string>(args->elements[1]);
                    int money = SystemUtils::toInt(std::get<std::string>(args->elements[3]), 0);

                    auto result = this->forTransfer(player, uuid, name, money);
                    auto values = std::make_shared<frontend::ArrayValue>();

                    if (!result.has_value()) {
                        auto code = static_cast<WalletPluginErrorCode>(result.error().as<ll::ErrorCodeError>().ec.value());

                        if (code == WalletPluginErrorCode::ConfirmRequired) {
                            long long fee = static_cast<long long>(money * this->mImpl->options.ExchangeRate);

                            values->elements.emplace_back(false);
                            values->elements.emplace_back(true);
                            values->elements.emplace_back(static_cast<int>(fee));
                            values->elements.emplace_back(static_cast<int>(money - fee));
                            return values;
                        }

                        if (code == WalletPluginErrorCode::BelowMinimum || code == WalletPluginErrorCode::DailyLimitExceeded || code == WalletPluginErrorCode::CooldownActive) {
                            return LanguagePlugin::getShared()->getLanguage(player)
                                .and_then([ec = result.error().as<ll::ErrorCodeError>().ec, &player](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                                    player.sendMessage(walletLimitMessage(language, ec));

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

                form::GUIManager::getInstance().registerCallback("wallet.transfer.confirm", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                    if (args->elements.size() != 3 ||
                        !std::holds_alternative<std::string>(args->elements[0]) ||
                        !std::holds_alternative<std::string>(args->elements[1]) ||
                        !std::holds_alternative<std::string>(args->elements[2]))
                        return ll::makeStringError("wallet.transfer.confirm: must take three string parameters");

                    auto uuid = std::get<std::string>(args->elements[0]);
                    auto name = std::get<std::string>(args->elements[1]);
                    int money = SystemUtils::toInt(std::get<std::string>(args->elements[2]), 0);

                    auto result = this->forTransfer(player, uuid, name, money, true);
                    if (!result.has_value()) {
                        return LanguagePlugin::getShared()->getLanguage(player)
                            .and_then([ec = result.error().as<ll::ErrorCodeError>().ec, &player](const std::string& language) -> ll::Expected<void> {
                                player.sendMessage(walletLimitMessage(language, ec));

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

                form::GUIManager::getInstance().registerRequest("wallet.redenvelope.submit", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
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

                    if (score <= 0 || count <= 0 || ScoreboardUtils::getScore(player, this->getTargetScoreboard()) < score * count) {
                        return LanguagePlugin::getShared()->getLanguage(player)
                            .and_then([&player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                                player.sendMessage(tr(language, "wallet.tips.redenvelope"));

                                values->elements.emplace_back(false);
                                return values;
                            });
                    }

                    auto result = this->redenvelope(player, key, score, count, targets);
                    if (!result.has_value()) {
                        return LanguagePlugin::getShared()->getLanguage(player)
                            .and_then([ec = result.error().as<ll::ErrorCodeError>().ec, &player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                                player.sendMessage(walletLimitMessage(language, ec));

                                values->elements.emplace_back(false);
                                return values;
                            });
                    }

                    values->elements.emplace_back(true);
                    return values;
                });

                form::GUIManager::getInstance().registerValue("wallet.rank", [this](Player&) -> ll::Expected<frontend::ArrayRef> {
                    return this->getWealthRanking(this->mImpl->options.WealthTopSize > 0 ? this->mImpl->options.WealthTopSize : 50)
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

                form::GUIManager::getInstance().registerRequest("wallet.rank.self", [this](frontend::ArrayRef, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    auto result = this->getWealthRank(player.getUuid().asString());
                    if (!result.has_value())
                        return ll::Unexpected(result.error());

                    values->elements.emplace_back(result.value().first);
                    values->elements.emplace_back(static_cast<long long>(result.value().second));

                    return values;
                });

                form::GUIManager::getInstance().registerRequest("wallet.bank.info", [this](frontend::ArrayRef, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    auto principal = this->getBankPrincipal(player.getUuid().asString());
                    if (!principal.has_value())
                        return ll::Unexpected(principal.error());

                    auto interest = this->getBankInterest(player.getUuid().asString());
                    if (!interest.has_value())
                        return ll::Unexpected(interest.error());

                    values->elements.emplace_back(static_cast<long long>(principal.value()));
                    values->elements.emplace_back(static_cast<long long>(interest.value()));

                    return values;
                });

                form::GUIManager::getInstance().registerRequest("wallet.bank.deposit", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    if (args->elements.size() != 1 ||
                        !std::holds_alternative<std::string>(args->elements[0]))
                        return ll::makeStringError("wallet.bank.deposit: must take one string parameter");

                    auto values = std::make_shared<frontend::ArrayValue>();

                    int amount = SystemUtils::toInt(std::get<std::string>(args->elements[0]), 0);

                    auto result = this->bankDeposit(player, amount);
                    if (!result.has_value()) {
                        return LanguagePlugin::getShared()->getLanguage(player)
                            .and_then([ec = result.error().as<ll::ErrorCodeError>().ec, &player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                                player.sendMessage(walletLimitMessage(language, ec));

                                values->elements.emplace_back(false);
                                return values;
                            });
                    }

                    values->elements.emplace_back(true);
                    return values;
                });

                form::GUIManager::getInstance().registerRequest("wallet.bank.withdraw", [this](frontend::ArrayRef, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    auto result = this->bankWithdraw(player);
                    if (!result.has_value()) {
                        return LanguagePlugin::getShared()->getLanguage(player)
                            .and_then([ec = result.error().as<ll::ErrorCodeError>().ec, &player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                                player.sendMessage(walletLimitMessage(language, ec));

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

                form::GUIManager::getInstance().registerCallback("wallet.wealth", [this](frontend::ArrayRef, Player& player) -> ll::Expected<void> {
                    return this->wealth(player);
                });
            });
    }

}
