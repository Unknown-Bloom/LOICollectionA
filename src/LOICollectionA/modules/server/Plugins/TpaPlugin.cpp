#include <atomic>
#include <memory>
#include <ranges>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

#include <fmt/format.h>

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

#include <mc/deps/core/string/HashedString.h>

#include <mc/world/level/Level.h>
#include <mc/world/actor/ActorDefinitionIdentifier.h>
#include <mc/world/actor/player/Player.h>

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

#include "LOICollectionA/base/Cache.h"
#include "LOICollectionA/base/Wrapper.h"
#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/TpaPlugin.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    struct TpaPlugin::RequestEntry {
        std::string id;
        std::string source;
        std::string target;

        TpaType type = TpaType::tpa;
    };

    struct TpaPlugin::PlayerRequestSetEntry {
        std::unordered_set<std::string> sent;
        std::unordered_set<std::string> received;
    };

    enum class SelectorType : int {
        tpa = 0,
        tphere = 1
    };

    struct TpaPlugin::operation {
        CommandSelector<Player> Target;
        SelectorType Type{ SelectorType::tpa };
        std::string Id;
    };

    struct TpaPlugin::Impl {
        std::shared_ptr<TimerManager> mTimerManager;

        ll::ConcurrentDenseMap<std::string, RequestEntry> mRequests;
        ll::ConcurrentDenseMap<std::string, PlayerRequestSetEntry> mActiveRequest;
        ll::ConcurrentDenseMap<std::pair<std::string, std::string>, std::string> mRequestPending;

        LRUKCache<std::string, std::vector<std::string>> BlacklistCache;
        LRUKCache<std::string, bool> InviteCache;

        std::atomic<bool> mRegistered{ false };

        Config::C_Tpa options;

        std::shared_ptr<SQLiteStorage> db;
        std::shared_ptr<SQLiteStorage> db2;
        std::shared_ptr<ll::io::Logger> logger;

        std::string mGuiPath;
        
        ll::event::ListenerPtr PlayerJoinEventListener;

        Impl() : mTimerManager(std::make_shared<TimerManager>(ll::thread::ServerThreadExecutor::getDefault())),
            BlacklistCache(100, 100), InviteCache(100, 100) {}
    };

    TpaPlugin::TpaPlugin() : mImpl(std::make_unique<Impl>()) {};
    TpaPlugin::~TpaPlugin() = default;

    std::shared_ptr<TpaPlugin> TpaPlugin::getShared() {
        static auto instance = std::shared_ptr<TpaPlugin>(new TpaPlugin());
        return instance;
    }

    std::error_code TpaPlugin::makeErrorCode(TpaPluginErrorCode e) {
        static TpaPluginErrorCategory cat;
        return std::error_code{ static_cast<int>(e), cat };
    }
    
    std::shared_ptr<SQLiteStorage> TpaPlugin::getDatabase() {
        return this->mImpl->db;
    }

    std::shared_ptr<ll::io::Logger> TpaPlugin::getLogger() {
        return this->mImpl->logger;
    }

    void TpaPlugin::registeryCommand() {
        ll::command::CommandHandle& command = ll::command::CommandRegistrar::getInstance(false)
            .getOrCreateCommand("tpa", tr({}, "commands.tpa.description"), CommandPermissionLevel::Any, CommandFlagValue::NotCheat | CommandFlagValue::Async);
        command.overload<operation>().text("invite").required("Type").required("Target").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                Actor* entity = origin.getEntity();
                if (entity == nullptr || !entity->isType(ActorType::Player))
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
                Player& player = *static_cast<Player*>(entity);

                CommandSelectorResults<Player> results = param.Target.results(origin);
                if (results.empty())
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));

                std::string uuid = player.getUuid().asString();
                auto mResults = results | std::views::filter([this, uuid](Player*& target) -> bool {
                    auto result = this->getBlacklist(*target)
                        .and_then([this](const std::vector<std::string>& ids) -> ll::Expected<std::vector<std::string>> {
                            return this->getBlacklistFromTarget(ids);
                        })
                        .transform([uuid, &target](const std::vector<std::string>& ids) -> bool {
                            return !target->isSimulatedPlayer() && std::find(ids.begin(), ids.end(), uuid) == ids.end();
                        })
                        .and_then([this, uuid, &target](bool exists) -> ll::Expected<bool> {
                            return this->isInvite(*target)
                                .transform([uuid, exists, &target](bool invite) -> bool { 
                                    return exists && !invite && target->getUuid().asString() != uuid;
                                });
                        });

                    if (!result.has_value()) {
                        modules::defaultErrorHandler<TpaPlugin>(result.error());

                        return false;
                    }
                    
                    return result.value();
                });

                int mResultSize = static_cast<int>(std::ranges::distance(mResults));
                if (this->mImpl->options.RequestUpload > 0 && mResultSize > this->mImpl->options.RequestUpload) {
                    output.error(fmt::runtime(tr(origin.getLocaleCode(), "commands.tpa.error.invite.request")), this->mImpl->options.RequestUpload);
                    return;
                }

                int mMoney = this->mImpl->options.RequestRequired * mResultSize;
                if (ScoreboardUtils::getScore(player, this->mImpl->options.TargetScoreboard) < mMoney) {
                    output.error(fmt::runtime(tr(origin.getLocaleCode(), "commands.tpa.error.invite")), mMoney);
                    return;
                }

                ScoreboardUtils::reduceScore(player, this->mImpl->options.TargetScoreboard, mMoney);

                for (Player*& pl : mResults) {
                    this->requestInvite(player, *pl, param.Type == SelectorType::tpa
                        ? TpaType::tpa : TpaType::tphere
                    ).or_else(modules::defaultErrorHandler<TpaPlugin, bool>);

                    output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.tpa.success.invite")), pl->getRealName());
                }
            });
        command.overload<operation>().text("accept").required("Id").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                Actor* entity = origin.getEntity();
                if (entity == nullptr || !entity->isType(ActorType::Player))
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
                Player& player = *static_cast<Player*>(entity);

                this->acceptRequest(player, param.Id)
                    .transform([&output, &origin, id = param.Id](bool result) -> void { 
                        if (!result) {
                            output.error(fmt::runtime(tr(origin.getLocaleCode(), "commands.tpa.error.accept")), id);
                            return;
                        }

                        output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.tpa.success.accept")), id);
                    })
                    .or_else(modules::defaultErrorHandler<TpaPlugin>);
            });
        command.overload<operation>().text("reject").required("Id").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                Actor* entity = origin.getEntity();
                if (entity == nullptr || !entity->isType(ActorType::Player))
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
                Player& player = *static_cast<Player*>(entity);

                this->rejectRequest(player, param.Id)
                    .transform([&output, &origin, id = param.Id](bool result) -> void { 
                        if (!result) {
                            output.error(fmt::runtime(tr(origin.getLocaleCode(), "commands.tpa.error.reject")), id);
                            return;
                        }

                        output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.tpa.success.reject")), id);
                    })
                    .or_else(modules::defaultErrorHandler<TpaPlugin>);
            });
        command.overload<operation>().text("cancel").required("Id").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                this->cancelRequest(param.Id)
                    .transform([&output, &origin, id = param.Id](bool result) -> void { 
                        if (!result) {
                            output.error(fmt::runtime(tr(origin.getLocaleCode(), "commands.tpa.error.cancel")), id);
                            return;
                        }

                        output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.tpa.success.cancel")), id);
                    })
                    .or_else(modules::defaultErrorHandler<TpaPlugin>);
            });
        command.overload().text("gui").execute([](CommandOrigin const& origin, CommandOutput& output) -> void {
            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);

            auto ctx = std::make_shared<frontend::ArrayValue>();
            ctx->elements.emplace_back("");
            ctx->elements.emplace_back("");

            form::GUIManager::getInstance().open("tpa", "tpa.open", form::GUIManagerType::PaginatedForm, player, ctx)
                .or_else(modules::defaultErrorHandler<TpaPlugin>);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
        command.overload().text("setting").execute([](CommandOrigin const& origin, CommandOutput& output) -> void {
            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);

            auto ctx = std::make_shared<frontend::ArrayValue>();
            ctx->elements.emplace_back("");
            ctx->elements.emplace_back("");

            form::GUIManager::getInstance().open("tpa", "tpa.setting", form::GUIManagerType::CustomForm, player, ctx)
                .or_else(modules::defaultErrorHandler<TpaPlugin>);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
        command.overload().text("reload").execute([this](CommandOrigin const& origin, CommandOutput& output) -> void {
            if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors)
                return output.error(tr(origin.getLocaleCode(), "commands.generic.permission"));

            output.success(tr(origin.getLocaleCode(), "commands.generic.reload"));
            
            form::GUIManager::getInstance().load("tpa", this->mImpl->mGuiPath)
                .transform([&origin, &output]() -> void {
                    output.success(tr(origin.getLocaleCode(), "commands.generic.reload.success"));
                })
                .or_else(modules::defaultErrorHandler<TpaPlugin>);
        });
    }

    ll::Expected<bool> TpaPlugin::requestInvite(Player& player, Player& target, TpaType type) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(TpaPluginErrorCode::Invalid));

        auto language = LanguagePlugin::getShared()->getLanguage(player);
        if (!language.has_value())
            return ll::Unexpected(language.error());

        if (this->getRequestCount(player) >= this->getRequestUpload()) {
            player.sendMessage(fmt::format(fmt::runtime(tr(language.value(), "tpa.tips5")), this->getRequestUpload()));

            return false;
        }

        auto forTpa = this->forTpaContent(player);
        if (!forTpa.has_value())
            return ll::Unexpected(forTpa.error());

        if (!forTpa.value()) {
            player.sendMessage(tr(language.value(), "tpa.tips1"));

            return false;
        }

        std::string id = SystemUtils::getCurrentTimestamp();

        auto result = this->sendRequest(player, target, id, type)
            .or_else([](ll::Error e) -> ll::Expected<void> {
                if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == makeErrorCode(TpaPluginErrorCode::RequestExists))
                    return {};

                return ll::Unexpected(e);
            });
        if (!result.has_value())
            return ll::Unexpected(result.error());

        auto ctx = std::make_shared<frontend::ArrayValue>();
        ctx->elements.emplace_back("invite");
        ctx->elements.emplace_back(id);

        auto open = form::GUIManager::getInstance().open("tpa", "tpa.invite", form::GUIManagerType::MessageBox, target, ctx);
        if (!open.has_value())
            return ll::Unexpected(open.error());

        return true;
    }

    ll::Expected<std::vector<std::pair<std::string, std::string>>> TpaPlugin::getEligiblePlayers(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(TpaPluginErrorCode::Invalid));

        std::string uuid = player.getUuid().asString();
        std::vector<std::pair<std::string, std::string>> result;

        ll::service::getLevel()->forEachPlayer([this, uuid, &result](Player& target) -> bool {
            auto check = this->getBlacklist(target)
                .and_then([this](const std::vector<std::string>& ids) -> ll::Expected<std::vector<std::string>> {
                    return this->getBlacklistFromTarget(ids);
                })
                .transform([uuid, &target](const std::vector<std::string>& ids) -> bool {
                    return !target.isSimulatedPlayer() && std::find(ids.begin(), ids.end(), uuid) == ids.end();
                })
                .and_then([this, uuid, &target](bool exists) -> ll::Expected<bool> {
                    return this->isInvite(target)
                        .transform([uuid, exists, &target](bool invite) -> bool {
                            return exists && !invite && target.getUuid().asString() != uuid;
                        });
                });

            if (!check.has_value()) {
                modules::defaultErrorHandler<TpaPlugin>(check.error());

                return true;
            }

            if (check.value())
                result.emplace_back(target.getUuid().asString(), target.getRealName());

            return true;
        });

        return result;
    }

    std::vector<std::pair<std::string, std::string>> TpaPlugin::getAddablePlayers(Player& player) {
        std::string uuid = player.getUuid().asString();
        std::vector<std::pair<std::string, std::string>> result;

        ll::service::getLevel()->forEachPlayer([uuid, &result](Player& target) -> bool {
            if (!target.isSimulatedPlayer() && target.getUuid().asString() != uuid)
                result.emplace_back(target.getUuid().asString(), target.getRealName());

            return true;
        });

        return result;
    }

    ll::Expected<void> TpaPlugin::registeryUI() {
        return form::GUIManager::getInstance().load("tpa", this->mImpl->mGuiPath)
            .transform([this]() -> void {
                form::GUIManager::getInstance().registerValue("tpa.players", [this](Player& player) -> ll::Expected<frontend::ArrayRef> {
                    return this->getEligiblePlayers(player)
                        .transform([](const std::vector<std::pair<std::string, std::string>>& players) -> frontend::ArrayRef {
                            auto values = std::make_shared<frontend::ArrayValue>();

                            for (const auto& [uuid, name] : players)
                                values->elements.emplace_back(name);

                            return values;
                        });
                });

                form::GUIManager::getInstance().registerValue("tpa.players.add", [this](Player& player) -> frontend::ArrayRef {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    for (const auto& [uuid, name] : this->getAddablePlayers(player))
                        values->elements.emplace_back(name);

                    return values;
                });

                form::GUIManager::getInstance().registerValue("tpa.blacklists", [this](Player& player) -> ll::Expected<frontend::ArrayRef> {
                    return this->getBlacklist(player)
                        .transform([](const std::vector<std::string>& ids) -> frontend::ArrayRef {
                            auto values = std::make_shared<frontend::ArrayValue>();

                            for (const std::string& id : ids)
                                values->elements.emplace_back(id);

                            return values;
                        });
                });

                form::GUIManager::getInstance().registerValue("tpa.invite", [this](Player& player) -> ll::Expected<frontend::ArrayRef> {
                    return this->isInvite(player)
                        .transform([](bool value) -> frontend::ArrayRef {
                            auto values = std::make_shared<frontend::ArrayValue>();
                            values->elements.emplace_back(value);

                            return values;
                        });
                });

                form::GUIManager::getInstance().registerValue("tpa.types", [](Player&) -> frontend::ArrayRef {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    auto makeItem = [&values](const std::string& label, int value) -> void {
                        auto obj = std::make_shared<frontend::Object>();
                        obj->className = "DropdownItem";
                        obj->classIndex = -1;
                        obj->assign("label", label);
                        obj->assign("value", value);
                        obj->assign("description", std::monostate{});

                        values->elements.emplace_back(obj);
                    };

                    makeItem("tpa", 0);
                    makeItem("tphere", 1);

                    return values;
                });

                form::GUIManager::getInstance().registerRequest("tpa.player.info", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                        return ll::makeStringError("tpa.player.info: must take exactly one int parameter");

                    int index = std::get<int>(args->elements[0]);
                    auto players = this->getEligiblePlayers(player);
                    if (!players.has_value())
                        return ll::Unexpected(players.error());

                    if (index < 0 || index >= static_cast<int>(players.value().size()))
                        return ll::makeStringError("tpa.player.info: index out of range");

                    auto values = std::make_shared<frontend::ArrayValue>();
                    values->elements.emplace_back(players.value().at(static_cast<size_t>(index)).first);

                    return values;
                });

                form::GUIManager::getInstance().registerRequest("tpa.blacklist.check", [this](frontend::ArrayRef, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    auto ids = this->getBlacklist(player);
                    if (!ids.has_value())
                        return ll::Unexpected(ids.error());

                    auto values = std::make_shared<frontend::ArrayValue>();
                    if (static_cast<int>(ids.value().size()) >= this->getBlacklistUpload()) {
                        return LanguagePlugin::getShared()->getLanguage(player)
                            .and_then([this, &player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                                player.sendMessage(fmt::format(fmt::runtime(tr(language, "tpa.tips2")), this->getBlacklistUpload()));

                                values->elements.emplace_back(false);
                                return values;
                            });
                    }

                    values->elements.emplace_back(true);
                    return values;
                });

                form::GUIManager::getInstance().registerRequest("tpa.blacklist.add.submit", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                        return ll::makeStringError("tpa.blacklist.add.submit: must take exactly one int parameter");

                    int index = std::get<int>(args->elements[0]);
                    auto players = this->getAddablePlayers(player);
                    auto values = std::make_shared<frontend::ArrayValue>();

                    if (index < 0 || index >= static_cast<int>(players.size())) {
                        values->elements.emplace_back(false);
                        return values;
                    }

                    Player* target = ll::service::getLevel()->getPlayer(mce::UUID::fromString(players.at(static_cast<size_t>(index)).first));
                    if (!target) {
                        return LanguagePlugin::getShared()->getLanguage(player)
                            .and_then([&player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                                player.sendMessage(tr(language, "tpa.gui.error"));

                                values->elements.emplace_back(false);
                                return values;
                            });
                    }

                    auto result = this->addBlacklist(player, *target);
                    if (!result.has_value()) {
                        modules::defaultErrorHandler<TpaPlugin>(result.error());

                        values->elements.emplace_back(false);
                        return values;
                    }

                    values->elements.emplace_back(true);
                    return values;
                });

                form::GUIManager::getInstance().registerRequest("tpa.blacklist.info", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                        return ll::makeStringError("tpa.blacklist.info: must take exactly one string parameter");

                    auto id = std::get<std::string>(args->elements[0]);

                    return LanguagePlugin::getShared()->getLanguage(player)
                        .and_then([this, id](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                            auto data = this->getBlacklistData(id);
                            if (!data.has_value())
                                return ll::Unexpected(data.error());

                            auto values = std::make_shared<frontend::ArrayValue>();
                            values->elements.emplace_back(fmt::format(
                                fmt::runtime(tr(language, "tpa.gui.setting.blacklist.set.label")),
                                data.value().at("target"),
                                data.value().at("name"),
                                SystemUtils::toFormatTime(data.value().at("time"), "None")
                            ));

                            return values;
                        });
                });

                form::GUIManager::getInstance().registerCallback("tpa.blacklist.remove", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                    if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                        return ll::makeStringError("tpa.blacklist.remove: must take exactly one string parameter");

                    return this->delBlacklist(player, std::get<std::string>(args->elements[0]))
                        .or_else([](ll::Error e) -> ll::Expected<void> {
                            if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == makeErrorCode(TpaPluginErrorCode::BlacklistNotFound))
                                return {};

                            return ll::Unexpected(e);
                        });
                });

                form::GUIManager::getInstance().registerCallback("tpa.invite.save", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                    if (args->elements.size() != 1 || !std::holds_alternative<bool>(args->elements[0]))
                        return ll::makeStringError("tpa.invite.save: must take exactly one bool parameter");

                    return this->setInvite(player, std::get<bool>(args->elements[0]));
                });

                form::GUIManager::getInstance().registerRequest("tpa.content.submit", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    if (args->elements.size() != 2 ||
                        !std::holds_alternative<std::string>(args->elements[0]) ||
                        !std::holds_alternative<float>(args->elements[1]))
                        return ll::makeStringError("tpa.content.submit: must take a string and a float parameter");

                    Player* target = ll::service::getLevel()->getPlayer(mce::UUID::fromString(std::get<std::string>(args->elements[0])));
                    auto values = std::make_shared<frontend::ArrayValue>();

                    if (!target) {
                        return LanguagePlugin::getShared()->getLanguage(player)
                            .and_then([&player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                                player.sendMessage(tr(language, "tpa.gui.error"));

                                values->elements.emplace_back(false);
                                return values;
                            });
                    }

                    TpaType type = static_cast<int>(std::get<float>(args->elements[1])) == 0 ? TpaType::tpa : TpaType::tphere;
                    auto result = this->requestInvite(player, *target, type);
                    if (!result.has_value()) {
                        modules::defaultErrorHandler<TpaPlugin>(result.error());

                        values->elements.emplace_back(false);
                        return values;
                    }

                    values->elements.emplace_back(result.value());
                    return values;
                });

                form::GUIManager::getInstance().registerRequest("tpa.invite.info", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                        return ll::makeStringError("tpa.invite.info: must take exactly one string parameter");

                    auto id = std::get<std::string>(args->elements[0]);
                    auto it = this->mImpl->mRequests.find(id);
                    if (it == this->mImpl->mRequests.end())
                        return ll::makeStringError("tpa.invite.info: request not found");

                    TpaType type = it->second.type;
                    Player* origin = ll::service::getLevel()->getPlayer(mce::UUID::fromString(it->second.source));
                    if (!origin)
                        return ll::makeStringError("tpa.invite.info: request source is offline");

                    return LanguagePlugin::getShared()->getLanguage(player)
                        .and_then([type, origin](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                            auto values = std::make_shared<frontend::ArrayValue>();
                            values->elements.emplace_back(LOICollectionAPI::CallbackUtils::getInstance().translate(
                                tr(language, type == TpaType::tpa ? "tpa.there" : "tpa.here"),
                                *origin
                            ));

                            return values;
                        });
                });

                form::GUIManager::getInstance().registerCallback("tpa.invite.response", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                    if (args->elements.size() != 2 ||
                        !std::holds_alternative<std::string>(args->elements[0]) ||
                        !std::holds_alternative<int>(args->elements[1]))
                        return ll::makeStringError("tpa.invite.response: must take a string and an int parameter");

                    auto id = std::get<std::string>(args->elements[0]);
                    if (std::get<int>(args->elements[1]) == 0) {
                        return this->acceptRequest(player, id)
                            .or_else([](ll::Error e) -> ll::Expected<bool> {
                                if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == makeErrorCode(TpaPluginErrorCode::RequestNotFound))
                                    return false;

                                return ll::Unexpected(e);
                            })
                            .transform([&player](bool exists) -> void {
                                if (exists)
                                    return;

                                auto language = LanguagePlugin::getShared()->getLanguage(player);
                                if (language.has_value())
                                    player.sendMessage(tr(language.value(), "tpa.gui.error"));
                            });
                    }

                    return this->rejectRequest(player, id)
                        .or_else([](ll::Error e) -> ll::Expected<bool> {
                            if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == makeErrorCode(TpaPluginErrorCode::RequestNotFound))
                                return {};

                            return ll::Unexpected(e);
                        })
                        .transform([](bool) -> void {});
                });
            });
    }

    void TpaPlugin::listenEvent() {
        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        this->mImpl->PlayerJoinEventListener = eventBus.emplaceListener<ll::event::PlayerJoinEvent>([this](ll::event::PlayerJoinEvent& event) mutable -> void {
            if (event.self().isSimulatedPlayer())
                return;

            std::string uuid = event.self().getUuid().asString();

            this->mImpl->db2->has("Tpa", uuid)
                .and_then([this, uuid, name = event.self().getRealName()](bool exists) -> ll::Expected<void> {
                    if (!exists) {
                        std::unordered_map<std::string, std::string> data = {
                            { "name", name },
                            { "invite", "false" }
                        };

                        return this->mImpl->db2->set("Tpa", uuid, data);
                    }

                    return {};
                })
                .or_else(modules::defaultErrorHandler<TpaPlugin>);
        });
    }

    void TpaPlugin::unlistenEvent() {
        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        eventBus.removeListener(this->mImpl->PlayerJoinEventListener);

        this->mImpl->mTimerManager->cancelAll();
    }

    ll::Expected<void> TpaPlugin::setInvite(Player& player, bool invite) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(TpaPluginErrorCode::Invalid));

        return this->mImpl->db2->set("Tpa", player.getUuid().asString(), "invite", invite ? "true" : "false");
    }

    ll::Expected<void> TpaPlugin::addBlacklist(Player& player, Player& target) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(TpaPluginErrorCode::Invalid));

        std::string mObject = player.getUuid().asString();
        std::string mTargetObject = target.getUuid().asString();
        std::string mTismestamp = SystemUtils::getCurrentTimestamp();

        std::unordered_map<std::string, std::string> mData = {
            { "name", target.getRealName() },
            { "target", mTargetObject },
            { "author", mObject },
            { "time", SystemUtils::getNowTime("%Y%m%d%H%M%S") }
        };

        return this->getDatabase()->set("Blacklist", mTismestamp, mData)
            .transform([this, mObject, mTargetObject, mTismestamp, &player]() -> void {
                this->getLogger()->info(fmt::runtime(LOICollectionAPI::CallbackUtils::getInstance().translate(tr({}, "tpa.log2"), player)), mTargetObject);

                if (this->mImpl->BlacklistCache.contains(mObject))
                    this->mImpl->BlacklistCache.update(mObject, [mTismestamp](std::shared_ptr<std::vector<std::string>> mList) -> void {
                        mList->push_back(mTismestamp);
                    });
            });
    }

    ll::Expected<void> TpaPlugin::delBlacklist(Player& player, const std::string& id) {
        if (!this->isValid()) 
            return ll::makeErrorCodeError(makeErrorCode(TpaPluginErrorCode::Invalid));

        return this->hasBlacklist(player, id)
            .and_then([this, id](bool exists) -> ll::Expected<void> {
                if (!exists) {
                    this->getLogger()->warn(fmt::runtime(tr({}, "console.log.error.object")), this->getName());

                    return ll::makeErrorCodeError(makeErrorCode(TpaPluginErrorCode::BlacklistNotFound));
                }

                return this->getDatabase()->del("Blacklist", id);
            })
            .transform([this, id, &player]() -> void {
                this->getLogger()->info(fmt::runtime(LOICollectionAPI::CallbackUtils::getInstance().translate(tr({}, "tpa.log3"), player)), id);

                this->mImpl->BlacklistCache.update(player.getUuid().asString(), [id](std::shared_ptr<std::vector<std::string>> mList) -> void {
                    mList->erase(std::remove(mList->begin(), mList->end(), id), mList->end());
                });
            });
    }

    ll::Expected<void> TpaPlugin::setExecutor(const ll::coro::Executor& executor) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(TpaPluginErrorCode::Invalid));

        this->mImpl->mTimerManager->setExecutor(executor);

        return {};
    }

    ll::Expected<bool> TpaPlugin::acceptRequest(Player& player, const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(TpaPluginErrorCode::Invalid));

        auto it = this->mImpl->mRequests.find(id);
        if (it == this->mImpl->mRequests.end())
            return ll::makeErrorCodeError(makeErrorCode(TpaPluginErrorCode::RequestNotFound));

        Player* origin = ll::service::getLevel()->getPlayer(mce::UUID::fromString(it->second.source));
        if (!origin) {
            auto language = LanguagePlugin::getShared()->getLanguage(player);
            if (!language.has_value())
                return ll::Unexpected(language.error());

            player.sendMessage(tr(language.value(), "tpa.gui.error"));
            return false;
        }

        if (!this->forTpaContent(*origin)) {
            auto language = LanguagePlugin::getShared()->getLanguage(*origin);
            if (!language.has_value())
                return ll::Unexpected(language.error());

            origin->sendMessage(tr(language.value(), "tpa.tips1"));
            return false;
        }

        auto language = LanguagePlugin::getShared()->getLanguage(*origin);
        if (!language.has_value())
            return ll::Unexpected(language.error());

        origin->sendMessage(fmt::format(fmt::runtime(tr(language.value(), "tpa.yes.tips")), player.getRealName(), id));
    
        auto& mover = (it->second.type == TpaType::tpa) ? *origin : player;
        auto& dest  = (it->second.type == TpaType::tpa) ? player : *origin;

        mover.teleport(dest.getPosition(), dest.getDimensionId());
        
        this->getLogger()->info(fmt::format(fmt::runtime(tr({}, "tpa.log1")), dest.getRealName(), mover.getRealName()));

        this->mImpl->mTimerManager->cancel(id);
        
        return this->clearRequest(id)
            .transform([]() -> bool {
                return true;
            });
    }

    ll::Expected<bool> TpaPlugin::rejectRequest(Player& player, const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(TpaPluginErrorCode::Invalid));

        auto it = this->mImpl->mRequests.find(id);
        if (it == this->mImpl->mRequests.end())
            return false;

        if (Player* origin = ll::service::getLevel()->getPlayer(mce::UUID::fromString(it->second.source)); origin) {
            auto language = LanguagePlugin::getShared()->getLanguage(*origin);
            if (!language.has_value())
                return ll::Unexpected(language.error());

            origin->sendMessage(fmt::format(fmt::runtime(tr(language.value(), "tpa.no.tips")), player.getRealName()));
        }

        this->mImpl->mTimerManager->cancel(id);
        
        return this->clearRequest(id)
            .transform([]() -> bool {
                return true;
            });
    }

    ll::Expected<bool> TpaPlugin::cancelRequest(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(TpaPluginErrorCode::Invalid));

        auto it = this->mImpl->mRequests.find(id);
        if (it == this->mImpl->mRequests.end())
            return false;

        this->mImpl->mTimerManager->cancel(id);
        
        return this->clearRequest(id)
            .transform([]() -> bool {
                return true;
            });
    }

    ll::Expected<bool> TpaPlugin::hasRequest(const std::string& origin, const std::string& target) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(TpaPluginErrorCode::Invalid));

        return this->mImpl->mRequestPending.contains({ origin, target });
    }

    ll::Expected<void> TpaPlugin::clearRequest(const std::string& id){
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(TpaPluginErrorCode::Invalid));

        auto it = this->mImpl->mRequests.find(id);
        if (it == this->mImpl->mRequests.end())
            return ll::makeErrorCodeError(makeErrorCode(TpaPluginErrorCode::RequestNotFound));

        this->mImpl->mActiveRequest[it->second.source].sent.erase(id);
        this->mImpl->mActiveRequest[it->second.target].received.erase(id);

        this->mImpl->mRequestPending.erase({ it->second.source, it->second.target });

        this->mImpl->mRequests.erase(id);

        return {};
    }

    ll::Expected<void> TpaPlugin::sendRequest(Player& player, Player& target, const std::string& id, TpaType type) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(TpaPluginErrorCode::Invalid));

        std::string originId = player.getUuid().asString();
        std::string targetId = target.getUuid().asString();

        auto result = this->hasRequest(originId, targetId);
        if (!result.has_value())
            return ll::Unexpected(result.error());

        if (result.value())
            return ll::makeErrorCodeError(makeErrorCode(TpaPluginErrorCode::RequestExists));

        RequestEntry mEntry{ id, originId, targetId, type };

        this->mImpl->mRequests[id] = std::move(mEntry);

        this->mImpl->mActiveRequest[originId].sent.insert(id);
        this->mImpl->mActiveRequest[targetId].received.insert(id);

        this->mImpl->mRequestPending[{ originId, targetId }] = id;

        this->mImpl->mTimerManager->schedule(id, std::chrono::seconds(this->mImpl->options.RequestTimeout), [this, id, originId, targetId]() -> void {
            auto result = this->hasRequest(originId, targetId);
            if (!result.has_value()) {
                modules::defaultErrorHandler<TpaPlugin>(result.error());

                return;
            }
            
            if (!result.value())
                return;

            if (Player* mPlayer = ll::service::getLevel()->getPlayer(mce::UUID::fromString(originId)); mPlayer) {
                LanguagePlugin::getShared()->getLanguage(*mPlayer)
                    .transform([&mPlayer, id](const std::string& language) -> void {
                        mPlayer->sendMessage(fmt::format(fmt::runtime(tr(language, "tpa.tips4")), id));
                    })
                    .or_else(modules::defaultErrorHandler<TpaPlugin>);
            }
            
            if (Player* mPlayer = ll::service::getLevel()->getPlayer(mce::UUID::fromString(targetId)); mPlayer) {
                LanguagePlugin::getShared()->getLanguage(*mPlayer)
                    .transform([&mPlayer, id](const std::string& language) -> void {
                        mPlayer->sendMessage(fmt::format(fmt::runtime(tr(language, "tpa.tips4")), id));
                    })
                    .or_else(modules::defaultErrorHandler<TpaPlugin>);
            }
        
            this->clearRequest(id)
                .or_else([](ll::Error e) -> ll::Expected<void> {
                    if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == makeErrorCode(TpaPluginErrorCode::RequestNotFound))
                        return {};

                    return ll::Unexpected(e);
                })
                .or_else(modules::defaultErrorHandler<TpaPlugin>);
        });

        return LanguagePlugin::getShared()->getLanguage(player)
            .transform([this, &player, id, targetName = target.getRealName()](const std::string& language) -> void {
                player.sendMessage(fmt::format(fmt::runtime(tr(language, "tpa.tips3")), id));

                this->getLogger()->info(fmt::runtime(tr({}, "tpa.log4")), player.getRealName(), targetName, id);
            });
    }

    ll::Expected<std::string> TpaPlugin::getBlacklist(Player& player, Player& target) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(TpaPluginErrorCode::Invalid));

        return this->getDatabase()->find("Blacklist", {
            { "target", target.getUuid().asString() },
            { "author", player.getUuid().asString() }
        }, "", SQLiteStorage::FindCondition::AND);
    }

    ll::Expected<std::vector<std::string>> TpaPlugin::getBlacklist(Player& player) {
        if (!this->isValid()) 
            return ll::makeErrorCodeError(makeErrorCode(TpaPluginErrorCode::Invalid));

        std::string uuid = player.getUuid().asString();
        if (this->mImpl->BlacklistCache.contains(uuid))
            return *this->mImpl->BlacklistCache.get(uuid).value();

        return this->getDatabase()->find("Blacklist", {
            { "author", uuid }
        }, SQLiteStorage::FindCondition::AND)
            .transform([this, uuid](const std::vector<std::string>& keys) -> std::vector<std::string> {
                this->mImpl->BlacklistCache.put(uuid, keys);

                return keys;
            });
    }

    ll::Expected<std::vector<std::string>> TpaPlugin::getBlacklistFromTarget(const std::vector<std::string>& ids) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(TpaPluginErrorCode::Invalid));

        return this->getDatabase()->get("Blacklist", ids)
            .transform([](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> data) -> std::vector<std::string> {
                return data
                    | std::views::values
                    | std::views::transform([](const std::unordered_map<std::string, std::string>& entry) -> std::string {
                        return entry.at("target");
                    })
                    | std::ranges::to<std::vector<std::string>>();
            });
    }

    ll::Expected<std::unordered_map<std::string, std::string>> TpaPlugin::getBlacklistData(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(TpaPluginErrorCode::Invalid));

        return this->getDatabase()->get("Blacklist", id);
    }

    ll::Expected<bool> TpaPlugin::hasBlacklist(Player& player, const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(TpaPluginErrorCode::Invalid));

        std::string mObject = player.getUuid().asString();
        if (this->mImpl->BlacklistCache.contains(mObject)) {
            auto mKeys = this->mImpl->BlacklistCache.get(mObject).value();
            return std::find(mKeys->begin(), mKeys->end(), id) != mKeys->end();
        }

        return this->getDatabase()->has("Blacklist", id);
    }

    ll::Expected<bool> TpaPlugin::forTpaContent(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(TpaPluginErrorCode::Invalid));

        std::string mScoreboard = this->mImpl->options.TargetScoreboard;

        int mRequestRequired = this->mImpl->options.RequestRequired;
        if (mRequestRequired && ScoreboardUtils::getScore(player, mScoreboard) < mRequestRequired)
            return false;

        ScoreboardUtils::reduceScore(player, mScoreboard, mRequestRequired);

        return true;
    }

    ll::Expected<bool> TpaPlugin::isInvite(Player& player) {
        if (!this->isValid()) 
            return ll::makeErrorCodeError(makeErrorCode(TpaPluginErrorCode::Invalid));

        std::string uuid = player.getUuid().asString();
        if (this->mImpl->InviteCache.contains(uuid))
            return *this->mImpl->InviteCache.get(uuid).value();
        
        return this->mImpl->db2->get("Tpa", uuid, "invite", "false")
            .transform([this, uuid](const std::string& value) -> bool {
                bool result = (value == "true");

                this->mImpl->InviteCache.put(uuid, result);

                return result;
            });
    }

    bool TpaPlugin::isValid() {
        return this->getLogger() != nullptr && this->getDatabase() != nullptr && this->mImpl->db2 != nullptr;
    }

    int TpaPlugin::getBlacklistUpload() {
        return this->mImpl->options.BlacklistUpload;
    }
    
    int TpaPlugin::getRequestUpload() {
        return this->mImpl->options.RequestUpload;
    }
    
    int TpaPlugin::getRequestCount(Player& player) {
        std::string originId = player.getUuid().asString();

        return static_cast<int>(this->mImpl->mActiveRequest[originId].sent.size()
            + this->mImpl->mActiveRequest[originId].received.size());
    }

    std::string TpaPlugin::getName() {
        return "TpaPlugin";
    }

    modules::ModulePriority TpaPlugin::getPriority() {
        return modules::ModulePriority::High;
    }

    ll::Expected<bool> TpaPlugin::load() {
        if (!ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Tpa.ModuleEnabled)
            return false;

        auto mDataPath = std::filesystem::path(ServiceProvider::getInstance().getService<std::string>("DataPath")->data());

        this->mImpl->db = std::make_shared<SQLiteStorage>((mDataPath / "tpa.db").string());
        this->mImpl->db2 = ServiceProvider::getInstance().getService<SQLiteStorage>("SettingsDB");
        this->mImpl->logger = ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA");
        this->mImpl->options = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Tpa;
        this->mImpl->mGuiPath = (std::filesystem::path(ServiceProvider::getInstance().getService<std::string>("GuiPath")->data()) / "tpa.lcui").string();

        return true;
    }

    ll::Expected<bool> TpaPlugin::unload() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        this->mImpl->db.reset();
        this->mImpl->db2.reset();
        this->mImpl->logger.reset();
        this->mImpl->options = {};

        if (this->mImpl->mRegistered.load(std::memory_order_acquire))
            this->unlistenEvent();

        return true;
    }

    ll::Expected<bool> TpaPlugin::registry() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        return this->mImpl->db2->create("Tpa", [](SQLiteStorage::ColumnCallback ctor) -> void {
            ctor("name");
            ctor("invite");
        }).and_then([this]() -> ll::Expected<void> {
            return this->getDatabase()->create("Blacklist", [](SQLiteStorage::ColumnCallback ctor) -> void {
                ctor("name");
                ctor("target");
                ctor("author");
                ctor("time");
            });
        }).and_then([this]() -> ll::Expected<void> {
            return this->registeryUI();
        }).transform([this]() -> bool {
            this->registeryCommand();
            this->listenEvent();

            this->mImpl->mRegistered.store(true, std::memory_order_release);

            return true;
        });
    }

    ll::Expected<bool> TpaPlugin::unregistry() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        this->unlistenEvent();

        return this->getDatabase()->exec("VACUUM;")
            .transform([this]() -> bool {
                this->mImpl->mRegistered.store(false, std::memory_order_release);

                return true;
            });
    }
}
