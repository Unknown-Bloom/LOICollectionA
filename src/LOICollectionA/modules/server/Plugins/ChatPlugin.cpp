#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>

#include <fmt/format.h>

#include <ll/api/Expected.h>
#include <ll/api/io/Logger.h>
#include <ll/api/io/LoggerRegistry.h>
#include <ll/api/service/Bedrock.h>
#include <ll/api/command/Command.h>
#include <ll/api/command/CommandHandle.h>
#include <ll/api/command/CommandRegistrar.h>
#include <ll/api/event/EventBus.h>
#include <ll/api/event/ListenerBase.h>
#include <ll/api/event/player/PlayerChatEvent.h>
#include <ll/api/event/player/PlayerJoinEvent.h>

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
#include "LOICollectionA/include/server/Plugins/MutePlugin.h"

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/data/SQLiteStorage.h"

#include "LOICollectionA/frontend/AST.h"

#include "LOICollectionA/include/form/GUIManager.h"

#include "LOICollectionA/base/Cache.h"
#include "LOICollectionA/base/Wrapper.h"
#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/ChatPlugin.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    struct ChatPlugin::operation {
        CommandSelector<Player> Target;
        std::string Title;
        int Time = 0;
    };

    struct ChatPlugin::Impl {
        LRUKCache<std::string, std::vector<std::string>> BlacklistCache;

        std::atomic<bool> mRegistered{ false };

        Config::C_Chat options;

        std::shared_ptr<SQLiteStorage> db;
        std::shared_ptr<SQLiteStorage> db2;
        std::shared_ptr<ll::io::Logger> logger;

        std::string mGuiPath;

        ll::event::ListenerPtr PlayerChatEventListener;
        ll::event::ListenerPtr PlayerJoinEventListener;

        Impl() : BlacklistCache(100, 100) {}
    };

    ChatPlugin::ChatPlugin() : mImpl(std::make_unique<Impl>()) {};
    ChatPlugin::~ChatPlugin() = default;

    std::shared_ptr<ChatPlugin> ChatPlugin::getShared() {
        static auto instance = std::shared_ptr<ChatPlugin>(new ChatPlugin());
        return instance;
    }

    std::error_code ChatPlugin::makeErrorCode(ChatPluginErrorCode e) {
        static ChatPluginErrorCategory cat;
        return std::error_code{ static_cast<int>(e), cat };
    }

    std::shared_ptr<SQLiteStorage> ChatPlugin::getDatabase() {
        return this->mImpl->db;
    }

    std::shared_ptr<ll::io::Logger> ChatPlugin::getLogger() {
        return this->mImpl->logger;
    }

    void ChatPlugin::registeryCommand() {
        ll::command::CommandHandle& command = ll::command::CommandRegistrar::getInstance(false)
            .getOrCreateCommand("chat", tr({}, "commands.chat.description"), CommandPermissionLevel::Any, CommandFlagValue::NotCheat | CommandFlagValue::Async);
        command.overload<operation>().text("add").required("Target").required("Title").optional("Time").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors)
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.permission"));

                CommandSelectorResults<Player> results = param.Target.results(origin);
                if (results.empty())
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));

                for (Player*& pl : results) {
                    this->addTitle(*pl, param.Title, param.Time).or_else(modules::defaultErrorHandler<ChatPlugin>);

                    output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.chat.success.add")), param.Title, pl->getRealName());
                }
            });
        command.overload<operation>().text("remove").required("Target").required("Title").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors)
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.permission"));

                CommandSelectorResults<Player> results = param.Target.results(origin);
                if (results.empty())
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));

                for (Player*& pl : results) {
                    this->delTitle(*pl, param.Title)
                        .or_else([](ll::Error e) -> ll::Expected<void> {
                            if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == makeErrorCode(ChatPluginErrorCode::TitleNotFound))
                                return {};

                            return ll::Unexpected(e);
                        })
                        .or_else(modules::defaultErrorHandler<ChatPlugin>);

                    output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.chat.success.remove")), pl->getRealName(), param.Title);
                }
            });
        command.overload<operation>().text("set").required("Target").required("Title").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors)
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.permission"));

                CommandSelectorResults<Player> results = param.Target.results(origin);
                if (results.empty())
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));

                for (Player*& pl : results) {
                    this->setTitle(*pl, param.Title).or_else(modules::defaultErrorHandler<ChatPlugin>);

                    output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.chat.success.set")), pl->getRealName(), param.Title);
                }
            });
        command.overload<operation>().text("list").required("Target").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors)
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.permission"));

                CommandSelectorResults<Player> results = param.Target.results(origin);
                if (results.empty())
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));

                for (Player*& player : results) {
                    this->getTitles(*player)
                        .transform([&output, &origin, name = player->getRealName()](const std::vector<std::string>& titles) -> void {
                             if (titles.empty())
                                return output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.chat.success.list")), name, "None");

                            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.chat.success.list")), name, fmt::join(titles, ", "));
                        })
                        .or_else(modules::defaultErrorHandler<ChatPlugin>);
                }
            });
        command.overload().text("reload").execute([this](CommandOrigin const& origin, CommandOutput& output) -> void {
            if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors)
                return output.error(tr(origin.getLocaleCode(), "commands.generic.permission"));

            output.success(tr(origin.getLocaleCode(), "commands.generic.reload"));
            
            form::GUIManager::getInstance().load("chat", this->mImpl->mGuiPath)
                .transform([&origin, &output]() -> void {
                    output.success(tr(origin.getLocaleCode(), "commands.generic.reload.success"));
                })
                .or_else(modules::defaultErrorHandler<ChatPlugin>);
        });
        command.overload().text("gui").execute([](CommandOrigin const& origin, CommandOutput& output) -> void {
            if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors)
                return output.error(tr(origin.getLocaleCode(), "commands.generic.permission"));

            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isRemotePlayer())
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);

            form::GUIManager::getInstance().open("chat", "chat.manage", form::GUIManagerType::CustomForm, player)
                .or_else(modules::defaultErrorHandler<ChatPlugin>);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
        command.overload().text("setting").execute([](CommandOrigin const& origin, CommandOutput& output) -> void {
            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isRemotePlayer())
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);
            
            form::GUIManager::getInstance().open("chat", "chat.setting", form::GUIManagerType::CustomForm, player)
                .or_else(modules::defaultErrorHandler<ChatPlugin>);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
    }

    ll::Expected<void> ChatPlugin::registeryUI() {
        return form::GUIManager::getInstance().load("chat", this->mImpl->mGuiPath)
            .transform([this]() -> void {
                form::GUIManager::getInstance().registerValue("chat.players", [](Player&) -> frontend::ArrayRef {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    ll::service::getLevel()->forEachPlayer([&values](Player& target) -> bool {
                        if (!target.isSimulatedPlayer())
                            values->elements.emplace_back(target.getRealName());

                        return true;
                    });

                    return values;
                });

                form::GUIManager::getInstance().registerValue("chat.players.add", [](Player& player) -> frontend::ArrayRef {
                    auto values = std::make_shared<frontend::ArrayValue>();
                    std::string uuid = player.getUuid().asString();

                    ll::service::getLevel()->forEachPlayer([uuid, &values](Player& target) -> bool {
                        if (!target.isSimulatedPlayer() && target.getUuid().asString() != uuid)
                            values->elements.emplace_back(target.getRealName());

                        return true;
                    });

                    return values;
                });

                form::GUIManager::getInstance().registerValue("chat.titles.self", [this](Player& player) -> ll::Expected<frontend::ArrayRef> {
                    return this->getTitles(player)
                        .transform([](const std::vector<std::string>& titles) -> frontend::ArrayRef {
                            auto values = std::make_shared<frontend::ArrayValue>();

                            for (const std::string& title : titles)
                                values->elements.emplace_back(title);

                            return values;
                        });
                });

                form::GUIManager::getInstance().registerValue("chat.blacklists", [this](Player& player) -> ll::Expected<frontend::ArrayRef> {
                    return this->getBlacklist(player)
                        .transform([](const std::vector<std::string>& ids) -> frontend::ArrayRef {
                            auto values = std::make_shared<frontend::ArrayValue>();

                            for (const std::string& id : ids)
                                values->elements.emplace_back(id);

                            return values;
                        });
                });

                form::GUIManager::getInstance().registerRequest("chat.player.info", [](frontend::ArrayRef args, Player&) -> ll::Expected<frontend::ArrayRef> {
                    if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                        return ll::makeStringError("chat.player.info: must take exactly one int parameter");

                    int index = std::get<int>(args->elements[0]);
                    std::vector<std::string> uuids;

                    ll::service::getLevel()->forEachPlayer([&uuids](Player& target) -> bool {
                        if (!target.isSimulatedPlayer())
                            uuids.push_back(target.getUuid().asString());

                        return true;
                    });

                    if (index < 0 || index >= static_cast<int>(uuids.size()))
                        return ll::makeStringError("chat.player.info: index out of range");

                    auto values = std::make_shared<frontend::ArrayValue>();
                    values->elements.emplace_back(uuids.at(static_cast<size_t>(index)));

                    return values;
                });

                form::GUIManager::getInstance().registerRequest("chat.title.add", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    if (args->elements.size() != 3 ||
                        !std::holds_alternative<std::string>(args->elements[0]) ||
                        !std::holds_alternative<std::string>(args->elements[1]) ||
                        !std::holds_alternative<std::string>(args->elements[2]))
                        return ll::makeStringError("chat.title.add: must take three string parameters");

                    auto values = std::make_shared<frontend::ArrayValue>();
                    Player* target = ll::service::getLevel()->getPlayer(mce::UUID::fromString(std::get<std::string>(args->elements[0])));
                    if (!target) {
                        return LanguagePlugin::getShared()->getLanguage(player)
                            .and_then([&player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                                player.sendMessage(tr(language, "chat.gui.error"));

                                values->elements.emplace_back(false);
                                return values;
                            });
                    }

                    auto title = std::get<std::string>(args->elements[1]);
                    if (title.empty()) {
                        return LanguagePlugin::getShared()->getLanguage(player)
                            .and_then([&player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                                player.sendMessage(tr(language, "generic.tips.noinput"));

                                values->elements.emplace_back(false);
                                return values;
                            });
                    }

                    auto result = this->addTitle(*target, title, SystemUtils::toInt(std::get<std::string>(args->elements[2]), 0));
                    if (!result.has_value()) {
                        modules::defaultErrorHandler<ChatPlugin>(result.error());

                        values->elements.emplace_back(false);
                        return values;
                    }

                    values->elements.emplace_back(true);
                    return values;
                });

                form::GUIManager::getInstance().registerRequest("chat.titles", [this](frontend::ArrayRef args, Player&) -> ll::Expected<frontend::ArrayRef> {
                    if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                        return ll::makeStringError("chat.titles: must take exactly one string parameter");

                    Player* target = ll::service::getLevel()->getPlayer(mce::UUID::fromString(std::get<std::string>(args->elements[0])));
                    if (!target)
                        return ll::makeStringError("chat.titles: target is offline");

                    return this->getTitles(*target)
                        .transform([](const std::vector<std::string>& titles) -> frontend::ArrayRef {
                            auto values = std::make_shared<frontend::ArrayValue>();

                            for (const std::string& title : titles)
                                values->elements.emplace_back(title);

                            return values;
                        });
                });

                form::GUIManager::getInstance().registerRequest("chat.title.info", [](frontend::ArrayRef, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    return LanguagePlugin::getShared()->getLanguage(player)
                        .and_then([&player](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                            auto values = std::make_shared<frontend::ArrayValue>();
                            values->elements.emplace_back(LOICollectionAPI::CallbackUtils::getInstance().translate(tr(language, "chat.gui.setTitle.label"), player));

                            return values;
                        });
                });

                form::GUIManager::getInstance().registerRequest("chat.blacklist.check", [this](frontend::ArrayRef, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    auto ids = this->getBlacklist(player);
                    if (!ids.has_value())
                        return ll::Unexpected(ids.error());

                    auto values = std::make_shared<frontend::ArrayValue>();
                    if (static_cast<int>(ids.value().size()) >= this->getBlacklistUpload()) {
                        return LanguagePlugin::getShared()->getLanguage(player)
                            .and_then([this, &player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                                player.sendMessage(fmt::format(fmt::runtime(tr(language, "chat.gui.setBlacklist.tips1")), this->getBlacklistUpload()));

                                values->elements.emplace_back(false);
                                return values;
                            });
                    }

                    values->elements.emplace_back(true);
                    return values;
                });

                form::GUIManager::getInstance().registerRequest("chat.blacklist.add.submit", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    if (args->elements.size() != 1 || !std::holds_alternative<int>(args->elements[0]))
                        return ll::makeStringError("chat.blacklist.add.submit: must take exactly one int parameter");

                    int index = std::get<int>(args->elements[0]);
                    std::vector<std::pair<std::string, std::string>> players;
                    std::string uuid = player.getUuid().asString();

                    ll::service::getLevel()->forEachPlayer([uuid, &players](Player& target) -> bool {
                        if (!target.isSimulatedPlayer() && target.getUuid().asString() != uuid)
                            players.emplace_back(target.getUuid().asString(), target.getRealName());

                        return true;
                    });

                    auto values = std::make_shared<frontend::ArrayValue>();
                    if (index < 0 || index >= static_cast<int>(players.size())) {
                        values->elements.emplace_back(false);
                        return values;
                    }

                    Player* target = ll::service::getLevel()->getPlayer(mce::UUID::fromString(players.at(static_cast<size_t>(index)).first));
                    if (!target) {
                        return LanguagePlugin::getShared()->getLanguage(player)
                            .and_then([&player, values](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                                player.sendMessage(tr(language, "chat.gui.error"));

                                values->elements.emplace_back(false);
                                return values;
                            });
                    }

                    auto result = this->addBlacklist(player, *target);
                    if (!result.has_value()) {
                        modules::defaultErrorHandler<ChatPlugin>(result.error());

                        values->elements.emplace_back(false);
                        return values;
                    }

                    values->elements.emplace_back(true);
                    return values;
                });

                form::GUIManager::getInstance().registerRequest("chat.blacklist.info", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                        return ll::makeStringError("chat.blacklist.info: must take exactly one string parameter");

                    auto id = std::get<std::string>(args->elements[0]);

                    return LanguagePlugin::getShared()->getLanguage(player)
                        .and_then([this, id](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                            auto data = this->getBlacklistData(id);
                            if (!data.has_value())
                                return ll::Unexpected(data.error());

                            auto values = std::make_shared<frontend::ArrayValue>();
                            values->elements.emplace_back(fmt::format(
                                fmt::runtime(tr(language, "chat.gui.setBlacklist.set.label")),
                                data.value().at("target"),
                                data.value().at("name"),
                                SystemUtils::toFormatTime(data.value().at("time"), "None")
                            ));

                            return values;
                        });
                });

                form::GUIManager::getInstance().registerCallback("chat.title.remove", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                    if (args->elements.size() != 2 ||
                        !std::holds_alternative<std::string>(args->elements[0]) ||
                        !std::holds_alternative<std::string>(args->elements[1]))
                        return ll::makeStringError("chat.title.remove: must take two string parameters");

                    Player* target = ll::service::getLevel()->getPlayer(mce::UUID::fromString(std::get<std::string>(args->elements[0])));
                    if (!target) {
                        return LanguagePlugin::getShared()->getLanguage(player)
                            .and_then([&player](const std::string& language) -> ll::Expected<void> {
                                player.sendMessage(tr(language, "chat.gui.error"));

                                return {};
                            });
                    }

                    return this->delTitle(*target, std::get<std::string>(args->elements[1]))
                        .or_else([](ll::Error e) -> ll::Expected<void> {
                            if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == makeErrorCode(ChatPluginErrorCode::TitleNotFound))
                                return {};

                            return ll::Unexpected(e);
                        });
                });

                form::GUIManager::getInstance().registerCallback("chat.title.set", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                    if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                        return ll::makeStringError("chat.title.set: must take exactly one string parameter");

                    return this->setTitle(player, std::get<std::string>(args->elements[0]))
                        .transform([this, &player]() -> void {
                            this->getLogger()->info(LOICollectionAPI::CallbackUtils::getInstance().translate(tr({}, "chat.log1"), player));
                        });
                });

                form::GUIManager::getInstance().registerCallback("chat.blacklist.remove", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                    if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                        return ll::makeStringError("chat.blacklist.remove: must take exactly one string parameter");

                    return this->delBlacklist(player, std::get<std::string>(args->elements[0]))
                        .or_else([](ll::Error e) -> ll::Expected<void> {
                            if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == makeErrorCode(ChatPluginErrorCode::BlacklistNotFound))
                                return {};

                            return ll::Unexpected(e);
                        });
                });
            });
    }

    void ChatPlugin::listenEvent() {
        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        this->mImpl->PlayerJoinEventListener = eventBus.emplaceListener<ll::event::PlayerJoinEvent>([this](ll::event::PlayerJoinEvent& event) mutable -> void {
            if (event.self().isSimulatedPlayer())
                return;

            std::string uuid = event.self().getUuid().asString();

            this->mImpl->db2->has("Chat", uuid)
                .and_then([this, uuid, name = event.self().getRealName()](bool exists) -> ll::Expected<void> {
                    if (!exists) {
                        std::unordered_map<std::string, std::string> mData = {
                            { "name", name },
                            { "title", "None" }
                        };

                        return this->mImpl->db2->set("Chat", uuid, mData);
                    }

                    return {};
                })
                .or_else(modules::defaultErrorHandler<ChatPlugin>);
        
        });
        this->mImpl->PlayerChatEventListener = eventBus.emplaceListener<ll::event::PlayerChatEvent>([this](ll::event::PlayerChatEvent& event) -> void {
            if (event.self().isSimulatedPlayer())
                return;

            MutePlugin::getShared()->isMute(event.self())
                .transform([this, &event](bool exists) -> void {
                    if (exists)
                        return;

                    event.cancel();

                    std::string mChat = LOICollectionAPI::CallbackUtils::getInstance().translate(this->mImpl->options.FormatText, event.self());
                    
                    TextPacket packet = TextPacket::createChat({}, 
                        fmt::format(fmt::runtime(mChat), event.message()), 
                        {}, event.self().getXuid(), {}
                    );

                    ll::service::getLevel()->forEachPlayer([this, &packet, &player = event.self()](Player& mTarget) -> bool {
                        if (mTarget.isSimulatedPlayer())
                            return true;

                        this->getBlacklist(mTarget, player)
                            .transform([&packet, &mTarget](const std::string& id) -> void {
                                if (!id.empty())
                                    return;

                                packet.sendTo(mTarget);
                            })
                            .or_else(modules::defaultErrorHandler<ChatPlugin>);
                        
                        return true;
                    });
                })
                .or_else(modules::defaultErrorHandler<ChatPlugin>);
        }, ll::event::EventPriority::Normal);
    }

    void ChatPlugin::unlistenEvent() {
        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        eventBus.removeListener(this->mImpl->PlayerJoinEventListener);
        eventBus.removeListener(this->mImpl->PlayerChatEventListener);
    }

    ll::Expected<void> ChatPlugin::setTitle(Player& player, const std::string& text) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(ChatPluginErrorCode::Invalid));

        return this->mImpl->db2->set("Chat", player.getUuid().asString(), "title", text);
    }

    ll::Expected<void> ChatPlugin::addTitle(Player& player, const std::string& text, int time) {
        if (!this->isValid()) 
            return ll::makeErrorCodeError(makeErrorCode(ChatPluginErrorCode::Invalid));

        std::string mTismestamp = SystemUtils::getCurrentTimestamp();

        std::unordered_map<std::string, std::string> mData = {
            { "title", text },
            { "author", player.getUuid().asString() },
            { "time", time ? SystemUtils::toTimeCalculate(SystemUtils::getNowTime(), time * 60, "None") : "None" }
        };

        return this->getDatabase()->set("Titles", mTismestamp, mData)
            .transform([this, text, &player]() -> void {
                this->getLogger()->info(fmt::runtime(LOICollectionAPI::CallbackUtils::getInstance().translate(tr({}, "chat.log2"), player)), text);
            });
    }

    ll::Expected<void> ChatPlugin::addBlacklist(Player& player, Player& target) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(ChatPluginErrorCode::Invalid));

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
                this->getLogger()->info(fmt::runtime(LOICollectionAPI::CallbackUtils::getInstance().translate(tr({}, "chat.log5"), player)), mTargetObject);

                if (this->mImpl->BlacklistCache.contains(mObject)) {
                    this->mImpl->BlacklistCache.update(mObject, [mTismestamp](std::shared_ptr<std::vector<std::string>> mList) -> void {
                        mList->push_back(mTismestamp);
                    });
                }
            });
    }

    ll::Expected<void> ChatPlugin::delTitle(Player& player, const std::string& text) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(ChatPluginErrorCode::Invalid));

        std::string uuid = player.getUuid().asString();

        return this->hasTitle(player, text)
            .and_then([this, text, uuid](bool exists) -> ll::Expected<std::string> {
                if (!exists) {
                    this->getLogger()->warn(fmt::runtime(tr({}, "console.log.error.object")), this->getName());

                    return ll::makeErrorCodeError(makeErrorCode(ChatPluginErrorCode::TitleNotFound));
                }

                return this->getDatabase()->find("Titles", {
                    { "title", text },
                    { "author", uuid }
                }, "", SQLiteStorage::FindCondition::AND);
            })
            .and_then([this](const std::string& id) -> ll::Expected<void> {
                if (id.empty())
                    return {};

                return this->getDatabase()->del("Titles", id);
            })
            .and_then([this, text, uuid, &player]() -> ll::Expected<void> {
                if (this->mImpl->db2->get("Chat", uuid, "title", "None") == text)
                    return this->setTitle(player, "None");

                return {};
            })
            .transform([this, text, &player]() -> void {
                this->getLogger()->info(fmt::runtime(LOICollectionAPI::CallbackUtils::getInstance().translate(tr({}, "chat.log3"), player)), text);
            });
    }

    ll::Expected<void> ChatPlugin::delBlacklist(Player& player, const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(ChatPluginErrorCode::Invalid));

        return this->hasBlacklist(player, id)
            .and_then([this, id, &player](bool exists) -> ll::Expected<void> {
                if (!exists) {
                    this->getLogger()->warn(fmt::runtime(tr({}, "console.log.error.object")), this->getName());

                    return ll::makeErrorCodeError(makeErrorCode(ChatPluginErrorCode::BlacklistNotFound));
                }

                return this->getDatabase()->del("Blacklist", id)
                    .transform([this, id, &player]() -> void { 
                        this->getLogger()->info(fmt::runtime(LOICollectionAPI::CallbackUtils::getInstance().translate(tr({}, "chat.log6"), player)), id);

                        this->mImpl->BlacklistCache.update(player.getUuid().asString(), [id](std::shared_ptr<std::vector<std::string>> mList) -> void {
                            mList->erase(std::remove(mList->begin(), mList->end(), id), mList->end());
                        });
                    });
            });
    }

    ll::Expected<std::string> ChatPlugin::getTitle(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(ChatPluginErrorCode::Invalid));

        std::string uuid = player.getUuid().asString();

        return this->mImpl->db2->get("Chat", uuid, "title", "None")
            .and_then([this, uuid, &player](const std::string& title) -> ll::Expected<std::string> {
                return this->getDatabase()->find("Titles", {
                    { "title", title },
                    { "author", uuid }
                }, "", SQLiteStorage::FindCondition::AND)
                    .and_then([this, title, &player](const std::string& id) -> ll::Expected<std::string> {
                        auto data = this->getDatabase()->get("Titles", id);
                        if (!data.has_value())
                            return ll::Unexpected(data.error());

                        if (data.value().empty())
                            return "None";

                        if (SystemUtils::isPastOrPresent(data.value().at("time"))) {
                            auto result = this->setTitle(player, "None").and_then([this, id]() -> ll::Expected<void> {
                                return this->getDatabase()->del("Titles", id);
                            });

                            if (!result.has_value())
                                return ll::Unexpected(result.error());

                            return "None";
                        }

                        return title;
                    });
            });
    }

    ll::Expected<std::string> ChatPlugin::getTitleTime(Player& player, const std::string& text) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(ChatPluginErrorCode::Invalid));

        return this->getDatabase()->find("Titles", {
            { "title", text },
            { "author", player.getUuid().asString() }
        }, "", SQLiteStorage::FindCondition::AND)
            .and_then([this](const std::string& id) -> ll::Expected<std::string> {
                if (id.empty())
                    return "None";

                return this->getDatabase()->get("Titles", id, "title", "None");
            });
    }

    ll::Expected<std::string> ChatPlugin::getBlacklist(Player& player, Player& target) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(ChatPluginErrorCode::Invalid));

        return this->getDatabase()->find("Blacklist", {
            { "target", target.getUuid().asString() },
            { "author", player.getUuid().asString() }
        }, "", SQLiteStorage::FindCondition::AND);
    }

    ll::Expected<std::vector<std::string>> ChatPlugin::getTitles(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(ChatPluginErrorCode::Invalid));

        return this->getDatabase()->find("Titles", "title", {
            { "author", player.getUuid().asString() }
        });
    }

    ll::Expected<std::vector<std::string>> ChatPlugin::getBlacklist(Player& player) {
        if (!this->isValid()) 
            return ll::makeErrorCodeError(makeErrorCode(ChatPluginErrorCode::Invalid));

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

    ll::Expected<std::unordered_map<std::string, std::string>> ChatPlugin::getBlacklistData(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(ChatPluginErrorCode::Invalid));

        return this->getDatabase()->get("Blacklist", id);
    }

    ll::Expected<bool> ChatPlugin::hasTitle(Player& player, const std::string& text) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(ChatPluginErrorCode::Invalid));

        return this->getDatabase()->find("Titles", {
            { "title", text },
            { "author", player.getUuid().asString() }
        }, "", SQLiteStorage::FindCondition::AND)
            .transform([](const std::string& id) -> bool {
                return !id.empty(); 
            });
    }

    ll::Expected<bool> ChatPlugin::hasBlacklist(Player& player, const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(ChatPluginErrorCode::Invalid));

        std::string mObject = player.getUuid().asString();
        if (this->mImpl->BlacklistCache.contains(mObject)) {
            auto mKeys = this->mImpl->BlacklistCache.get(mObject).value();
            return std::find(mKeys->begin(), mKeys->end(), id) != mKeys->end();
        }

        return this->getDatabase()->has("Blacklist", id);
    }

    bool ChatPlugin::isValid() {
        return this->getLogger() != nullptr && this->getDatabase() != nullptr && this->mImpl->db2 != nullptr;
    }

    int ChatPlugin::getBlacklistUpload() {
        return this->mImpl->options.BlacklistUpload;
    }

    std::string ChatPlugin::getName() {
        return "ChatPlugin";
    }

    modules::ModulePriority ChatPlugin::getPriority() {
        return modules::ModulePriority::High;
    }

    ll::Expected<bool> ChatPlugin::load() {
        if (!ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Chat.ModuleEnabled)
            return false;

        auto mDataPath = std::filesystem::path(ServiceProvider::getInstance().getService<std::string>("DataPath")->data());

        this->mImpl->db = std::make_shared<SQLiteStorage>((mDataPath / "chat.db").string());
        this->mImpl->db2 = ServiceProvider::getInstance().getService<SQLiteStorage>("SettingsDB");
        this->mImpl->logger = ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA");
        this->mImpl->options = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Chat;
        this->mImpl->mGuiPath = (std::filesystem::path(ServiceProvider::getInstance().getService<std::string>("GuiPath")->data()) / "chat.lcui").string();

        return true;
    }

    ll::Expected<bool> ChatPlugin::unload() {
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

    ll::Expected<bool> ChatPlugin::registry() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        return this->mImpl->db2->create("Chat", [](SQLiteStorage::ColumnCallback ctor) -> void {
            ctor("name");
            ctor("title");
        }).and_then([this]() -> ll::Expected<void> {
            return this->getDatabase()->create("Blacklist", [](SQLiteStorage::ColumnCallback ctor) -> void {
                ctor("name");
                ctor("target");
                ctor("author");
                ctor("time");
            });
        }).and_then([this]() -> ll::Expected<void> {
            return this->getDatabase()->create("Titles", [](SQLiteStorage::ColumnCallback ctor) -> void {
                ctor("title");
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

    ll::Expected<bool> ChatPlugin::unregistry() {
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
