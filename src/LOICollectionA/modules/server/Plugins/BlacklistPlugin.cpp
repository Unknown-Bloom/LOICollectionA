#include <atomic>
#include <memory>
#include <vector>
#include <ranges>
#include <string>
#include <filesystem>
#include <unordered_map>

#include <fmt/core.h>

#include <ll/api/Expected.h>
#include <ll/api/io/Logger.h>
#include <ll/api/io/LoggerRegistry.h>
#include <ll/api/ui/base/ScreenSession.h>
#include <ll/api/service/Bedrock.h>
#include <ll/api/command/Command.h>
#include <ll/api/command/CommandHandle.h>
#include <ll/api/command/CommandRegistrar.h>
#include <ll/api/command/EnumName.h>
#include <ll/api/event/EventBus.h>
#include <ll/api/event/ListenerBase.h>

#include <mc/deps/certificates/WebToken.h>
#include <mc/deps/core/string/HashedString.h>

#include <mc/world/level/Level.h>
#include <mc/world/actor/ActorDefinitionIdentifier.h>
#include <mc/world/actor/player/Player.h>

#include <mc/network/ConnectionRequest.h>
#include <mc/network/NetworkIdentifier.h>
#include <mc/network/MinecraftPacketIds.h>
#include <mc/network/ServerNetworkHandler.h>
#include <mc/network/packet/TextPacket.h>
#include <mc/network/packet/LoginPacket.h>
#include <mc/network/packet/DisconnectPacket.h>
#include <mc/network/connection/DisconnectFailReason.h>

#include <mc/server/ServerPlayer.h>
#include <mc/server/commands/CommandOrigin.h>
#include <mc/server/commands/CommandOutput.h>
#include <mc/server/commands/CommandSelector.h>
#include <mc/server/commands/CommandPermissionLevel.h>
#include <mc/server/commands/CommandOutputMessageType.h>

#include <mc/common/SubClientId.h>

#include "LOICollectionA/include/CallbackUtils.h"
#include "LOICollectionA/include/server/Plugins/LanguagePlugin.h"

#include "LOICollectionA/include/server/Events/modules/BlacklistEvent.h"
#include "LOICollectionA/include/server/Events/server/NetworkPacketEvent.h"

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/data/SQLiteStorage.h"

#include "LOICollectionA/frontend/AST.h"

#include "LOICollectionA/include/form/GUIManager.h"

