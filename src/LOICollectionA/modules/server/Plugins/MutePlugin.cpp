#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <ranges>
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
#include <ll/api/event/player/PlayerChatEvent.h>

#include <mc/deps/core/string/HashedString.h>

#include <mc/world/level/Level.h>
#include <mc/world/actor/ActorDefinitionIdentifier.h>
#include <mc/world/actor/player/Player.h>

#include <mc/server/commands/CommandOrigin.h>
#include <mc/server/commands/CommandOutput.h>
#include <mc/server/commands/CommandSelector.h>
#include <mc/server/commands/CommandPermissionLevel.h>
#include <mc/server/commands/CommandOutputMessageType.h>

#include "LOICollectionA/frontend/AST.h"

#include "LOICollectionA/include/form/GUIManager.h"

#include "LOICollectionA/include/CallbackUtils.h"
#include "LOICollectionA/include/server/Plugins/LanguagePlugin.h"

#include "LOICollectionA/include/server/Events/modules/MuteEvent.h"

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/data/SQLiteStorage.h"

#include "LOICollectionA/base/Wrapper.h"
#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/MutePlugin.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    enum class MuteObject;

    constexpr inline auto MuteObjectName = ll::command::enum_name_v<MuteObject>;

    struct MutePlugin::operation {
        ll::command::SoftEnum<MuteObject> Object;

        CommandSelector<Player> Target;
        
        std::string Cause;
        int Time = 0;
        int Limit = 100;
    };

    struct MutePlugin::Impl {
        std::atomic<bool> mRegistered{ false };

        bool ModuleEnabled = false;

        std::shared_ptr<SQLiteStorage> db;
        std::shared_ptr<ll::io::Logger> logger;

        std::string mGuiPath;
        
        ll::event::ListenerPtr PlayerChatEventListener;
        ll::event::ListenerPtr MuteAddEventListener;
        ll::event::ListenerPtr MuteRemoveEventListener;
    };

    MutePlugin::MutePlugin() : mImpl(std::make_unique<Impl>()) {};
    MutePlugin::~MutePlugin() = default;

    std::shared_ptr<MutePlugin> MutePlugin::getShared() {
        static auto instance = std::shared_ptr<MutePlugin>(new MutePlugin());
        return instance;
    }

    std::error_code MutePlugin::makeErrorCode(MutePluginErrorCode e) {
        static MutePluginErrorCategory cat;
        return std::error_code{ static_cast<int>(e), cat };
    }
    
    std::shared_ptr<SQLiteStorage> MutePlugin::getDatabase() {
        return this->mImpl->db;
    }

    std::shared_ptr<ll::io::Logger> MutePlugin::getLogger() {
        return this->mImpl->logger;
    }

    void MutePlugin::registeryCommand() {
        this->getMutes()
            .transform([](std::vector<std::string> mutes) -> void {
                ll::command::CommandRegistrar::getInstance(false).tryRegisterSoftEnum(MuteObjectName, std::move(mutes));
            })
            .or_else(modules::defaultErrorHandler<MutePlugin>);

        ll::command::CommandHandle& command = ll::command::CommandRegistrar::getInstance(false)
            .getOrCreateCommand("mute", tr({}, "commands.mute.description"), CommandPermissionLevel::GameDirectors, CommandFlagValue::NotCheat | CommandFlagValue::Async);
        command.overload<operation>().text("add").required("Target").optional("Cause").optional("Time").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                CommandSelectorResults<Player> results = param.Target.results(origin);
                if (results.empty())
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));

                for (Player*& pl : results) {
                    bool exists = false;

                    auto result = this->isMute(*pl);
                    if (!result.has_value()) {
                        if (!result.error().isA<ll::ErrorCodeError>() || result.error().as<ll::ErrorCodeError>().ec != makeErrorCode(MutePluginErrorCode::PermissionDenied)) {
                            modules::defaultErrorHandler<MutePlugin>(result.error());
                            return;
                        }

                        exists = true;
                    }

                    if (exists || pl->getCommandPermissionLevel() >= CommandPermissionLevel::GameDirectors || pl->isSimulatedPlayer()) {
                        output.error(fmt::runtime(tr(origin.getLocaleCode(), "commands.mute.error.add")), pl->getRealName());
                        continue;
                    }

                    this->addMute(*pl, param.Cause, param.Time).or_else(modules::defaultErrorHandler<MutePlugin>);

                    output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.mute.success.add")), pl->getRealName());
                }
            });
        command.overload<operation>().text("remove").text("target").required("Target").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                CommandSelectorResults<Player> results = param.Target.results(origin);
                if (results.empty())
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
                
                for (Player*& pl : results) {
                    bool exists = false;

                    auto result = this->isMute(*pl);
                    if (!result.has_value()) {
                        if (!result.error().isA<ll::ErrorCodeError>() || result.error().as<ll::ErrorCodeError>().ec != makeErrorCode(MutePluginErrorCode::PermissionDenied)) {
                            modules::defaultErrorHandler<MutePlugin>(result.error());
                            return;
                        }

                        exists = true;
                    }

                    if (!exists) {
                        output.error(fmt::runtime(tr(origin.getLocaleCode(), "commands.mute.error.remove")), pl->getRealName());
                        continue;
                    }

                    this->delMute(*pl)
                        .or_else([](ll::Error e) -> ll::Expected<void> {
                            if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == makeErrorCode(MutePluginErrorCode::NotFound))
                                return {};

                            return ll::Unexpected(e);
                        })
                        .or_else(modules::defaultErrorHandler<MutePlugin>);

                    output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.mute.success.remove")), pl->getRealName());
                }
            });
        command.overload<operation>().text("remove").text("id").required("Object").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                this->hasMute(param.Object)
                    .and_then([this, &output, &origin, target = param.Object](bool exists) -> ll::Expected<void> {
                        if (!exists) {
                            output.error(tr(origin.getLocaleCode(), "commands.mute.error.remove"));
                            return {};
                        }

                        return this->delMute(target).transform([&output, &origin, target]() -> void { 
                            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.mute.success.remove")), target);
                        });
                    })
                    .or_else([](ll::Error e) -> ll::Expected<void> {
                        if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == makeErrorCode(MutePluginErrorCode::NotFound))
                            return {};

                        return ll::Unexpected(e);
                    })
                    .or_else(modules::defaultErrorHandler<MutePlugin>);
            });
        command.overload<operation>().text("info").required("Object").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                this->getDatabase()->get("Mute", param.Object)
                    .transform([&output, &origin, target = param.Object](std::unordered_map<std::string, std::string> data) -> void {
                        if (data.empty()) {
                            output.error(tr(origin.getLocaleCode(), "commands.mute.error.info"));
                            return;
                        }

                        output.success(tr(origin.getLocaleCode(), "commands.mute.success.info"));
                        for (auto& pair : data) {
                            std::string key = pair.first.substr(pair.first.find_first_of('.') + 1);

                            output.success("{0}: {1}", key, pair.second);
                        }
                    })
                    .or_else(modules::defaultErrorHandler<MutePlugin>);
            });
        command.overload<operation>().text("list").optional("Limit").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                this->getMutes(param.Limit)
                    .transform([&output, &origin, limit = param.Limit](std::vector<std::string> mutes) -> void {
                        if (mutes.empty())
                            return output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.mute.success.list")), limit, "None");

                        output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.mute.success.list")), limit, fmt::join(mutes, ", "));
                    })
                    .or_else(modules::defaultErrorHandler<MutePlugin>);
            });
        command.overload().text("gui").execute([](CommandOrigin const& origin, CommandOutput& output) -> void {
            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);
            
            form::GUIManager::getInstance().open("mute", "mute.open", form::GUIManagerType::CustomForm, player)
                .or_else(modules::defaultErrorHandler<MutePlugin>);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
        command.overload().text("reload").execute([this](CommandOrigin const& origin, CommandOutput& output) -> void {
            output.success(tr(origin.getLocaleCode(), "commands.generic.reload"));
            
            form::GUIManager::getInstance().load("mute", this->mImpl->mGuiPath)
                .transform([&origin, &output]() -> void {
                    output.success(tr(origin.getLocaleCode(), "commands.generic.reload.success"));
                })
                .or_else(modules::defaultErrorHandler<MutePlugin>);
        });
    }

    ll::Expected<void> MutePlugin::registeryUI() {
        return form::GUIManager::getInstance().load("mute", this->mImpl->mGuiPath)
            .transform([this]() -> void {
                form::GUIManager::getInstance().registerValue("mute.players", [](Player&) -> frontend::ArrayRef {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    ll::service::getLevel()->forEachPlayer([&values](Player& target) -> bool {
                        if (!target.isSimulatedPlayer())
                            values->elements.emplace_back(target.getRealName());

                        return true;
                    });

                    return values;
                });

                form::GUIManager::getInstance().registerValue("mute.mutes", [this](Player&) -> ll::Expected<frontend::ArrayRef> {
                    return this->getMutes()
                        .transform([](const std::vector<std::string>& mutes) -> frontend::ArrayRef {
                            auto values = std::make_shared<frontend::ArrayValue>();
                            values->elements = mutes
                                | std::ranges::to<std::vector<frontend::TypedValue>>();

                            return values;
                        });
                });

                form::GUIManager::getInstance().registerRequest("mute.info", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                        return ll::makeStringError("mute.info: must take exactly one string parameter");

                    auto id = std::get<std::string>(args->elements[0]);

                    return LanguagePlugin::getShared()->getLanguage(player)
                        .and_then([this, id](const std::string&) -> ll::Expected<frontend::ArrayRef> {
                            return this->hasMute(id)
                                .and_then([this, id](bool exists) -> ll::Expected<frontend::ArrayRef> {
                                    if (!exists) {
                                        return std::make_shared<frontend::ArrayValue>();
                                    }

                                    auto data = this->getMuteData(id);
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

                form::GUIManager::getInstance().registerRequest("mute.online", [](frontend::ArrayRef args, Player&) -> ll::Expected<frontend::ArrayRef> {
                    if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                        return ll::makeStringError("mute.online: must take exactly one string parameter");

                    auto values = std::make_shared<frontend::ArrayValue>();
                    values->elements.emplace_back(
                        ll::service::getLevel()->getPlayer(std::get<std::string>(args->elements[0])) != nullptr
                    );

                    return values;
                });

                form::GUIManager::getInstance().registerCallback("mute.add", [](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                    if (args->elements.size() != 1 || !std::holds_alternative<frontend::ObjectRef>(args->elements[0]))
                        return ll::makeStringError("mute.add: must take exactly one PaginatedFormResult parameter");

                    auto result = std::get<frontend::ObjectRef>(args->elements[0]);

                    Player* target = ll::service::getLevel()->getPlayer(std::get<std::string>(result->at("selection")));
                    if (!target) {
                        return LanguagePlugin::getShared()->getLanguage(player)
                            .and_then([&player](const std::string& language) -> ll::Expected<void> {
                                player.sendMessage(tr(language, "mute.gui.error"));

                                return {};
                            });
                    }

                    return {};
                });

                form::GUIManager::getInstance().registerCallback("mute.submit", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                    if (args->elements.empty())
                        return {};

                    if (!std::ranges::all_of(args->elements, [](const auto& e) {
                        return std::holds_alternative<std::string>(e);
                    }) || args->elements.size() != 3) {
                        return ll::makeStringError("mute.submit: must take exactly three string parameters");
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
                                player.sendMessage(tr(language, "mute.gui.error"));

                                return {};
                            });
                    }

                    return this->addMute(*target, cause, SystemUtils::toInt(std::get<std::string>(args->elements[2]), 0));
                });

                form::GUIManager::getInstance().registerCallback("mute.remove", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                    if (args->elements.size() != 1 || !std::holds_alternative<frontend::ObjectRef>(args->elements[0]))
                        return ll::makeStringError("mute.remove: must take exactly one PaginatedFormResult parameter");

                    auto result = std::get<frontend::ObjectRef>(args->elements[0]);

                    if (auto closeReason = std::get_if<int>(&result->at("closeReason"));
                        closeReason && *closeReason == static_cast<int>(ll::ui::ScreenSession::Result::value_type::UserBusy))
                        return {};

                    std::string selection = std::get<std::string>(result->at("selection"));
                    if (selection.empty())
                        return {};

                    return this->hasMute(selection)
                        .and_then([&player](bool exists) -> ll::Expected<void> {
                            if (!exists) {
                                return LanguagePlugin::getShared()->getLanguage(player)
                                    .and_then([&player](const std::string& language) -> ll::Expected<void> {
                                        player.sendMessage(tr(language, "mute.gui.error"));

                                        return {};
                                    });
                            }

                            return {};
                        });
                });

                form::GUIManager::getInstance().registerCallback("mute.info.remove", [this](frontend::ArrayRef args, Player&) -> ll::Expected<void> {
                    if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                        return ll::makeStringError("mute.info.remove: must take exactly one string parameter");

                    return this->delMute(std::get<std::string>(args->elements[0]))
                        .or_else([](ll::Error e) -> ll::Expected<void> {
                            if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == makeErrorCode(MutePluginErrorCode::NotFound))
                                return {};

                            return ll::Unexpected(e);
                        });
                });
            });
    }

    void MutePlugin::listenEvent() {
        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        this->mImpl->PlayerChatEventListener = eventBus.emplaceListener<ll::event::PlayerChatEvent>([this](ll::event::PlayerChatEvent& event) -> void {
            this->getMute(event.self())
                .and_then([this, &event](const std::string& id) -> ll::Expected<void> {
                    if (id.empty())
                        return {};

                    return this->getDatabase()->get("Mute", id)
                        .and_then([this, id, &event](std::unordered_map<std::string, std::string> data) -> ll::Expected<void> {
                            if (SystemUtils::isPastOrPresent(data.at("time")))
                                return this->delMute(event.self());

                            auto language = LanguagePlugin::getShared()->getLanguage(event.self());
                            if (!language.has_value())
                                return ll::Unexpected(language.error());
                            
                            event.self().sendMessage(fmt::format(
                                fmt::runtime(tr(language.value(), "mute.tips")), 
                                data.at("cause"), 
                                SystemUtils::toFormatTime(data.at("time"), "None")
                            ));
                            event.cancel();

                            return {};
                        });
                })
                .or_else([](ll::Error e) -> ll::Expected<void> {
                    if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == makeErrorCode(MutePluginErrorCode::NotFound))
                        return {};

                    return ll::Unexpected(e);
                })
                .or_else(modules::defaultErrorHandler<MutePlugin>);
        }, ll::event::EventPriority::Highest);

        this->mImpl->MuteAddEventListener = eventBus.emplaceListener<LOICollection::server::Events::MuteAddAfterEvent>([this](LOICollection::server::Events::MuteAddAfterEvent& event) -> void {
            this->getMute(event.self())
                .transform([](std::string id) -> void {
                    if (id.empty())
                        return;

                    ll::command::CommandRegistrar::getInstance(false).addSoftEnumValues(MuteObjectName, { id });
                })
                .or_else(modules::defaultErrorHandler<MutePlugin>);
        });

        this->mImpl->MuteRemoveEventListener = eventBus.emplaceListener<LOICollection::server::Events::MuteRemoveEvent>([](LOICollection::server::Events::MuteRemoveEvent& event) -> void {
            ll::command::CommandRegistrar::getInstance(false).removeSoftEnumValues(MuteObjectName, { event.getTarget() });
        });
    }

    void MutePlugin::unlistenEvent() {
        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        eventBus.removeListener(this->mImpl->PlayerChatEventListener);
        eventBus.removeListener(this->mImpl->MuteAddEventListener);
        eventBus.removeListener(this->mImpl->MuteRemoveEventListener);
    }

    ll::Expected<void> MutePlugin::addMute(Player& player, const std::string& cause, int time) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MutePluginErrorCode::Invalid));

        if (player.getCommandPermissionLevel() >= CommandPermissionLevel::GameDirectors)
            return ll::makeErrorCodeError(makeErrorCode(MutePluginErrorCode::PermissionDenied));

        std::string mCause = cause.empty() ? "None" : cause;
        std::string mTimestamp = SystemUtils::getCurrentTimestamp();

        std::unordered_map<std::string, std::string> mData = {
            { "name", player.getRealName() },
            { "cause", mCause },
            { "time", time ? SystemUtils::toTimeCalculate(SystemUtils::getNowTime(), time * 60, "0") : "0" },
            { "subtime", SystemUtils::getNowTime("%Y%m%d%H%M%S") },
            { "data", player.getUuid().asString() }
        };

        return this->getDatabase()->set("Mute", mTimestamp, mData)
            .transform([this, mCause, &player]() -> void {
                this->getLogger()->info(fmt::runtime(LOICollectionAPI::CallbackUtils::getInstance().translate(tr({}, "mute.log1"), player)), mCause);
            });
    }

    ll::Expected<void> MutePlugin::delMute(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MutePluginErrorCode::Invalid));

        return this->getMute(player)
            .and_then([this](const std::string& id) -> ll::Expected<void> {
                return this->delMute(id);
            });
    }

    ll::Expected<void> MutePlugin::delMute(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MutePluginErrorCode::Invalid));

        return this->hasMute(id)
            .and_then([this, id](bool exists) -> ll::Expected<void> {
                if (!exists) {
                    this->getLogger()->error(fmt::runtime(tr({}, "console.log.error.object")), this->getName());

                    return ll::makeErrorCodeError(makeErrorCode(MutePluginErrorCode::NotFound));
                }

                return this->getDatabase()->del("Mute", id);
            })
            .transform([this, id]() -> void {
                this->getLogger()->info(fmt::runtime(tr({}, "mute.log2")), id);
            });
    }

    ll::Expected<std::string> MutePlugin::getMute(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MutePluginErrorCode::Invalid));

        return this->getDatabase()->find("Mute", {
            { "data", player.getUuid().asString() }
        }, "", SQLiteStorage::FindCondition::AND);
    }

    ll::Expected<std::vector<std::string>> MutePlugin::getMutes(int limit) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MutePluginErrorCode::Invalid));
        
        return this->getDatabase()->list("Mute")
            .transform([limit](const std::vector<std::string>& keys) -> std::vector<std::string> {
                return keys
                    | std::views::take(limit > 0 ? limit : static_cast<int>(keys.size()))
                    | std::ranges::to<std::vector<std::string>>();
            });
    }

    ll::Expected<std::unordered_map<std::string, std::string>> MutePlugin::getMuteData(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MutePluginErrorCode::Invalid));

        return this->getDatabase()->get("Mute", id);
    }

    ll::Expected<bool> MutePlugin::hasMute(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MutePluginErrorCode::Invalid));

        return this->getDatabase()->has("Mute", id);
    }

    ll::Expected<bool> MutePlugin::isMute(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MutePluginErrorCode::Invalid));

        return this->getMute(player)
            .transform([](const std::string& id) -> bool {
                return !id.empty();
            });
    }

    bool MutePlugin::isValid() {
        return this->getLogger() != nullptr && this->getDatabase() != nullptr;
    }

    std::string MutePlugin::getName() {
        return "MutePlugin";
    }

    modules::ModulePriority MutePlugin::getPriority() {
        return modules::ModulePriority::High;
    }

    ll::Expected<bool> MutePlugin::load() {
        if (!ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Mute)
            return false;

        auto mDataPath = std::filesystem::path(ServiceProvider::getInstance().getService<std::string>("DataPath")->data());

        this->mImpl->db = std::make_shared<SQLiteStorage>((mDataPath / "mute.db").string());
        this->mImpl->logger = ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA");
        this->mImpl->ModuleEnabled = true;
        this->mImpl->mGuiPath = (std::filesystem::path(ServiceProvider::getInstance().getService<std::string>("GuiPath")->data()) / "mute.lcui").string();

        return true;
    }

    ll::Expected<bool> MutePlugin::unload() {
        if (!this->mImpl->ModuleEnabled)
            return false;

        this->mImpl->db.reset();
        this->mImpl->logger.reset();
        this->mImpl->ModuleEnabled = false;

        if (this->mImpl->mRegistered.load(std::memory_order_acquire))
            this->unlistenEvent();

        return true;
    }

    ll::Expected<bool> MutePlugin::registry() {
        if (!this->mImpl->ModuleEnabled)
            return false;

        return this->getDatabase()->create("Mute", [](SQLiteStorage::ColumnCallback ctor) -> void {
            ctor("name");
            ctor("cause");
            ctor("time");
            ctor("subtime");
            ctor("data");
        }).and_then([this]() -> ll::Expected<void> {
            return this->registeryUI();
        }).transform([this]() -> bool {
            this->registeryCommand();
            this->listenEvent();

            this->mImpl->mRegistered.store(true, std::memory_order_release);

            return true;
        });
    }
    
    ll::Expected<bool> MutePlugin::unregistry() {
        if (!this->mImpl->ModuleEnabled)
            return false;

        this->unlistenEvent();

        return this->getDatabase()->exec("VACUUM;")
            .transform([this]() -> bool {
                this->mImpl->mRegistered.store(false, std::memory_order_release);

                return true;
            });
    }
}
