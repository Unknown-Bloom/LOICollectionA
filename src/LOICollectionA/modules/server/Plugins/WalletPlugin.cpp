#include <atomic>
#include <cmath>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <unordered_map>

#include <fmt/core.h>

#include <ll/api/Expected.h>
#include <ll/api/io/Logger.h>
#include <ll/api/io/LoggerRegistry.h>

#include <ll/api/thread/ServerThreadExecutor.h>
#include <ll/api/base/Containers.h>

#include <ll/api/service/Bedrock.h>
#include <ll/api/command/Command.h>
#include <ll/api/command/CommandHandle.h>
#include <ll/api/command/CommandRegistrar.h>
#include <ll/api/event/EventBus.h>
#include <ll/api/event/ListenerBase.h>
#include <ll/api/event/player/PlayerJoinEvent.h>
#include <ll/api/event/player/PlayerChatEvent.h>
#include <ll/api/event/player/PlayerDisconnectEvent.h>
#include <ll/api/utils/RandomUtils.h>
#include <ll/api/utils/HashUtils.h>

#include <mc/deps/core/string/HashedString.h>

#include <mc/world/level/Level.h>
#include <mc/world/actor/ActorDefinitionIdentifier.h>
#include <mc/world/actor/player/Player.h>

#include <mc/network/packet/TextPacket.h>

#include <mc/server/commands/CommandOrigin.h>
#include <mc/server/commands/CommandOutput.h>
#include <mc/server/commands/CommandSelector.h>
#include <mc/server/commands/CommandPermissionLevel.h>
#include <mc/server/commands/CommandOutputMessageType.h>

#include "LOICollectionA/include/CallbackUtils.h"
#include "LOICollectionA/include/server/Plugins/LanguagePlugin.h"

#include "LOICollectionA/include/server/Events/modules/WalletTransferEvent.h"
#include "LOICollectionA/include/server/Events/modules/RedEnvelopeCompletedEvent.h"

#include "LOICollectionA/coro/TimerManager.h"

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/mc-server/ScoreboardUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/data/SQLiteStorage.h"

#include "LOICollectionA/frontend/AST.h"

#include "LOICollectionA/include/form/GUIManager.h"