#include "LOICollectionA/base/Wrapper.h"
#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/BlacklistPlugin.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    enum class BlacklistObject;

    constexpr inline auto BlacklistObjectName = ll::command::enum_name_v<BlacklistObject>;

    struct BlacklistPlugin::operation {
        ll::command::SoftEnum<BlacklistObject> Object;
        
        CommandSelector<Player> Target;

        std::string Cause;
        int Time = 0;
        int Limit = 100;
    };

    struct BlacklistPlugin::Impl {
        std::atomic<bool> mRegistered{ false };

        Config::C_Blacklist options;
        
        std::shared_ptr<SQLiteStorage> db;
        std::shared_ptr<ll::io::Logger> logger;

        std::string mGuiPath;

        ll::event::ListenerPtr NetworkPacketEventListener;
        ll::event::ListenerPtr BlacklistAddEventListener;
        ll::event::ListenerPtr BlacklistRemoveEventListener;
    };

    BlacklistPlugin::BlacklistPlugin() : mImpl(std::make_unique<Impl>()) {};
    BlacklistPlugin::~BlacklistPlugin() = default;

    std::shared_ptr<BlacklistPlugin> BlacklistPlugin::getShared() {
        static auto instance = std::shared_ptr<BlacklistPlugin>(new BlacklistPlugin());
        return instance;
    }

    std::error_code BlacklistPlugin::makeErrorCode(BlacklistPluginErrorCode e) {
        static BlacklistPluginErrorCategory cat;
        return std::error_code{ static_cast<int>(e), cat };
    }
    
    std::shared_ptr<SQLiteStorage> BlacklistPlugin::getDatabase() {
        return this->mImpl->db;
    }

    std::shared_ptr<ll::io::Logger> BlacklistPlugin::getLogger() {
        return this->mImpl->logger;
    }

    void BlacklistPlugin::registeryCommand() {
        this->getBlacklists()
            .transform([](std::vector<std::string> blacklists) -> void {
                ll::command::CommandRegistrar::getInstance(false).tryRegisterSoftEnum(BlacklistObjectName, std::move(blacklists));
            })
            .or_else(modules::defaultErrorHandler<BlacklistPlugin>);

        ll::command::CommandHandle& command = ll::command::CommandRegistrar::getInstance(false)
            .getOrCreateCommand("blacklist", tr({}, "commands.blacklist.description"), CommandPermissionLevel::GameDirectors, CommandFlagValue::NotCheat | CommandFlagValue::Async);
        command.overload<operation>().text("add").required("Target").optional("Cause").optional("Time").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                CommandSelectorResults<Player> results = param.Target.results(origin);
                if (results.empty())
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));

                for (Player*& pl : results) {
                    if (pl->getCommandPermissionLevel() >= CommandPermissionLevel::GameDirectors || pl->isSimulatedPlayer()) {
                        output.error(fmt::runtime(tr(origin.getLocaleCode(), "commands.blacklist.error.add")), pl->getRealName());
                        continue;
                    }

                    this->addBlacklist(*pl, param.Cause, param.Time).or_else(modules::defaultErrorHandler<BlacklistPlugin>);

                    output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.blacklist.success.add")), pl->getRealName());
                }
            });
        command.overload<operation>().text("remove").required("Object").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                this->hasBlacklist(param.Object)
                    .and_then([this, &output, &origin, target = param.Object](bool exists) -> ll::Expected<void> {
                        if (!exists) {
                            output.error(fmt::runtime(tr(origin.getLocaleCode(), "commands.blacklist.error.remove")), target);
                            return {};
                        }

                        return this->delBlacklist(target).transform([&output, &origin, target]() -> void { 
                            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.blacklist.success.remove")), target);
                        });
                    })
                    .or_else([](ll::Error e) -> ll::Expected<void> {
                        if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == makeErrorCode(BlacklistPluginErrorCode::NotFound))
                            return {};

                        return ll::Unexpected(e);
                    })
                    .or_else(modules::defaultErrorHandler<BlacklistPlugin>);
            });
        command.overload<operation>().text("info").required("Object").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                this->getDatabase()->get("Blacklist", param.Object)
                    .transform([&output, &origin, target = param.Object](std::unordered_map<std::string, std::string> data) -> void {
                        if (data.empty()) {
                            output.error(tr(origin.getLocaleCode(), "commands.blacklist.error.info"));
                            return;
                        }

                        output.success(tr(origin.getLocaleCode(), "commands.blacklist.success.info"));
                        for (auto& pair : data) {
                            std::string key = pair.first.substr(pair.first.find_first_of('.') + 1);

                            output.success("{0}: {1}", key, pair.second);
                        }
                    })
                    .or_else(modules::defaultErrorHandler<BlacklistPlugin>);
            });
        command.overload<operation>().text("list").optional("Limit").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                this->getBlacklists(param.Limit)
                    .transform([&output, &origin, limit = param.Limit](std::vector<std::string> blacklists) -> void {
                        if (blacklists.empty())
                            return output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.blacklist.success.list")), limit, "None");

                        output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.blacklist.success.list")), limit, fmt::join(blacklists, ", "));
                    })
                    .or_else(modules::defaultErrorHandler<BlacklistPlugin>);
            });
        command.overload().text("reload").execute([this](CommandOrigin const& origin, CommandOutput& output) -> void {
            output.success(tr(origin.getLocaleCode(), "commands.generic.reload"));
            
            form::GUIManager::getInstance().load("blacklist", this->mImpl->mGuiPath)
                .transform([&origin, &output]() -> void {
                    output.success(tr(origin.getLocaleCode(), "commands.generic.reload.success"));
                })
                .or_else(modules::defaultErrorHandler<BlacklistPlugin>);
        });
        command.overload().text("gui").execute([](CommandOrigin const& origin, CommandOutput& output) -> void {
            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);
            
            form::GUIManager::getInstance().open("blacklist", "blacklist.open", form::GUIManagerType::CustomForm, player)
                .or_else(modules::defaultErrorHandler<BlacklistPlugin>);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
    }

    ll::Expected<void> BlacklistPlugin::registeryUI() {
        return form::GUIManager::getInstance().load("blacklist", this->mImpl->mGuiPath)
            .transform([this]() -> void {
                form::GUIManager::getInstance().registerValue("blacklist.players", [](Player&) -> frontend::ArrayRef {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    ll::service::getLevel()->forEachPlayer([&values](Player& target) -> bool {
                        if (!target.isSimulatedPlayer())
                            values->elements.emplace_back(target.getRealName());

                        return true;
                    });

                    return values;
                });

                form::GUIManager::getInstance().registerValue("blacklist.blacklists", [this](Player&) -> ll::Expected<frontend::ArrayRef> {
                    return this->getBlacklists()
                        .transform([](const std::vector<std::string>& blacklists) -> frontend::ArrayRef {
                            auto values = std::make_shared<frontend::ArrayValue>();
                            values->elements = blacklists
                                | std::ranges::to<std::vector<frontend::TypedValue>>();

                            return values;
                        });
                });

                form::GUIManager::getInstance().registerRequest("blacklist.info", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                        return ll::makeStringError("blacklist.add: must take exactly one string parameter");

                    auto id = std::get<std::string>(args->elements[0]);

                    return LanguagePlugin::getShared()->getLanguage(player)
                        .and_then([this, id](const std::string&) -> ll::Expected<frontend::ArrayRef> {
                            return this->hasBlacklist(id)
                                .and_then([this, id](bool exists) -> ll::Expected<frontend::ArrayRef> { 
                                    if (!exists) {
                                        return std::make_shared<frontend::ArrayValue>();
                                    }

                                    auto data = this->getBlacklistData(id);
                                    if (!data.has_value())
                                        return ll::Unexpected(data.error());

                                    auto info = std::make_shared<frontend::ArrayValue>();

                                    if (data.value().empty())
                                        return info;

                                    info->elements.emplace_back(id);
                                    info->elements.emplace_back(data.value().at("name"));
                                    info->elements.emplace_back(data.value().at("cause"));
                                    info->elements.emplace_back(SystemUtils::toFormatTime(data.value().at("subtime"), "None"));
                                    info->elements.emplace_back(SystemUtils::toFormatTime(data.value().at("time"), "None"));

                                    return info;
                                });
                        });
                });

                form::GUIManager::getInstance().registerRequest("blacklist.online", [](frontend::ArrayRef args, Player&) -> ll::Expected<frontend::ArrayRef> {
                    if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                        return ll::makeStringError("blacklist.online: must take exactly one string parameter");

                    auto values = std::make_shared<frontend::ArrayValue>();
                    values->elements.emplace_back(
                        ll::service::getLevel()->getPlayer(std::get<std::string>(args->elements[0])) != nullptr
                    );

                    return values;
                });

                form::GUIManager::getInstance().registerCallback("blacklist.add", [](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                    if (args->elements.size() != 1 || !std::holds_alternative<frontend::ObjectRef>(args->elements[0]))
                        return ll::makeStringError("blacklist.add: must take exactly one PaginatedFormResult parameter");

                    auto result = std::get<frontend::ObjectRef>(args->elements[0]);

                    Player* target = ll::service::getLevel()->getPlayer(std::get<std::string>(result->at("selection")));
                    if (!target) {
                        return LanguagePlugin::getShared()->getLanguage(player)
                            .and_then([&player](const std::string& language) -> ll::Expected<void> {
                                player.sendMessage(tr(language, "blacklist.gui.error"));

                                return {};
                            });
                    }

                    return {};
                });

                form::GUIManager::getInstance().registerCallback("blacklist.submit", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                    if (args->elements.empty())
                        return {};
                    
                    if (!std::ranges::all_of(args->elements, [](const auto& e) {
                        return std::holds_alternative<std::string>(e);
                    }) || args->elements.size() != 3) {
                        return ll::makeStringError("blacklist.submit: must take exactly three string parameters");
                    }

                    const std::string& cause = std::get<std::string>(args->elements[1]);
                    if (cause.empty()) {
                        return LanguagePlugin::getShared()->getLanguage(player)
                            .and_then([&player](const std::string& language) -> ll::Expected<void> {
                                player.sendMessage(tr(language, "generic.tips.noinput"));

                                return {};
                            });
                    }

                    Player* target = ll::service::getLevel()->getPlayer(std::get<std::string>(args->elements[0]));
                    if (!target) {
                        return LanguagePlugin::getShared()->getLanguage(player)
                            .and_then([&player](const std::string& language) -> ll::Expected<void> {
                                player.sendMessage(tr(language, "blacklist.gui.error"));

                                return {};
                            });
                    }

                    return this->addBlacklist(*target, cause, SystemUtils::toInt(std::get<std::string>(args->elements[2]), 0));
                });

                form::GUIManager::getInstance().registerCallback("blacklist.remove", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                    if (args->elements.size() != 1 || !std::holds_alternative<frontend::ObjectRef>(args->elements[0]))
                        return ll::makeStringError("blacklist.add: must take exactly one PaginatedFormResult parameter");

                    auto result = std::get<frontend::ObjectRef>(args->elements[0]);

                    if (std::get<int>(result->at("closeReason")) == static_cast<int>(ll::ui::ScreenSession::Result::value_type::UserBusy))
                        return {};

                    std::string selection = std::get<std::string>(result->at("selection"));
                    if (selection.empty())
                        return {};

                    return this->hasBlacklist(selection)
                        .and_then([ &player](bool exists) -> ll::Expected<void> {
                            if (!exists) {
                                return LanguagePlugin::getShared()->getLanguage(player)
                                    .and_then([&player](const std::string& language) -> ll::Expected<void> {
                                        player.sendMessage(tr(language, "blacklist.gui.error"));

                                        return {};
                                    });
                            }

                            return {};
                        });
                });

                form::GUIManager::getInstance().registerCallback("blacklist.info.remove", [this](frontend::ArrayRef args, Player&) -> ll::Expected<void> {
                    if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                        return ll::makeStringError("blacklist.info.remove: must take exactly one string parameter");

                    return this->delBlacklist(std::get<std::string>(args->elements[0]))
                        .or_else([](ll::Error e) -> ll::Expected<void> {
                            if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == makeErrorCode(BlacklistPluginErrorCode::NotFound))
                                return {};

                            return ll::Unexpected(e);
                        });
                });
            });
    }

    void BlacklistPlugin::listenEvent() {
        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        this->mImpl->NetworkPacketEventListener = eventBus.emplaceListener<LOICollection::server::Events::NetworkPacketBeforeEvent>([this](LOICollection::server::Events::NetworkPacketBeforeEvent& event) -> void {
            if (event.getPacket().getId() != MinecraftPacketIds::Login)
                return;

            std::string mUuid = static_cast<LoginPacket const&>(event.getPacket()).mConnectionRequest->mRawToken->mDataInfo.get("extraData", {}).get("identity", "None").asString("None");
            std::string mIp = event.getNetworkIdentifier().getIPAndPort().substr(0, event.getNetworkIdentifier().getIPAndPort().find_last_of(':'));
            std::string mClientId = static_cast<LoginPacket const&>(event.getPacket()).mConnectionRequest->getDeviceId();

            this->getDatabase()->find("Blacklist", {
                { "data_uuid", mUuid },
                { "data_ip", mIp },
                { "data_clientid", mClientId }
            }, "", SQLiteStorage::FindCondition::OR)
                .and_then([this, mUuid, &event](const std::string& id) -> ll::Expected<void> {
                    if (id.empty())
                        return {};

                    return this->getDatabase()->get("Blacklist", id)
                        .and_then([this, id, mUuid, &event](std::unordered_map<std::string, std::string> data) -> ll::Expected<void> {
                            if (SystemUtils::isPastOrPresent(data.at("time")))
                                return this->delBlacklist(id);

                            auto language = LanguagePlugin::getShared()->getLanguage(mUuid);
                            if (!language.has_value())
                                return ll::Unexpected(language.error());
                                
                            ll::service::getServerNetworkHandler()->disconnectClientWithMessage(
                                event.getNetworkIdentifier(), event.getSubClientId(), Connection::DisconnectFailReason::Kicked,
                                fmt::format(fmt::runtime(tr(language.value(), "blacklist.tips")),
                                    SystemUtils::toFormatTime(data.at("time"), "None"), data.at("cause")
                                ),
                                std::nullopt
                            );

                            return {};
                        });
                })
                .or_else([](ll::Error e) -> ll::Expected<void> {
                    if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == makeErrorCode(BlacklistPluginErrorCode::NotFound))
                        return {};

                    return ll::Unexpected(e);
                })
                .or_else(modules::defaultErrorHandler<BlacklistPlugin>);
        });

        this->mImpl->BlacklistAddEventListener = eventBus.emplaceListener<LOICollection::server::Events::BlacklistAddBeforeEvent>([this](LOICollection::server::Events::BlacklistAddBeforeEvent& event) -> void {
            this->getBlacklist(event.self())
                .transform([](const std::string& id) -> void {
                    if (id.empty())
                        return;

                    ll::command::CommandRegistrar::getInstance(false).addSoftEnumValues(BlacklistObjectName, { id });
                })
                .or_else(modules::defaultErrorHandler<BlacklistPlugin>);
        });

        this->mImpl->BlacklistRemoveEventListener = eventBus.emplaceListener<LOICollection::server::Events::BlacklistRemoveEvent>([](LOICollection::server::Events::BlacklistRemoveEvent& event) -> void {
            ll::command::CommandRegistrar::getInstance(false).removeSoftEnumValues(BlacklistObjectName, { event.getTarget() });
        });
    }

    void BlacklistPlugin::unlistenEvent() {
        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        eventBus.removeListener(this->mImpl->NetworkPacketEventListener);
        eventBus.removeListener(this->mImpl->BlacklistAddEventListener);
        eventBus.removeListener(this->mImpl->BlacklistRemoveEventListener);
    }

    ll::Expected<void> BlacklistPlugin::addBlacklist(Player& player, const std::string& cause, int time) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(BlacklistPluginErrorCode::Invalid));

        if (player.getCommandPermissionLevel() >= CommandPermissionLevel::GameDirectors)
            return ll::makeErrorCodeError(makeErrorCode(BlacklistPluginErrorCode::PermissionDenied));

        std::string mCause = cause.empty() ? "None" : cause;
        std::string mTismestamp = SystemUtils::getCurrentTimestamp();

        std::unordered_map<std::string, std::string> mData = {
            { "name", player.getRealName() },
            { "cause", mCause },
            { "time", time ? SystemUtils::toTimeCalculate(SystemUtils::getNowTime(), time * 60, "None") : "None" },
            { "subtime", SystemUtils::getNowTime("%Y%m%d%H%M%S") },
            { "data_uuid", player.getUuid().asString() },
            { "data_ip", player.getIPAndPort().substr(0, player.getIPAndPort().find_last_of(':')) },
            { "data_clientid", player.getConnectionRequest().transform(&ConnectionRequest::getDeviceId).value_or("None") }
        };

        return this->getDatabase()->set("Blacklist", mTismestamp, mData)
            .and_then([&player]() -> ll::Expected<std::string> {
                return LanguagePlugin::getShared()->getLanguage(player);
            })
            .and_then([this, mTismestamp, mCause, &player](const std::string& language) -> ll::Expected<void> {
                auto time = this->getDatabase()->get("Blacklist", mTismestamp, "time");
                if (!time.has_value())
                    return ll::Unexpected(time.error());

                ll::service::getServerNetworkHandler()->disconnectClientWithMessage(
                    player.getNetworkIdentifier(), player.getClientSubId(), Connection::DisconnectFailReason::Unknown,
                    fmt::format(fmt::runtime(tr(language, "blacklist.tips")),
                        SystemUtils::toFormatTime(time.value(), "None"),
                        mCause
                    ),
                    std::nullopt
                );

                if (this->mImpl->options.BroadcastMessage) {
                    ll::service::getLevel()->forEachPlayer([&player](Player& target) -> bool {
                        LanguagePlugin::getShared()->getLanguage(target)
                            .and_then([&player, &target](const std::string& language) -> ll::Expected<void> {
                                TextPacket::createRawMessage(
                                    LOICollectionAPI::CallbackUtils::getInstance().translate(tr(language, "blacklist.broadcast"), player)
                                ).sendTo(target);

                                return {};
                            })
                            .or_else(modules::defaultErrorHandler<BlacklistPlugin>);

                        return true;
                    });
                }

                this->getLogger()->info(LOICollectionAPI::CallbackUtils::getInstance().translate(tr({}, "blacklist.log1"), player));

                return {};
            });
    }

    ll::Expected<void> BlacklistPlugin::delBlacklist(const std::string& id) {
        if (!this->isValid()) 
            return ll::makeErrorCodeError(makeErrorCode(BlacklistPluginErrorCode::Invalid));

        return this->hasBlacklist(id)
            .and_then([this, id](bool exists) -> ll::Expected<void> {
                if (!exists) {
                    this->getLogger()->error(fmt::runtime(tr({}, "console.log.error.object")), this->getName());

                    return ll::makeErrorCodeError(makeErrorCode(BlacklistPluginErrorCode::NotFound));
                }

                return this->getDatabase()->del("Blacklist", id);  
            })
            .transform([this, id]() -> void {
                this->getLogger()->info(fmt::runtime(tr({}, "blacklist.log2")), id);
            });
    }

    ll::Expected<std::string> BlacklistPlugin::getBlacklist(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(BlacklistPluginErrorCode::Invalid));

        return this->getDatabase()->find("Blacklist", {
            { "data_uuid", player.getUuid().asString() },
            { "data_ip", player.getIPAndPort().substr(0, player.getIPAndPort().find_last_of(':')) },
            { "data_clientid", player.getConnectionRequest().transform(&ConnectionRequest::getDeviceId).value_or("None") }
        }, "", SQLiteStorage::FindCondition::OR);
    }

    ll::Expected<std::unordered_map<std::string, std::string>> BlacklistPlugin::getBlacklistData(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(BlacklistPluginErrorCode::Invalid));

        return this->getDatabase()->get("Blacklist", id);
    }

    ll::Expected<std::vector<std::string>> BlacklistPlugin::getBlacklists(int limit) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(BlacklistPluginErrorCode::Invalid));

        return this->getDatabase()->list("Blacklist")
            .transform([limit](const std::vector<std::string>& keys) -> std::vector<std::string> {
                return keys
                    | std::views::take(limit > 0 ? limit : static_cast<int>(keys.size()))
                    | std::ranges::to<std::vector<std::string>>();
            });
    }

    ll::Expected<bool> BlacklistPlugin::hasBlacklist(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(BlacklistPluginErrorCode::Invalid));

        return this->getDatabase()->has("Blacklist", id);
    }

    ll::Expected<bool> BlacklistPlugin::isBlacklist(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(BlacklistPluginErrorCode::Invalid));

        return this->getBlacklist(player)
            .transform([](const std::string& id) -> bool {
                return !id.empty();
            });
    }

    bool BlacklistPlugin::isValid() {
        return this->getLogger() != nullptr && this->getDatabase() != nullptr;
    }

    std::string BlacklistPlugin::getName() {
        return "BlacklistPlugin";
    }

    modules::ModulePriority BlacklistPlugin::getPriority() {
        return modules::ModulePriority::High;
    }

    ll::Expected<bool> BlacklistPlugin::load() {
        if (!ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Blacklist.ModuleEnabled)
            return false;

        auto mDataPath = std::filesystem::path(ServiceProvider::getInstance().getService<std::string>("DataPath")->data());

        this->mImpl->db = std::make_shared<SQLiteStorage>((mDataPath / "blacklist.db").string());
        this->mImpl->logger = ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA");
        this->mImpl->options = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Blacklist;
        this->mImpl->mGuiPath = (std::filesystem::path(ServiceProvider::getInstance().getService<std::string>("GuiPath")->data()) / "blacklist.lcui").string();

        return true;
    }

    ll::Expected<bool> BlacklistPlugin::unload() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        this->mImpl->db.reset();
        this->mImpl->logger.reset();
        this->mImpl->options = {};

        if (this->mImpl->mRegistered.load(std::memory_order_acquire))
            this->unlistenEvent();

        return true;
    }

    ll::Expected<bool> BlacklistPlugin::registry() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        return this->getDatabase()->create("Blacklist", [](SQLiteStorage::ColumnCallback ctor) -> void {
            ctor("name");
            ctor("cause");
            ctor("time");
            ctor("subtime");
            ctor("data_uuid");
            ctor("data_ip");
            ctor("data_clientid");
        }).and_then([this]() -> ll::Expected<void> {
            return this->registeryUI();
        }).transform([this]() -> bool {
            this->registeryCommand();
            this->listenEvent();

            this->mImpl->mRegistered.store(true, std::memory_order_release);

            return true;
        });
    }

    ll::Expected<bool> BlacklistPlugin::unregistry() {
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
