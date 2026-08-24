#include <atomic>
#include <cmath>
#include <chrono>
#include <memory>
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
    };

    struct WalletPlugin::operation {
        CommandSelector<Player> Target;
        int Score = 0;
    };

    struct WalletPlugin::operationQuery {
        std::string PlayerName;
    };

    struct WalletPlugin::Impl {
        std::shared_ptr<TimerManager> mTimerManager;

        ll::ConcurrentDenseMap<std::string, std::vector<RedEnvelopeEntry>> mRedEnvelopes;
        ll::ConcurrentDenseMap<std::string, bool> mSettling;

        std::atomic<uint64_t> mLedgerSeq{ 0 };
        std::atomic<bool> mRegistered{ false };

        Config::C_Wallet options;
        
        std::shared_ptr<SQLiteStorage> db;
        std::shared_ptr<ll::io::Logger> logger;

        std::string mGuiPath;

        ll::event::ListenerPtr PlayerJoinEventListener;
        ll::event::ListenerPtr PlayerChatEventListener;

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

                int mMoney = param.Score * static_cast<int>(results.size());
                if (ScoreboardUtils::getScore(player, mScoreboard) < mMoney || param.Score < 0)
                    return output.error(tr(origin.getLocaleCode(), "commands.wallet.error.score"));

                ScoreboardUtils::reduceScore(player, mScoreboard, mMoney);

                int mTargetMoney = static_cast<int>(param.Score * (1 - this->mImpl->options.ExchangeRate));
                for (Player*& target : results) {
                    ScoreboardUtils::addScore(*target, mScoreboard, mTargetMoney);

                    long long perTargetFee = static_cast<long long>(param.Score) - mTargetMoney;
                    this->appendLedger(player.getUuid().asString(), player.getRealName(), target->getUuid().asString(), target->getRealName(), mTargetMoney, perTargetFee, "transfer")
                        .or_else(modules::defaultErrorHandler<WalletPlugin>);
                }

                long long fee = static_cast<long long>(mMoney) - static_cast<long long>(mTargetMoney) * results.size();
                if (fee > 0)
                    this->accumulateFee(fee).or_else(modules::defaultErrorHandler<WalletPlugin>);

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
                        modules::defaultErrorHandler<WalletPlugin>(result.error());

                        values->elements.emplace_back(false);
                        return values;
                    }

                    if (!result.value()) {
                        return LanguagePlugin::getShared()->getLanguage(player)
                            .and_then([&player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                                player.sendMessage(tr(language, "wallet.tips.transfer"));

                                values->elements.emplace_back(false);
                                return values;
                            });
                    }

                    values->elements.emplace_back(true);
                    return values;
                });

                form::GUIManager::getInstance().registerRequest("wallet.redenvelope.submit", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    if (args->elements.size() != 3 ||
                        !std::holds_alternative<std::string>(args->elements[0]) ||
                        !std::holds_alternative<std::string>(args->elements[1]) ||
                        !std::holds_alternative<std::string>(args->elements[2]))
                        return ll::makeStringError("wallet.redenvelope.submit: must take three string parameters");

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

                    if (score <= 0 || count <= 0 || ScoreboardUtils::getScore(player, this->getTargetScoreboard()) < score * count) {
                        return LanguagePlugin::getShared()->getLanguage(player)
                            .and_then([&player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                                player.sendMessage(tr(language, "wallet.tips.redenvelope"));

                                values->elements.emplace_back(false);
                                return values;
                            });
                    }

                    auto result = this->redenvelope(player, key, score, count);
                    if (!result.has_value()) {
                        modules::defaultErrorHandler<WalletPlugin>(result.error());

                        values->elements.emplace_back(false);
                        return values;
                    }

                    values->elements.emplace_back(true);
                    return values;
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
                            { "score", "0" }
                        };

                        return this->mImpl->db->set("Wallet", uuid, data);
                    }

                    return {};
                })
                .or_else(modules::defaultErrorHandler<WalletPlugin>);

            this->mImpl->db->get("Wallet", uuid, "score", "0")
                .and_then([this, uuid, &event](const std::string& value) -> ll::Expected<void> {
                    int score = SystemUtils::toInt(value, 0);
                    if (score <= 0)
                        return {};

                    if (this->mImpl->mSettling.contains(uuid))
                        return {};

                    this->mImpl->mSettling[uuid] = true;
                    auto guard = make_scope_guard([this, uuid]() -> void {
                        this->mImpl->mSettling.erase(uuid);
                    });

                    return this->mImpl->db->set("Wallet", uuid, "score", "0")
                        .transform([this, score, &event]() -> void {
                            ScoreboardUtils::addScore(event.self(), this->mImpl->options.TargetScoreboard, score);
                        });
                })
                .or_else(modules::defaultErrorHandler<WalletPlugin>);
        });
        this->mImpl->PlayerChatEventListener = eventBus.emplaceListener<ll::event::PlayerChatEvent>([this](ll::event::PlayerChatEvent& event) mutable -> void {
            if (event.self().isSimulatedPlayer())
                return;

            this->tryGrabRedEnvelope(event.self(), event.message())
                .or_else([](ll::Error e) -> ll::Expected<void> {
                    if (e.isA<ll::ErrorCodeError>())
                        return {};

                    return ll::Unexpected(e);
                })
                .or_else(modules::defaultErrorHandler<WalletPlugin>);
        }, ll::event::EventPriority::High);
    }

    void WalletPlugin::unlistenEvent() {
        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        eventBus.removeListener(this->mImpl->PlayerJoinEventListener);
        eventBus.removeListener(this->mImpl->PlayerChatEventListener);

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

    ll::Expected<bool> WalletPlugin::forTransfer(Player& player, const std::string& target, const std::string& name, int score) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        std::string mScoreboard = this->mImpl->options.TargetScoreboard;
        if (ScoreboardUtils::getScore(player, mScoreboard) < score || score <= 0)
            return false;

        ScoreboardUtils::reduceScore(player, mScoreboard, score);

        int mTargetMoney = static_cast<int>(score * (1 - this->mImpl->options.ExchangeRate));

        return this->transfer(target, mTargetMoney)
            .transform([this, name, score, mTargetMoney, uuid = player.getUuid().asString(), playerName = player.getRealName()]() -> bool {
                this->getLogger()->info(fmt::runtime(tr({}, "wallet.log")), playerName, name, score);

                long long fee = static_cast<long long>(score) - mTargetMoney;
                if (fee > 0)
                    this->accumulateFee(fee).or_else(modules::defaultErrorHandler<WalletPlugin>);

                this->appendLedger(uuid, playerName, target, name, mTargetMoney, fee, "transfer")
                    .or_else(modules::defaultErrorHandler<WalletPlugin>);

                return true;
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

        this->broadcastReceive(entry, player, amount, people + 1);

        if (amount > entry.kingAmount) {
            entry.kingAmount = amount;
            entry.kingUuid = uuid;
            entry.kingName = player.getRealName();
        }

        if (nowFull) {
            this->announceKing(entry);

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

            this->mImpl->mRedEnvelopes[chatKey].push_back({
                id,
                chatKey,
                m["sender_uuid"],
                m["sender_name"],
                count,
                expireAt,
                "",
                "",
                0
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

                return this->mImpl->db->set("Wallet", target, "score", std::to_string(walletScore + score));
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

    ll::Expected<void> WalletPlugin::redenvelope(Player& player, const std::string& key, int score, int count) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        if (count <= 0 || score <= 0)
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        std::string uuid = player.getUuid().asString();

        int total = score * count;
        if (ScoreboardUtils::getScore(player, this->getTargetScoreboard()) < total)
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

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
            { "count", std::to_string(count) },
            { "people", "0" },
            { "created_at", SystemUtils::getNowTime() },
            { "expire_at", std::to_string(expire) }
        };

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
            0
        });

        this->appendLedger(uuid, player.getRealName(), "", "", total, 0, "redenvelope_send")
            .or_else(modules::defaultErrorHandler<WalletPlugin>);

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