#include "LOICollectionA/base/ScopeGuard.h"
#include "LOICollectionA/base/Wrapper.h"
#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/WalletPlugin.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    constexpr const char* WALLET_FEE_TABLE = "WalletFee";
    constexpr const char* WALLET_FEE_COLUMN = "amount";

    constexpr const char* WALLET_LEDGER_TABLE = "WalletLedger";

    constexpr const char* WALLET_BANK_TABLE = "WalletBank";
    constexpr const char* WALLET_BANK_PRINCIPAL = "principal";
    constexpr const char* WALLET_BANK_DEPOSIT_AT = "deposit_at";

    inline std::string walletLimitMessage(const std::string& locale, const std::error_code& error) {
        switch (static_cast<WalletPluginErrorCode>(error.value())) {
            case WalletPluginErrorCode::BelowMinimum: return tr(locale, "wallet.limit.min");
            case WalletPluginErrorCode::DailyLimitExceeded: return tr(locale, "wallet.limit.daily");
            case WalletPluginErrorCode::CooldownActive: return tr(locale, "wallet.limit.cooldown");
            case WalletPluginErrorCode::ConfirmRequired: return tr(locale, "wallet.limit.confirm");
            case WalletPluginErrorCode::BankEmpty: return tr(locale, "wallet.bank.empty");
            case WalletPluginErrorCode::BelowMinDeposit: return tr(locale, "wallet.bank.min.deposit");
            case WalletPluginErrorCode::RedEnvelopeCountExceeded: return tr(locale, "wallet.redenvelope.max.count");
            case WalletPluginErrorCode::NotInTargetList: return tr(locale, "wallet.redenvelope.not.target");
            default: return error.message();
        }
    }

    struct WalletPlugin::WealthEntry {
        std::string uuid;
        std::string name;
        long long balance;
    };

    struct WalletPlugin::RedEnvelopeEntry {
        std::string id;
        std::string chatKey;
        std::string senderUuid;
        std::string senderName;

        int count;
        long long expireAt;

        std::string kingUuid;
        std::string kingName;
        int kingAmount;

        int total;
    };

    struct WalletPlugin::operation {
        CommandSelector<Player> Target;
        int Score = 0;
    };

    struct WalletPlugin::operationQuery {
        std::string PlayerName;
    };

    struct WalletPlugin::operationQueryId {
        std::string EnvelopeId;
    };

    struct WalletPlugin::Impl {
        std::shared_ptr<TimerManager> mTimerManager;

        ll::ConcurrentDenseMap<std::string, std::vector<RedEnvelopeEntry>> mRedEnvelopes;
        ll::ConcurrentDenseMap<std::string, bool> mSettling;
        ll::ConcurrentDenseMap<std::string, int64_t> mLastTransferTime;

        std::atomic<uint64_t> mLedgerSeq{ 0 };
        std::atomic<bool> mRegistered{ false };

        Config::C_Wallet options;
        
        std::shared_ptr<SQLiteStorage> db;
        std::shared_ptr<ll::io::Logger> logger;

        std::string mGuiPath;

        ll::event::ListenerPtr PlayerJoinEventListener;
        ll::event::ListenerPtr PlayerChatEventListener;
        ll::event::ListenerPtr PlayerDisconnectEventListener;
        ll::event::ListenerPtr WalletTransferEventListener;
        ll::event::ListenerPtr RedEnvelopeCompletedEventListener;

        mutable std::mutex mRankMutex;
        std::vector<WealthEntry> mWealthRank;
        std::unordered_map<std::string, size_t> mRankOf;

        Impl() : mTimerManager(std::make_shared<TimerManager>(ll::thread::ServerThreadExecutor::getDefault())) {}
    };

    WalletPlugin::WalletPlugin() : mImpl(std::make_unique<Impl>()) {};
    WalletPlugin::~WalletPlugin() = default;

    std::shared_ptr<WalletPlugin> WalletPlugin::getShared() {
        static auto instance = std::shared_ptr<WalletPlugin>(new WalletPlugin());
        return instance;
    }

    std::error_code WalletPlugin::makeErrorCode(WalletPluginErrorCode e) {
        static WalletPluginErrorCategory cat;
        return std::error_code{ static_cast<int>(e), cat };
    }

    std::shared_ptr<ll::io::Logger> WalletPlugin::getLogger() {
        return this->mImpl->logger;
    }

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

    void WalletPlugin::listenEvent() {
        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        this->mImpl->PlayerJoinEventListener = eventBus.emplaceListener<ll::event::PlayerJoinEvent>([this](ll::event::PlayerJoinEvent& event) mutable -> void {
            if (event.self().isSimulatedPlayer())
                return;

            std::string uuid = event.self().getUuid().asString();

            this->mImpl->db->has("Wallet", uuid)
                .and_then([this, uuid, name = event.self().getRealName()](bool exists) -> ll::Expected<void> {
                    if (!exists) {
                        std::unordered_map<std::string, std::string> data = {
                            { "name", name },
                            { "score", "0" },
                            { "balance", "0" }
                        };

                        return this->mImpl->db->set("Wallet", uuid, data);
                    }

                    return {};
                })
                .or_else(modules::defaultErrorHandler<WalletPlugin>);

            this->mImpl->db->get("Wallet", uuid, "score", "0")
                .and_then([this, uuid, &event](const std::string& value) -> ll::Expected<void> {
                    int score = SystemUtils::toInt(value, 0);
                    if (score <= 0) {
                        this->updateBalanceSnapshot(uuid, ScoreboardUtils::getScore(event.self(), this->mImpl->options.TargetScoreboard))
                            .or_else(modules::defaultErrorHandler<WalletPlugin>);

                        return {};
                    }

                    if (this->mImpl->mSettling.contains(uuid))
                        return {};

                    this->mImpl->mSettling[uuid] = true;
                    auto guard = make_scope_guard([this, uuid]() -> void {
                        this->mImpl->mSettling.erase(uuid);
                    });

                    return this->mImpl->db->set("Wallet", uuid, "score", "0")
                        .transform([this, uuid, score, &event]() -> void {
                            ScoreboardUtils::addScore(event.self(), this->mImpl->options.TargetScoreboard, score);

                            this->updateBalanceSnapshot(uuid, ScoreboardUtils::getScore(event.self(), this->mImpl->options.TargetScoreboard))
                                .or_else(modules::defaultErrorHandler<WalletPlugin>);
                        });
                })
                .or_else(modules::defaultErrorHandler<WalletPlugin>);
        });
        this->mImpl->PlayerChatEventListener = eventBus.emplaceListener<ll::event::PlayerChatEvent>([this](ll::event::PlayerChatEvent& event) mutable -> void {
            if (event.self().isSimulatedPlayer())
                return;

            this->tryGrabRedEnvelope(event.self(), event.message())
                .or_else([&event](ll::Error e) -> ll::Expected<void> {
                    if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == makeErrorCode(WalletPluginErrorCode::NotInTargetList)) {
                        return LanguagePlugin::getShared()->getLanguage(event.self())
                            .and_then([&event](const std::string& language) -> ll::Expected<void> {
                                event.self().sendMessage(tr(language, "wallet.redenvelope.not.target"));

                                return {};
                            });
                    }

                    return ll::Unexpected(e);
                })
                .or_else(modules::defaultErrorHandler<WalletPlugin>);
        }, ll::event::EventPriority::High);
        this->mImpl->PlayerDisconnectEventListener = eventBus.emplaceListener<ll::event::PlayerDisconnectEvent>([this](ll::event::PlayerDisconnectEvent& event) mutable -> void {
            if (event.self().isSimulatedPlayer())
                return;

            this->updateBalanceSnapshot(event.self().getUuid().asString(), ScoreboardUtils::getScore(event.self(), this->mImpl->options.TargetScoreboard))
                .or_else(modules::defaultErrorHandler<WalletPlugin>);
        });
        this->mImpl->WalletTransferEventListener = eventBus.emplaceListener<LOICollection::server::Events::WalletTransferEvent>([this](LOICollection::server::Events::WalletTransferEvent& event) mutable -> void {
            this->mImpl->logger->info(fmt::runtime(tr({}, "wallet.event.transfer")),
                event.getType(), event.getFromName(), event.getToName(), event.getAmount(), event.getFee());
        });
        this->mImpl->RedEnvelopeCompletedEventListener = eventBus.emplaceListener<LOICollection::server::Events::RedEnvelopeCompletedEvent>([this](LOICollection::server::Events::RedEnvelopeCompletedEvent& event) mutable -> void {
            this->mImpl->logger->info(fmt::runtime(tr({}, "wallet.event.envelope")),
                event.getId(), event.getKingName(), event.getKingAmount(), event.getTotal());
        });
    }

    void WalletPlugin::unlistenEvent() {
        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        eventBus.removeListener(this->mImpl->PlayerJoinEventListener);
        eventBus.removeListener(this->mImpl->PlayerChatEventListener);
        eventBus.removeListener(this->mImpl->PlayerDisconnectEventListener);
        eventBus.removeListener(this->mImpl->WalletTransferEventListener);
        eventBus.removeListener(this->mImpl->RedEnvelopeCompletedEventListener);

        this->mImpl->mTimerManager->cancelAll();
    }

    ll::Expected<std::string> WalletPlugin::getPlayerInfo(const std::string& uuid) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->db->get("Wallet", uuid, "name", "Unknown");
    }

    ll::Expected<std::vector<std::pair<std::string, std::string>>> WalletPlugin::getPlayerInfo() {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->db->list("Wallet")
            .and_then([this](const std::vector<std::string>& ids) -> ll::Expected<std::unordered_map<std::string, std::unordered_map<std::string, std::string>>> {
                return this->mImpl->db->get("Wallet", ids);
            })
            .transform([](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> mData) -> std::vector<std::pair<std::string, std::string>> {
                std::vector<std::pair<std::string, std::string>> result;

                result.reserve(mData.size());
                for (auto& [id, data] : mData)
                    result.emplace_back(id, data.at("name"));

                return result;
            });
    }

    ll::Expected<bool> WalletPlugin::forTransfer(Player& player, const std::string& target, const std::string& name, int score, bool confirmed) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        if (this->mImpl->options.TransferConfirmThreshold > 0 && score > this->mImpl->options.TransferConfirmThreshold && !confirmed)
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::ConfirmRequired));

        if (auto verification = this->validateTransfer(player.getUuid().asString(), score); !verification.has_value())
            return ll::Unexpected(verification.error());

        std::string mScoreboard = this->mImpl->options.TargetScoreboard;
        if (ScoreboardUtils::getScore(player, mScoreboard) < score || score <= 0)
            return false;

        ScoreboardUtils::reduceScore(player, mScoreboard, score);

        int mTargetMoney = static_cast<int>(score * (1 - this->mImpl->options.ExchangeRate));

        return this->transfer(target, mTargetMoney)
            .transform([this, name, score, mTargetMoney, uuid = player.getUuid().asString(), playerName = player.getRealName()]() -> bool {
                this->getLogger()->info(fmt::runtime(tr({}, "wallet.log")), playerName, name, score);

                this->updateTransferCooldown(uuid);

                long long fee = static_cast<long long>(score) - mTargetMoney;
                if (fee > 0)
                    this->accumulateFee(fee).or_else(modules::defaultErrorHandler<WalletPlugin>);

                this->appendLedger(uuid, playerName, target, name, mTargetMoney, fee, "transfer")
                    .or_else(modules::defaultErrorHandler<WalletPlugin>);

                this->emitWalletTransfer(uuid, playerName, target, name, mTargetMoney, fee, "transfer");

                return true;
            });
    }

    ll::Expected<void> WalletPlugin::validateTransfer(const std::string& uuid, int spend) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        const auto& options = this->mImpl->options;

        if (options.TransferMinAmount > 0 && spend < options.TransferMinAmount)
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::BelowMinimum));

        if (options.TransferCooldownSeconds > 0) {
            auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            if (auto it = this->mImpl->mLastTransferTime.find(uuid); it != this->mImpl->mLastTransferTime.end() && now - it->second < options.TransferCooldownSeconds)
                return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::CooldownActive));
        }

        if (options.TransferDailyLimit > 0) {
            long long today = this->getTodayOutgoing(uuid);
            if (today + spend > options.TransferDailyLimit)
                return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::DailyLimitExceeded));
        }

        return {};
    }

    long long WalletPlugin::getTodayOutgoing(const std::string& uuid) {
        constexpr long long NS_PER_DAY = 86400LL * 1000000000LL;

        auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        long long todayStartNs = (nowNs / NS_PER_DAY) * NS_PER_DAY;

        auto ids = this->mImpl->db->find(WALLET_LEDGER_TABLE, std::vector<std::pair<std::string, std::string>>{ { "from_uuid", uuid } });
        if (!ids.has_value())
            return 0;

        long long total = 0;
        for (const auto& id : ids.value()) {
            auto row = this->mImpl->db->get(WALLET_LEDGER_TABLE, id);
            if (!row.has_value())
                continue;

            const auto& fields = row.value();
            if (!fields.contains("type") || fields.at("type") != "transfer")
                continue;
            if (!fields.contains("time_ns") || SystemUtils::toLongLong(fields.at("time_ns"), 0) < todayStartNs)
                continue;

            total += SystemUtils::toLongLong(fields.at("amount"), 0) + SystemUtils::toLongLong(fields.at("fee"), 0);
        }

        return total;
    }

    void WalletPlugin::updateTransferCooldown(const std::string& uuid) {
        if (this->mImpl->options.TransferCooldownSeconds <= 0)
            return;

        auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        this->mImpl->mLastTransferTime[uuid] = now;
    }

    ll::Expected<void> WalletPlugin::updateBalanceSnapshot(const std::string& uuid, long long balance) {
        if (!this->isValid() || uuid.empty())
            return {};

        return this->mImpl->db->set("Wallet", uuid, "balance", std::to_string(balance));
    }

    ll::Expected<void> WalletPlugin::bankDeposit(Player& player, int amount) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        if (amount <= 0 || (this->mImpl->options.WalletBankMinDeposit > 0 && amount < this->mImpl->options.WalletBankMinDeposit))
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::BelowMinDeposit));

        std::string uuid = player.getUuid().asString();
        std::string mScoreboard = this->mImpl->options.TargetScoreboard;

        if (ScoreboardUtils::getScore(player, mScoreboard) < amount)
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        ScoreboardUtils::reduceScore(player, mScoreboard, amount);

        long long nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

        auto transaction = SQLiteStorageTransaction::create(*this->mImpl->db);
        if (!transaction.has_value()) {
            ScoreboardUtils::addScore(player, mScoreboard, amount);

            return ll::Unexpected(transaction.error());
        }

        auto conn = transaction.value().connection();

        auto current = this->mImpl->db->get(conn, WALLET_BANK_TABLE, uuid);
        if (!current.has_value()) {
            ScoreboardUtils::addScore(player, mScoreboard, amount);

            return ll::Unexpected(current.error());
        }

        long long principal = current.value().contains(WALLET_BANK_PRINCIPAL)
            ? SystemUtils::toLongLong(current.value().at(WALLET_BANK_PRINCIPAL), 0)
            : 0;

        auto setBank = this->mImpl->db->set(conn, WALLET_BANK_TABLE, uuid, {
            { WALLET_BANK_PRINCIPAL, std::to_string(principal + amount) },
            { WALLET_BANK_DEPOSIT_AT, std::to_string(nowNs) },
            { "name", player.getRealName() }
        });
        if (!setBank.has_value()) {
            ScoreboardUtils::addScore(player, mScoreboard, amount);

            return ll::Unexpected(setBank.error());
        }

        auto commit = transaction.value().commit();
        if (!commit.has_value()) {
            ScoreboardUtils::addScore(player, mScoreboard, amount);

            return ll::Unexpected(commit.error());
        }

        this->appendLedger(uuid, player.getRealName(), "", "", amount, 0, "bank_deposit")
            .or_else(modules::defaultErrorHandler<WalletPlugin>);

        this->emitWalletTransfer(uuid, player.getRealName(), "", "", amount, 0, "bank_deposit");

        this->updateBalanceSnapshot(uuid, static_cast<long long>(ScoreboardUtils::getScore(player, mScoreboard)))
            .or_else(modules::defaultErrorHandler<WalletPlugin>);

        return {};
    }

    ll::Expected<long long> WalletPlugin::computeBankInterest(const std::string& uuid, long long principal, long long depositAt) {
        if (principal <= 0)
            return 0;

        long long nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        long long elapsedSeconds = std::max<long long>(0, (nowNs - depositAt) / 1000000000LL);
        long long days = elapsedSeconds / 86400LL;

        return static_cast<long long>(std::floor(
            static_cast<double>(principal) * this->mImpl->options.WalletBankDailyRate * static_cast<double>(days)
        ));
    }

    ll::Expected<void> WalletPlugin::bankWithdraw(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        std::string uuid = player.getUuid().asString();

        auto data = this->mImpl->db->get(WALLET_BANK_TABLE, uuid);
        if (!data.has_value())
            return ll::Unexpected(data.error());

        auto row = data.value();
        if (row.empty() || !row.contains(WALLET_BANK_PRINCIPAL) || SystemUtils::toLongLong(row.at(WALLET_BANK_PRINCIPAL), 0) <= 0)
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::BankEmpty));

        long long principal = SystemUtils::toLongLong(row.at(WALLET_BANK_PRINCIPAL), 0);
        long long depositAt = row.contains(WALLET_BANK_DEPOSIT_AT) ? SystemUtils::toLongLong(row.at(WALLET_BANK_DEPOSIT_AT), 0) : 0;

        auto interest = this->computeBankInterest(uuid, principal, depositAt);
        if (!interest.has_value())
            return ll::Unexpected(interest.error());

        long long paidInterest = interest.value();
        long long interestTax = 0;

        auto transaction = SQLiteStorageTransaction::create(*this->mImpl->db);
        if (!transaction.has_value())
            return ll::Unexpected(transaction.error());

        auto conn = transaction.value().connection();

        if (this->mImpl->options.WalletInterestFromPool) {
            auto pool = this->mImpl->db->get(conn, WALLET_FEE_TABLE, "total", WALLET_FEE_COLUMN, "0");
            if (!pool.has_value())
                return ll::Unexpected(pool.error());

            long long available = SystemUtils::toLongLong(pool.value(), 0);
            paidInterest = std::min(paidInterest, available);

            if (paidInterest > 0) {
                auto setPool = this->mImpl->db->set(conn, WALLET_FEE_TABLE, "total", WALLET_FEE_COLUMN, std::to_string(available - paidInterest));
                if (!setPool.has_value())
                    return ll::Unexpected(setPool.error());
            }
        } else {
            interestTax = static_cast<long long>(std::floor(
                static_cast<double>(paidInterest) * this->mImpl->options.WalletInterestTaxRate
            ));

            if (interestTax > 0) {
                auto pool = this->mImpl->db->get(conn, WALLET_FEE_TABLE, "total", WALLET_FEE_COLUMN, "0");
                if (!pool.has_value())
                    return ll::Unexpected(pool.error());

                long long available = SystemUtils::toLongLong(pool.value(), 0);

                auto setPool = this->mImpl->db->set(conn, WALLET_FEE_TABLE, "total", WALLET_FEE_COLUMN, std::to_string(available + interestTax));
                if (!setPool.has_value())
                    return ll::Unexpected(setPool.error());
            }
        }

        auto delBank = this->mImpl->db->del(conn, WALLET_BANK_TABLE, uuid);
        if (!delBank.has_value())
            return ll::Unexpected(delBank.error());

        auto commit = transaction.value().commit();
        if (!commit.has_value())
            return ll::Unexpected(commit.error());

        long long credit = principal + paidInterest;
        ScoreboardUtils::addScore(player, this->mImpl->options.TargetScoreboard, static_cast<int>(credit));

        std::string playerName = player.getRealName();

        this->appendLedger(uuid, playerName, "", "", principal, 0, "bank_withdraw")
            .or_else(modules::defaultErrorHandler<WalletPlugin>);

        if (paidInterest > 0)
            this->appendLedger("", "", uuid, playerName, paidInterest, interestTax, "bank_interest")
                .or_else(modules::defaultErrorHandler<WalletPlugin>);

        this->emitWalletTransfer(uuid, playerName, "", "", principal, 0, "bank_withdraw");

        if (paidInterest > 0)
            this->emitWalletTransfer("", "", uuid, playerName, paidInterest, interestTax, "bank_interest");

        this->updateBalanceSnapshot(uuid, static_cast<long long>(ScoreboardUtils::getScore(player, this->mImpl->options.TargetScoreboard)))
            .or_else(modules::defaultErrorHandler<WalletPlugin>);

        return {};
    }

    ll::Expected<long long> WalletPlugin::getBankPrincipal(const std::string& uuid) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->db->get(WALLET_BANK_TABLE, uuid, WALLET_BANK_PRINCIPAL, "0")
            .transform([](const std::string& value) -> long long {
                return SystemUtils::toLongLong(value, 0);
            });
    }

    ll::Expected<long long> WalletPlugin::getBankInterest(const std::string& uuid) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->db->get(WALLET_BANK_TABLE, uuid)
            .and_then([this, uuid](std::unordered_map<std::string, std::string> row) -> ll::Expected<long long> {
                if (row.empty() || !row.contains(WALLET_BANK_PRINCIPAL))
                    return 0;

                long long principal = SystemUtils::toLongLong(row.at(WALLET_BANK_PRINCIPAL), 0);
                long long depositAt = row.contains(WALLET_BANK_DEPOSIT_AT) ? SystemUtils::toLongLong(row.at(WALLET_BANK_DEPOSIT_AT), 0) : 0;

                return this->computeBankInterest(uuid, principal, depositAt);
            });
    }

    ll::Expected<std::vector<WalletPlugin::WealthEntry>> WalletPlugin::computeWealthRanking() {
        auto ids = this->mImpl->db->list("Wallet");
        if (!ids.has_value())
            return ll::Unexpected(ids.error());

        if (ids.value().empty())
            return std::vector<WealthEntry>{};

        auto rows = this->mImpl->db->get("Wallet", ids.value());
        if (!rows.has_value())
            return ll::Unexpected(rows.error());

        std::vector<WealthEntry> entries;
        entries.reserve(rows.value().size());

        std::string mScoreboard = this->mImpl->options.TargetScoreboard;

        for (const auto& [uuid, row] : rows.value()) {
            std::string name = row.contains("name") ? row.at("name") : "Unknown";

            long long balance = 0;
            if (Player* player = ll::service::getLevel()->getPlayer(mce::UUID::fromString(uuid)); player)
                balance = ScoreboardUtils::getScore(*player, mScoreboard);
            else
                balance = SystemUtils::toLongLong(row.contains("balance") ? row.at("balance") : "0", 0);

            entries.push_back({ uuid, name, balance });
        }

        std::sort(entries.begin(), entries.end(), [](const WealthEntry& left, const WealthEntry& right) -> bool {
            if (left.balance != right.balance)
                return left.balance > right.balance;

            return left.name < right.name;
        });

        return entries;
    }

    ll::Expected<void> WalletPlugin::rebuildWealthRanking() {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->computeWealthRanking()
            .transform([this](std::vector<WealthEntry> entries) -> void {
                std::lock_guard<std::mutex> lock(this->mImpl->mRankMutex);

                this->mImpl->mWealthRank = std::move(entries);

                this->mImpl->mRankOf.clear();
                this->mImpl->mRankOf.reserve(this->mImpl->mWealthRank.size());
                for (size_t i = 0; i < this->mImpl->mWealthRank.size(); ++i)
                    this->mImpl->mRankOf[this->mImpl->mWealthRank[i].uuid] = i;
            });
    }

    ll::Expected<std::vector<std::pair<std::string, long long>>> WalletPlugin::getWealthRanking(int limit) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        std::lock_guard<std::mutex> lock(this->mImpl->mRankMutex);

        std::vector<std::pair<std::string, long long>> result;
        result.reserve(this->mImpl->mWealthRank.size());

        for (const auto& entry : this->mImpl->mWealthRank)
            result.emplace_back(entry.name, entry.balance);

        if (limit > 0 && result.size() > static_cast<size_t>(limit))
            result.resize(static_cast<size_t>(limit));

        return result;
    }

    ll::Expected<std::pair<int, long long>> WalletPlugin::getWealthRank(const std::string& uuid) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        std::lock_guard<std::mutex> lock(this->mImpl->mRankMutex);

        auto it = this->mImpl->mRankOf.find(uuid);
        if (it == this->mImpl->mRankOf.end())
            return std::make_pair(-1, 0);

        const WealthEntry& entry = this->mImpl->mWealthRank[it->second];

        long long balance = entry.balance;
        if (Player* player = ll::service::getLevel()->getPlayer(mce::UUID::fromString(uuid)); player)
            balance = ScoreboardUtils::getScore(*player, this->mImpl->options.TargetScoreboard);

        return std::make_pair(static_cast<int>(it->second) + 1, balance);
    }

    void WalletPlugin::scheduleWealthRefresh() {
        if (this->mImpl->options.WealthRefreshMinutes <= 0)
            return;

        this->mImpl->mTimerManager->loopSchedule("wallet_wealth_refresh", std::chrono::minutes(this->mImpl->options.WealthRefreshMinutes), [this]() -> void {
            this->rebuildWealthRanking().or_else(modules::defaultErrorHandler<WalletPlugin>);
        });
    }

    ll::Expected<void> WalletPlugin::setExecutor(const ll::coro::Executor& executor) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        this->mImpl->mTimerManager->setExecutor(executor);

        return {};
    }

    ll::Expected<void> WalletPlugin::tryGrabRedEnvelope(Player& player, const std::string& message) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        auto it = this->mImpl->mRedEnvelopes.find(message);
        if (it == this->mImpl->mRedEnvelopes.end())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::NotFound));

        std::string uuid = player.getUuid().asString();

        for (auto& entry : it->second) {
            auto result = this->grabEnvelope(player, uuid, entry);
            if (!result.has_value())
                return ll::Unexpected(result.error());

            if (result.value())
                return {};
        }

        return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::RedEnvelopeCompleted));
    }

    int WalletPlugin::computeGiftAmount(int remainingCapacity, int remainingPeople) {
        if (remainingCapacity <= 0)
            return 0;

        if (remainingPeople <= 1)
            return remainingCapacity;

        int upper = std::min(remainingCapacity - (remainingPeople - 1), (remainingCapacity / remainingPeople) * 2);
        upper = std::max(upper, 1);

        return ll::random_utils::rand(1, upper);
    }

    ll::Expected<bool> WalletPlugin::grabEnvelope(Player& player, const std::string& uuid, RedEnvelopeEntry& entry) {
        auto transaction = SQLiteStorageTransaction::create(*this->mImpl->db);
        if (!transaction.has_value())
            return ll::Unexpected(transaction.error());

        auto conn = transaction.value().connection();

        auto data = this->mImpl->db->get(conn, "RedEnvelope", entry.id);
        if (!data.has_value())
            return ll::Unexpected(data.error());

        auto m = data.value();
        if (m.empty())
            return false;

        int capacity = SystemUtils::toInt(m["capacity"], 0);
        int count = SystemUtils::toInt(m["count"], 0);
        int people = SystemUtils::toInt(m["people"], 0);

        if (people >= count)
            return false;

        if (m.contains("targets") && !m.at("targets").empty()) {
            bool inList = false;
            std::string_view targetsView = m.at("targets");
            size_t start = 0;
            while (start <= targetsView.size()) {
                size_t comma = targetsView.find(',', start);
                size_t length = comma == std::string_view::npos ? std::string_view::npos : comma - start;
                if (!targetsView.substr(start, length).empty() && targetsView.substr(start, length) == uuid) {
                    inList = true;
                    break;
                }
                if (comma == std::string_view::npos)
                    break;
                start = comma + 1;
            }

            if (!inList)
                return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::NotInTargetList));
        }

        std::string grabKey = entry.id + ":" + uuid;
        auto grabbed = this->mImpl->db->has(conn, "RedEnvelopeGrab", grabKey);
        if (!grabbed.has_value())
            return ll::Unexpected(grabbed.error());

        if (grabbed.value())
            return false;

        int remainingPeople = count - people;
        bool last = remainingPeople == 1;
        int amount = last ? capacity : this->computeGiftAmount(capacity, remainingPeople);
        if (amount <= 0)
            return false;

        std::unordered_map<std::string, std::string> update = {
            { "capacity", std::to_string(capacity - amount) },
            { "people", std::to_string(people + 1) }
        };

        auto setEnv = this->mImpl->db->set(conn, "RedEnvelope", entry.id, update);
        if (!setEnv.has_value())
            return ll::Unexpected(setEnv.error());

        auto setGrab = this->mImpl->db->set(conn, "RedEnvelopeGrab", grabKey, {
            { "name", player.getRealName() },
            { "amount", std::to_string(amount) }
        });
        if (!setGrab.has_value())
            return ll::Unexpected(setGrab.error());

        bool nowFull = (people + 1) >= count;
        if (nowFull) {
            auto delEnv = this->mImpl->db->del(conn, "RedEnvelope", entry.id);
            if (!delEnv.has_value())
                return ll::Unexpected(delEnv.error());
        }

        auto commit = transaction.value().commit();
        if (!commit.has_value())
            return ll::Unexpected(commit.error());

        ScoreboardUtils::addScore(player, this->mImpl->options.TargetScoreboard, amount);

        this->appendLedger(entry.senderUuid, entry.senderName, uuid, player.getRealName(), amount, 0, "redenvelope_grab")
            .or_else(modules::defaultErrorHandler<WalletPlugin>);

        this->emitWalletTransfer(entry.senderUuid, entry.senderName, uuid, player.getRealName(), amount, 0, "redenvelope_grab");

        this->broadcastReceive(entry, player, amount, people + 1);

        if (amount > entry.kingAmount) {
            entry.kingAmount = amount;
            entry.kingUuid = uuid;
            entry.kingName = player.getRealName();
        }

        if (nowFull) {
            this->announceKing(entry);

            ll::event::EventBus::getInstance().publish(LOICollection::server::Events::RedEnvelopeCompletedEvent(
                entry.id,
                entry.senderUuid,
                entry.kingUuid,
                entry.kingName,
                entry.kingAmount,
                entry.total,
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count()
            ));

            auto& entries = this->mImpl->mRedEnvelopes[entry.chatKey];
            entries.erase(std::remove_if(entries.begin(), entries.end(), [&entry](const RedEnvelopeEntry& e) -> bool {
                return e.id == entry.id;
            }), entries.end());
        }

        return true;
    }

    void WalletPlugin::broadcastContent(Player& sender, const std::string& key, const std::string& id, int score, int count) {
        ll::service::getLevel()->forEachPlayer([&, score, count](Player& target) -> bool {
            LanguagePlugin::getShared()->getLanguage(target)
                .transform([&, score, count, &sender, id, key](const std::string& language) -> void {
                    std::string mMessage = LOICollectionAPI::CallbackUtils::getInstance().translate(
                        tr(language, "wallet.tips.redenvelope.content"), sender
                    );

                    TextPacket::createRawMessage(fmt::format(fmt::runtime(mMessage), 
                        id, score, count, this->mImpl->options.RedEnvelopeTimeout, key
                    )).sendTo(target);
                })
                .or_else(modules::defaultErrorHandler<WalletPlugin>);

            return true;
        });
    }

    void WalletPlugin::broadcastReceive(const RedEnvelopeEntry& entry, Player& player, int amount, int people) {
        ll::service::getLevel()->forEachPlayer([&, amount, people](Player& target) -> bool {
            LanguagePlugin::getShared()->getLanguage(target)
                .transform([&, amount, people, &player, &entry, &target](const std::string& language) -> void {
                    std::string mMessage = LOICollectionAPI::CallbackUtils::getInstance().translate(
                        tr(language, "wallet.tips.redenvelope.receive"), player
                    );

                    TextPacket::createRawMessage(fmt::format(fmt::runtime(mMessage),
                        entry.id, amount, people, entry.count
                    )).sendTo(target);
                })
                .or_else(modules::defaultErrorHandler<WalletPlugin>);

            return true;
        });
    }

    void WalletPlugin::announceKing(RedEnvelopeEntry& entry) {
        ll::service::getLevel()->forEachPlayer([&entry](Player& target) -> bool {
            LanguagePlugin::getShared()->getLanguage(target)
                .transform([&entry, &target](const std::string& language) -> void {
                    TextPacket::createRawMessage(fmt::format(fmt::runtime(
                        tr(language, "wallet.tips.redenvelope.receive.over")),
                        entry.id, entry.kingName, entry.kingAmount
                    )).sendTo(target);
                })
                .or_else(modules::defaultErrorHandler<WalletPlugin>);

            return true;
        });
    }

    ll::Expected<void> WalletPlugin::deleteEnvelope(const std::string& id) {
        auto transaction = SQLiteStorageTransaction::create(*this->mImpl->db);
        if (!transaction.has_value())
            return ll::Unexpected(transaction.error());

        auto conn = transaction.value().connection();

        auto delEnv = this->mImpl->db->del(conn, "RedEnvelope", id);
        if (!delEnv.has_value())
            return ll::Unexpected(delEnv.error());

        auto grabs = this->mImpl->db->list(conn, "RedEnvelopeGrab");
        if (!grabs.has_value())
            return ll::Unexpected(grabs.error());

        std::string prefix = id + ":";
        std::vector<std::string> keys;
        for (const auto& grabKey : grabs.value()) {
            if (grabKey.rfind(prefix, 0) == 0)
                keys.emplace_back(grabKey);
        }

        if (!keys.empty()) {
            auto delGrabs = this->mImpl->db->del(conn, "RedEnvelopeGrab", keys);
            if (!delGrabs.has_value())
                return ll::Unexpected(delGrabs.error());
        }

        auto commit = transaction.value().commit();
        if (!commit.has_value())
            return ll::Unexpected(commit.error());

        return {};
    }

    ll::Expected<bool> WalletPlugin::refundEnvelope(const std::string& id) {
        auto data = this->mImpl->db->get("RedEnvelope", id, "sender_uuid", "");
        if (!data.has_value())
            return ll::Unexpected(data.error());

        if (data.value().empty())
            return false;

        auto capacity = this->mImpl->db->get("RedEnvelope", id, "capacity", "0");
        if (!capacity.has_value())
            return ll::Unexpected(capacity.error());

        auto chatKey = this->mImpl->db->get("RedEnvelope", id, "chat_key", "");
        if (!chatKey.has_value())
            return ll::Unexpected(chatKey.error());

        auto senderName = this->mImpl->db->get("RedEnvelope", id, "sender_name", "");
        if (!senderName.has_value())
            return ll::Unexpected(senderName.error());

        int remaining = SystemUtils::toInt(capacity.value(), 0);
        if (remaining > 0) {
            auto refund = this->transfer(data.value(), remaining);
            if (!refund.has_value())
                return ll::Unexpected(refund.error());

            this->appendLedger("", "", data.value(), senderName.value(), remaining, 0, "redenvelope_refund")
                .or_else(modules::defaultErrorHandler<WalletPlugin>);

            this->emitWalletTransfer("", "", data.value(), senderName.value(), remaining, 0, "redenvelope_refund");
        }

        auto del = this->deleteEnvelope(id);
        if (!del.has_value())
            return ll::Unexpected(del.error());

        auto it = this->mImpl->mRedEnvelopes.find(chatKey.value());
        if (it != this->mImpl->mRedEnvelopes.end()) {
            it->second.erase(std::remove_if(it->second.begin(), it->second.end(), [&id](const RedEnvelopeEntry& e) -> bool {
                return e.id == id;
            }), it->second.end());
        }

        return true;
    }

    ll::Expected<void> WalletPlugin::sweepExpiredEnvelopes() {
        auto ids = this->mImpl->db->list("RedEnvelope");
        if (!ids.has_value())
            return ll::Unexpected(ids.error());

        for (const auto& id : ids.value()) {
            auto data = this->mImpl->db->get("RedEnvelope", id);
            if (!data.has_value())
                return ll::Unexpected(data.error());

            auto m = data.value();
            if (m.empty())
                continue;

            int people = SystemUtils::toInt(m["people"], 0);
            int count = SystemUtils::toInt(m["count"], 0);
            long long expireAt = SystemUtils::toLongLong(m["expire_at"], 0);

            long long now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();

            bool completed = people >= count;
            bool expired = now > expireAt;

            if (completed || expired) {
                auto refund = this->refundEnvelope(id);
                if (!refund.has_value())
                    return ll::Unexpected(refund.error());

                continue;
            }

            auto chatKey = m["chat_key"];
            long long remain = expireAt - now;
            int total = m.contains("total") ? SystemUtils::toInt(m["total"], 0) : SystemUtils::toInt(m["capacity"], 0);

            this->mImpl->mRedEnvelopes[chatKey].push_back({
                id,
                chatKey,
                m["sender_uuid"],
                m["sender_name"],
                count,
                expireAt,
                "",
                "",
                0,
                total
            });

            this->mImpl->mTimerManager->schedule(id, std::chrono::nanoseconds(remain), [this, id]() -> void {
                auto refund = this->refundEnvelope(id);
                if (!refund.has_value()) {
                    modules::defaultErrorHandler<WalletPlugin>(refund.error());
                    return;
                }

                if (!refund.value())
                    return;

                ll::service::getLevel()->forEachPlayer([id](Player& target) -> bool {
                    LanguagePlugin::getShared()->getLanguage(target)
                        .transform([id, &target](const std::string& language) -> void {
                            TextPacket::createRawMessage(
                                fmt::format(fmt::runtime(tr(language, "wallet.tips.redenvelope.timeout")), id)
                            ).sendTo(target);
                        })
                        .or_else(modules::defaultErrorHandler<WalletPlugin>);

                    return true;
                });
            });
        }

        return {};
    }

    ll::Expected<void> WalletPlugin::transfer(const std::string& target, int score) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        if (Player* mObject = ll::service::getLevel()->getPlayer(mce::UUID::fromString(target)); mObject) {
            ScoreboardUtils::addScore(*mObject, this->mImpl->options.TargetScoreboard, score);
            
            return {};
        }

        return this->mImpl->db->get("Wallet", target, "score", "0")
            .and_then([this, score, target](const std::string& value) -> ll::Expected<void> {
                int walletScore = SystemUtils::toInt(value);

                return this->mImpl->db->set("Wallet", target, "score", std::to_string(walletScore + score))
                    .transform([this, target, walletScore, score]() -> void {
                        this->updateBalanceSnapshot(target, static_cast<long long>(walletScore) + score)
                            .or_else(modules::defaultErrorHandler<WalletPlugin>);
                    });
            });
    }

    ll::Expected<void> WalletPlugin::wealth(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        int score = ScoreboardUtils::getScore(player, this->mImpl->options.TargetScoreboard);

        ll::service::getLevel()->forEachPlayer([score, &player](Player& target) -> bool {
            LanguagePlugin::getShared()->getLanguage(target)
                .transform([score, &player, &target](const std::string& language) -> void {
                    std::string mMessage = LOICollectionAPI::CallbackUtils::getInstance().translate(
                        tr(language, "wallet.showOff"), player
                    );

                    TextPacket::createRawMessage(
                        fmt::format(fmt::runtime(mMessage), score)
                    ).sendTo(target);
                })
                .or_else(modules::defaultErrorHandler<WalletPlugin>);

            return true;
        });

        return {};
    }

    ll::Expected<void> WalletPlugin::redenvelope(Player& player, const std::string& key, int score, int count, const std::vector<std::string>& targets) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        if (count <= 0 || score <= 0)
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        if (this->mImpl->options.RedEnvelopeMaxCount > 0 && count > this->mImpl->options.RedEnvelopeMaxCount)
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::RedEnvelopeCountExceeded));

        std::string uuid = player.getUuid().asString();

        int total = score * count;
        if (ScoreboardUtils::getScore(player, this->getTargetScoreboard()) < total)
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        auto targetUuids = this->resolveTargetUuids(targets);
        if (!targetUuids.has_value())
            return ll::Unexpected(targetUuids.error());

        std::string targetsValue;
        if (this->mImpl->options.RedEnvelopeTargetedEnabled && !targetUuids.value().empty()) {
            for (const auto& targetUuid : targetUuids.value()) {
                if (!targetsValue.empty())
                    targetsValue += ",";
                targetsValue += targetUuid;
            }
        }

        ScoreboardUtils::reduceScore(player, this->getTargetScoreboard(), total);

        std::string id = SystemUtils::getCurrentTimestamp();

        long long expire = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count() + static_cast<long long>(this->mImpl->options.RedEnvelopeTimeout) * 1000000000LL;

        std::unordered_map<std::string, std::string> env = {
            { "chat_key", key },
            { "sender_uuid", uuid },
            { "sender_name", player.getRealName() },
            { "capacity", std::to_string(total) },
            { "total", std::to_string(total) },
            { "count", std::to_string(count) },
            { "people", "0" },
            { "created_at", SystemUtils::getNowTime() },
            { "expire_at", std::to_string(expire) }
        };
        if (!targetsValue.empty())
            env["targets"] = targetsValue;

        auto transaction = SQLiteStorageTransaction::create(*this->mImpl->db);
        if (!transaction.has_value()) {
            ScoreboardUtils::addScore(player, this->getTargetScoreboard(), total);

            return ll::Unexpected(transaction.error());
        }

        auto conn = transaction.value().connection();
        auto setEnv = this->mImpl->db->set(conn, "RedEnvelope", id, env);
        if (!setEnv.has_value()) {
            ScoreboardUtils::addScore(player, this->getTargetScoreboard(), total);

            return ll::Unexpected(setEnv.error());
        }

        auto commit = transaction.value().commit();
        if (!commit.has_value()) {
            ScoreboardUtils::addScore(player, this->getTargetScoreboard(), total);

            return ll::Unexpected(commit.error());
        }

        this->mImpl->mRedEnvelopes[key].push_back({
            id,
            key,
            uuid,
            player.getRealName(),
            count,
            expire,
            "",
            "",
            0,
            total
        });

        this->appendLedger(uuid, player.getRealName(), "", "", total, 0, "redenvelope_send")
            .or_else(modules::defaultErrorHandler<WalletPlugin>);

        this->emitWalletTransfer(uuid, player.getRealName(), "", "", total, 0, "redenvelope_send");

        this->mImpl->mTimerManager->schedule(id, std::chrono::seconds(this->mImpl->options.RedEnvelopeTimeout), [this, id]() -> void {
            auto refund = this->refundEnvelope(id);
            if (!refund.has_value()) {
                modules::defaultErrorHandler<WalletPlugin>(refund.error());
                return;
            }

            if (!refund.value())
                return;

            ll::service::getLevel()->forEachPlayer([id](Player& target) -> bool {
                LanguagePlugin::getShared()->getLanguage(target)
                    .transform([id, &target](const std::string& language) -> void {
                        TextPacket::createRawMessage(
                            fmt::format(fmt::runtime(tr(language, "wallet.tips.redenvelope.timeout")), id)
                        ).sendTo(target);
                    })
                    .or_else(modules::defaultErrorHandler<WalletPlugin>);

                return true;
            });
        });

        this->broadcastContent(player, key, id, score, count);

        return {};
    }

    bool WalletPlugin::isValid() {
        return this->getLogger() != nullptr && this->mImpl->db != nullptr;
    }

    std::string WalletPlugin::getTargetScoreboard() {
        return this->mImpl->options.TargetScoreboard;
    }

    double WalletPlugin::getExchangeRate() {
        return this->mImpl->options.ExchangeRate;
    }

    void WalletPlugin::setOptionsForTest(const Config::C_Wallet& options) {
        this->mImpl->options = options;
    }

    std::string WalletPlugin::getName() {
        return "WalletPlugin";
    }

    modules::ModulePriority WalletPlugin::getPriority() {
        return modules::ModulePriority::High;
    }

    ll::Expected<bool> WalletPlugin::load() {
        if (!ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Wallet.ModuleEnabled)
            return false;

        this->mImpl->db = ServiceProvider::getInstance().getService<SQLiteStorage>("SettingsDB");
        this->mImpl->logger = ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA");
        this->mImpl->options = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Wallet;
        this->mImpl->mGuiPath = (std::filesystem::path(ServiceProvider::getInstance().getService<std::string>("GuiPath")->data()) / "wallet.lcui").string();

        return true;
    }

    ll::Expected<bool> WalletPlugin::unload() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        this->mImpl->db.reset();
        this->mImpl->logger.reset();
        this->mImpl->options = {};

        if (this->mImpl->mRegistered.load(std::memory_order_acquire))
            this->unlistenEvent();

        return true;
    }

    ll::Expected<bool> WalletPlugin::registry() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        return this->mImpl->db->create("Wallet", [](SQLiteStorage::ColumnCallback ctor) -> void {
            ctor("name");
            ctor("score");
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->db->create("RedEnvelope", [](SQLiteStorage::ColumnCallback ctor) -> void {
                ctor("chat_key");
                ctor("sender_uuid");
                ctor("sender_name");
                ctor("capacity");
                ctor("count");
                ctor("people");
                ctor("created_at");
                ctor("expire_at");
            });
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->db->create("RedEnvelopeGrab", [](SQLiteStorage::ColumnCallback ctor) -> void {
                ctor("name");
                ctor("amount");
            });
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->db->create(WALLET_FEE_TABLE, [](SQLiteStorage::ColumnCallback ctor) -> void {
                ctor(WALLET_FEE_COLUMN);
            });
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->db->create(WALLET_LEDGER_TABLE, [](SQLiteStorage::ColumnCallback ctor) -> void {
                ctor("from_uuid");
                ctor("from_name");
                ctor("to_uuid");
                ctor("to_name");
                ctor("amount");
                ctor("fee");
                ctor("type");
                ctor("time_ns");
                ctor("time");
            });
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->db->create(WALLET_BANK_TABLE, [](SQLiteStorage::ColumnCallback ctor) -> void {
                ctor(WALLET_BANK_PRINCIPAL);
                ctor(WALLET_BANK_DEPOSIT_AT);
                ctor("name");
            });
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->db->exec("CREATE INDEX IF NOT EXISTS idx_WalletLedger_time_ns ON WalletLedger(time_ns);")
                .and_then([this]() -> ll::Expected<void> {
                    return this->mImpl->db->exec("CREATE INDEX IF NOT EXISTS idx_WalletLedger_from_uuid ON WalletLedger(from_uuid);");
                })
                .and_then([this]() -> ll::Expected<void> {
                    return this->mImpl->db->exec("CREATE INDEX IF NOT EXISTS idx_WalletLedger_to_uuid ON WalletLedger(to_uuid);");
                });
        }).and_then([this]() -> ll::Expected<void> {
            return this->sweepExpiredEnvelopes();
        }).and_then([this]() -> ll::Expected<void> {
            return this->registeryUI();
        }).transform([this]() -> bool {
            this->registeryCommand();
            this->listenEvent();

            if (this->mImpl->options.WalletHistoryRetentionDays > 0)
                this->scheduleLedgerCleanup();

            this->rebuildWealthRanking().or_else(modules::defaultErrorHandler<WalletPlugin>);
            this->scheduleWealthRefresh();

            this->mImpl->mRegistered.store(true, std::memory_order_release);

            return true;
        });
    }

    ll::Expected<bool> WalletPlugin::unregistry() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        this->unlistenEvent();

        return this->mImpl->db->list("RedEnvelope")
            .and_then([this](const std::vector<std::string>& ids) -> ll::Expected<void> {
                for (const auto& id : ids) {
                    auto result = this->refundEnvelope(id);
                    if (!result.has_value())
                        return ll::Unexpected(result.error());
                }

                return {};
            })
            .transform([this]() -> bool {
                this->mImpl->mRedEnvelopes.clear();

                this->mImpl->mRegistered.store(false, std::memory_order_release);

                return true;
            });
    }

    ll::Expected<long long> WalletPlugin::getFeePool() {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->db->get(WALLET_FEE_TABLE, "total", WALLET_FEE_COLUMN, "0")
            .transform([](const std::string& value) -> long long {
                return SystemUtils::toLongLong(value, 0);
            });
    }

    ll::Expected<void> WalletPlugin::accumulateFee(long long amount) {
        if (amount <= 0)
            return {};

        return this->mImpl->db->get(WALLET_FEE_TABLE, "total", WALLET_FEE_COLUMN, "0")
            .and_then([this, amount](const std::string& value) -> ll::Expected<void> {
                long long total = SystemUtils::toLongLong(value, 0);

                return this->mImpl->db->set(WALLET_FEE_TABLE, "total", WALLET_FEE_COLUMN, std::to_string(total + amount));
            });
    }

    void WalletPlugin::emitWalletTransfer(const std::string& fromUuid, const std::string& fromName, const std::string& toUuid, const std::string& toName, long long amount, long long fee, const std::string& type) {
        ll::event::EventBus::getInstance().publish(LOICollection::server::Events::WalletTransferEvent(
            fromUuid,
            fromName,
            toUuid,
            toName,
            amount,
            fee,
            type,
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        ));
    }

    ll::Expected<std::vector<std::string>> WalletPlugin::resolveTargetUuids(const std::vector<std::string>& names) {
        std::vector<std::string> uuids;
        if (names.empty())
            return uuids;

        std::unordered_map<std::string, std::string> nameToUuid;

        ll::service::getLevel()->forEachPlayer([&nameToUuid](Player& target) -> bool {
            nameToUuid[target.getRealName()] = target.getUuid().asString();

            return true;
        });

        auto players = this->getPlayerInfo();
        if (players.has_value()) {
            for (const auto& [uuid, name] : players.value())
                nameToUuid[name] = uuid;
        }

        for (const auto& name : names) {
            auto it = nameToUuid.find(name);
            if (it != nameToUuid.end())
                uuids.emplace_back(it->second);
        }

        return uuids;
    }

    ll::Expected<std::vector<std::string>> WalletPlugin::getEnvelopeStats(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->db->list("RedEnvelopeGrab")
            .and_then([this, id](const std::vector<std::string>& keys) -> ll::Expected<std::vector<std::string>> {
                std::string prefix = id + ":";
                std::vector<std::string> grabKeys;
                for (const auto& key : keys)
                    if (key.rfind(prefix, 0) == 0)
                        grabKeys.emplace_back(key);

                if (grabKeys.empty())
                    return std::vector<std::string>{};

                return this->mImpl->db->get("RedEnvelopeGrab", grabKeys)
                    .transform([this, id](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> rows) -> std::vector<std::string> {
                        std::vector<std::pair<std::string, long long>> grabs;
                        for (const auto& [key, row] : rows) {
                            std::string name = row.contains("name") ? row.at("name") : "?";
                            long long amount = row.contains("amount") ? SystemUtils::toLongLong(row.at("amount"), 0) : 0;

                            grabs.emplace_back(name, amount);
                        }

                        std::sort(grabs.begin(), grabs.end(), [](const auto& a, const auto& b) {
                            return a.second > b.second;
                        });

                        std::vector<std::string> result;
                        result.emplace_back(fmt::format(fmt::runtime(tr({}, "wallet.rinfo.header")), id));

                        for (const auto& [name, amount] : grabs)
                            result.emplace_back(fmt::format(fmt::runtime(tr({}, "wallet.rinfo.row")), name, amount));

                        if (!grabs.empty()) {
                            const auto& [kingName, kingAmount] = grabs.front();
                            result.emplace_back(fmt::format(fmt::runtime(tr({}, "wallet.rinfo.king")), kingName, kingAmount));
                        }

                        return result;
                    });
            });
    }

    ll::Expected<std::vector<std::string>> WalletPlugin::getRedEnvelopeDailyStats() {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        constexpr long long NS_PER_DAY = 86400LL * 1000000000LL;

        long long nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        long long todayStartNs = (nowNs / NS_PER_DAY) * NS_PER_DAY;

        auto sendIds = this->mImpl->db->find(WALLET_LEDGER_TABLE, {
            { "type", "redenvelope_send" }
        });
        if (!sendIds.has_value())
            return ll::Unexpected(sendIds.error());

        long long sendCount = 0;
        long long sendTotal = 0;
        std::unordered_map<std::string, long long> senderTotal;

        for (const auto& id : sendIds.value()) {
            auto row = this->mImpl->db->get(WALLET_LEDGER_TABLE, id);
            if (!row.has_value())
                continue;

            const auto& fields = row.value();
            if (!fields.contains("time_ns") || SystemUtils::toLongLong(fields.at("time_ns"), 0) < todayStartNs)
                continue;

            long long amount = SystemUtils::toLongLong(fields.contains("amount") ? fields.at("amount") : "0", 0);
            sendCount += 1;
            sendTotal += amount;

            std::string sender = fields.contains("from_name") ? fields.at("from_name") : "?";
            senderTotal[sender] += amount;
        }

        auto grabIds = this->mImpl->db->find(WALLET_LEDGER_TABLE, {
            { "type", "redenvelope_grab" }
        });
        if (!grabIds.has_value())
            return ll::Unexpected(grabIds.error());

        std::unordered_map<std::string, long long> grabTotal;
        for (const auto& id : grabIds.value()) {
            auto row = this->mImpl->db->get(WALLET_LEDGER_TABLE, id);
            if (!row.has_value())
                continue;

            const auto& fields = row.value();
            if (!fields.contains("time_ns") || SystemUtils::toLongLong(fields.at("time_ns"), 0) < todayStartNs)
                continue;

            std::string grabber = fields.contains("to_name") ? fields.at("to_name") : "?";
            grabTotal[grabber] += SystemUtils::toLongLong(fields.contains("amount") ? fields.at("amount") : "0", 0);
        }

        std::vector<std::pair<std::string, long long>> topGrabbers(grabTotal.begin(), grabTotal.end());
        std::sort(topGrabbers.begin(), topGrabbers.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });
        if (topGrabbers.size() > 5)
            topGrabbers.resize(5);

        std::string topSender;
        long long topSenderAmount = 0;
        for (const auto& [name, amount] : senderTotal)
            if (amount > topSenderAmount) {
                topSenderAmount = amount;
                topSender = name;
            }

        std::vector<std::string> result;
        result.emplace_back(tr({}, "wallet.rstat.header"));
        result.emplace_back(fmt::format(fmt::runtime(tr({}, "wallet.rstat.count")), sendCount));
        result.emplace_back(fmt::format(fmt::runtime(tr({}, "wallet.rstat.total")), sendTotal));
        result.emplace_back(fmt::format(fmt::runtime(tr({}, "wallet.rstat.generous")), topSender, topSenderAmount));

        for (size_t i = 0; i < topGrabbers.size(); ++i)
            result.emplace_back(fmt::format(fmt::runtime(tr({}, "wallet.rstat.grabber")),
                i + 1, topGrabbers.at(i).first, topGrabbers.at(i).second));

        return result;
    }

    ll::Expected<void> WalletPlugin::appendLedger(const std::string& fromUuid, const std::string& fromName, const std::string& toUuid, const std::string& toName, long long amount, long long fee, const std::string& type) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        if (!this->mImpl->options.WalletHistoryEnabled)
            return {};

        long long nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        std::string id = std::to_string(nowNs) + "_" + std::to_string(this->mImpl->mLedgerSeq.fetch_add(1, std::memory_order_relaxed));

        std::unordered_map<std::string, std::string> row = {
            { "from_uuid", fromUuid },
            { "from_name", fromName },
            { "to_uuid", toUuid },
            { "to_name", toName },
            { "amount", std::to_string(amount) },
            { "fee", std::to_string(fee) },
            { "type", type },
            { "time_ns", std::to_string(nowNs) },
            { "time", SystemUtils::getNowTime() }
        };

        return this->mImpl->db->set(WALLET_LEDGER_TABLE, id, row);
    }

    ll::Expected<std::vector<std::string>> WalletPlugin::getPlayerLedger(const std::string& uuid, int limit) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->db->find(WALLET_LEDGER_TABLE, {
            { "from_uuid", uuid },
            { "to_uuid", uuid }
        }, SQLiteStorage::FindCondition::OR)
            .and_then([this, uuid, limit](const std::vector<std::string>& ids) -> ll::Expected<std::vector<std::string>> {
                if (ids.empty())
                    return std::vector<std::string>{};

                return this->mImpl->db->get(WALLET_LEDGER_TABLE, ids)
                    .transform([this, uuid, limit](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> rows) -> std::vector<std::string> {
                        std::vector<std::pair<std::string, std::string>> sorted;
                        sorted.reserve(rows.size());

                        for (const auto& [id, row] : rows) {
                            std::string type = row.contains("type") ? row.at("type") : "";
                            std::string fromName = row.contains("from_name") ? row.at("from_name") : "";
                            std::string toName = row.contains("to_name") ? row.at("to_name") : "";
                            std::string amount = row.contains("amount") ? row.at("amount") : "0";
                            std::string fee = row.contains("fee") ? row.at("fee") : "0";
                            std::string timeNs = row.contains("time_ns") ? row.at("time_ns") : "";
                            std::string timeStr = row.contains("time") ? row.at("time") : "";

                            bool isOut = row.contains("from_uuid") && row.at("from_uuid") == uuid;
                            std::string direction = tr({}, isOut ? "wallet.history.type.out" : "wallet.history.type.in");

                            std::string display = fmt::format(fmt::runtime(tr({}, "wallet.history.row")),
                                timeStr, direction, fromName, toName, amount, fee);

                            sorted.emplace_back(timeNs, display);
                        }

                        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
                            return a.first > b.first;
                        });

                        if (limit > 0 && sorted.size() > static_cast<size_t>(limit))
                            sorted.resize(static_cast<size_t>(limit));

                        std::vector<std::string> result;
                        result.reserve(sorted.size());
                        for (const auto& [t, d] : sorted)
                            result.emplace_back(d);

                        return result;
                    });
            });
    }

    void WalletPlugin::scheduleLedgerCleanup() {
        this->mImpl->mTimerManager->schedule("wallet_ledger_cleanup", std::chrono::hours(24), [this]() -> void {
            this->cleanupLedger();
        });
    }

    void WalletPlugin::cleanupLedger() {
        long long cutoff = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()
            - static_cast<long long>(this->mImpl->options.WalletHistoryRetentionDays) * 86400LL * 1000000000LL;

        this->mImpl->db->exec(fmt::format("DELETE FROM {} WHERE time_ns < {}", WALLET_LEDGER_TABLE, cutoff))
            .or_else(modules::defaultErrorHandler<WalletPlugin>);

        this->scheduleLedgerCleanup();
    }

    ll::Expected<void> WalletPlugin::sendHistory(Player& receiver, const std::string& uuid, const std::string& name, int limit) {
        return this->getPlayerLedger(uuid, limit)
            .and_then([&receiver, name](const std::vector<std::string>& lines) -> ll::Expected<void> {
                if (lines.empty()) {
                    receiver.sendMessage(tr({}, "wallet.history.empty"));

                    return {};
                }

                receiver.sendMessage(fmt::format(fmt::runtime(tr({}, "wallet.history.header")), name));

                for (const auto& line : lines)
                    receiver.sendMessage(line);

                return {};
            });
    }
}
