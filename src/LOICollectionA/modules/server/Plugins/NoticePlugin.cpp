#include <atomic>
#include <memory>
#include <ranges>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <ll/api/io/Logger.h>
#include <ll/api/io/LoggerRegistry.h>
#include <ll/api/command/Command.h>
#include <ll/api/command/CommandHandle.h>
#include <ll/api/command/CommandRegistrar.h>
#include <ll/api/command/EnumName.h>
#include <ll/api/event/EventBus.h>
#include <ll/api/event/ListenerBase.h>
#include <ll/api/event/player/PlayerJoinEvent.h>
#include <ll/api/utils/HashUtils.h>

#include <mc/world/actor/player/Player.h>
#include <mc/server/commands/CommandOrigin.h>
#include <mc/server/commands/CommandOutput.h>
#include <mc/server/commands/CommandPermissionLevel.h>

#include "LOICollectionA/include/CallbackUtils.h"
#include "LOICollectionA/include/server/Plugins/LanguagePlugin.h"
#include "LOICollectionA/include/server/Events/modules/NoticeEvent.h"

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/data/JsonStorage.h"
#include "LOICollectionA/data/SQLiteStorage.h"

#include "LOICollectionA/frontend/AST.h"

#include "LOICollectionA/include/form/GUIManager.h"

#include "LOICollectionA/base/Cache.h"
#include "LOICollectionA/base/Wrapper.h"
#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/NoticePlugin.h"

using I18nUtilsTools::tr;

namespace {
    std::vector<std::string> getSortedNoticeIds(JsonStorage& db) {
        nlohmann::ordered_json data = db.get();

        std::vector<std::pair<std::string, int>> entries;
        entries.reserve(data.size());
        for (auto it = data.begin(); it != data.end(); ++it)
            entries.emplace_back(it.key(), it.value().value("priority", 0));

        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            return a.second < b.second;
        });

        std::vector<std::string> ids;
        ids.reserve(entries.size());
        for (const auto& [id, priority] : entries)
            ids.push_back(id);

        return ids;
    }
}

namespace LOICollection::server::Plugins {
    enum class NoticeObject;

    constexpr inline auto NoticeObjectName = ll::command::enum_name_v<NoticeObject>;

    struct NoticePlugin::operation {
        ll::command::SoftEnum<NoticeObject> Object;
    };

    struct NoticePlugin::Impl {
        LRUKCache<std::string, bool> CloseCache;

        std::atomic<bool> mRegistered{ false };

        bool ModuleEnabled = false;

        std::shared_ptr<JsonStorage> db;
        std::shared_ptr<SQLiteStorage> db2;
        std::shared_ptr<ll::io::Logger> logger;

        std::string mGuiPath;
        
        ll::event::ListenerPtr PlayerJoinEventListener;
        ll::event::ListenerPtr NoticeCreateEventListener;
        ll::event::ListenerPtr NoticeDeleteEventListener;

        Impl() : CloseCache(100, 100) {}
    };

    NoticePlugin::NoticePlugin() : mImpl(std::make_unique<Impl>()) {};
    NoticePlugin::~NoticePlugin() = default;

    std::shared_ptr<NoticePlugin> NoticePlugin::getShared() {
        static auto instance = std::shared_ptr<NoticePlugin>(new NoticePlugin());
        return instance;
    }

    std::error_code NoticePlugin::makeErrorCode(NoticePluginErrorCode e) {
        static NoticePluginErrorCategory cat;
        return std::error_code{ static_cast<int>(e), cat };
    }
    
    std::shared_ptr<JsonStorage> NoticePlugin::getDatabase() {
        return this->mImpl->db;
    }

    std::shared_ptr<ll::io::Logger> NoticePlugin::getLogger() {
        return this->mImpl->logger;
    }

    void NoticePlugin::registeryCommand() {
        ll::command::CommandRegistrar::getInstance(false).tryRegisterSoftEnum(NoticeObjectName, this->getDatabase()->keys());

        ll::command::CommandHandle& command = ll::command::CommandRegistrar::getInstance(false)
            .getOrCreateCommand("notice", tr({}, "commands.notice.description"), CommandPermissionLevel::Any, CommandFlagValue::NotCheat | CommandFlagValue::Async);
        command.overload<operation>().text("gui").optional("Object").execute(
            [](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                Actor* entity = origin.getEntity();
                if (entity == nullptr || !entity->isType(ActorType::Player))
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
                Player& player = *static_cast<Player*>(entity);

                auto ctx = std::make_shared<frontend::ArrayValue>();
                if (param.Object.empty()) {
                    ctx->elements.emplace_back("");
                    ctx->elements.emplace_back("");

                    form::GUIManager::getInstance().open("notice", "notice.open", form::GUIManagerType::PaginatedForm, player, ctx)
                        .or_else(modules::defaultErrorHandler<NoticePlugin>);
                } else {
                    ctx->elements.emplace_back("view");
                    ctx->elements.emplace_back(param.Object);

                    form::GUIManager::getInstance().open("notice", "notice.view", form::GUIManagerType::CustomForm, player, ctx)
                        .or_else(modules::defaultErrorHandler<NoticePlugin>);
                }

                output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
            });
        command.overload().text("edit").execute([](CommandOrigin const& origin, CommandOutput& output) -> void {
            if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors)
                return output.error(tr(origin.getLocaleCode(), "commands.generic.permission"));

            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);

            auto ctx = std::make_shared<frontend::ArrayValue>();
            ctx->elements.emplace_back("");
            ctx->elements.emplace_back("");

            form::GUIManager::getInstance().open("notice", "notice.edit", form::GUIManagerType::CustomForm, player, ctx)
                .or_else(modules::defaultErrorHandler<NoticePlugin>);

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

            form::GUIManager::getInstance().open("notice", "notice.setting", form::GUIManagerType::CustomForm, player, ctx)
                .or_else(modules::defaultErrorHandler<NoticePlugin>);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
        command.overload().text("reload").execute([this](CommandOrigin const& origin, CommandOutput& output) -> void {
            if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors)
                return output.error(tr(origin.getLocaleCode(), "commands.generic.permission"));

            output.success(tr(origin.getLocaleCode(), "commands.generic.reload"));
            
            form::GUIManager::getInstance().load("notice", this->mImpl->mGuiPath)
                .transform([&origin, &output]() -> void {
                    output.success(tr(origin.getLocaleCode(), "commands.generic.reload.success"));
                })
                .or_else(modules::defaultErrorHandler<NoticePlugin>);
        });
    }

    ll::Expected<void> NoticePlugin::registeryUI() {
        return form::GUIManager::getInstance().load("notice", this->mImpl->mGuiPath)
            .transform([this]() -> void {
                form::GUIManager::getInstance().registerValue("notice.keys", [this](Player&) -> frontend::ArrayRef {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    for (const std::string& key : this->getDatabase()->keys())
                        values->elements.emplace_back(key);

                    return values;
                });

                form::GUIManager::getInstance().registerValue("notice.names", [this](Player& player) -> ll::Expected<frontend::ArrayRef> {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    for (const std::string& id : getSortedNoticeIds(*this->getDatabase())) {
                        auto title = this->getDatabase()->get_ptr<std::string>("/" + id + "/title").value_or("");
                        values->elements.emplace_back(LOICollectionAPI::CallbackUtils::getInstance().translate(title, player));
                    }

                    return values;
                });

                form::GUIManager::getInstance().registerValue("notice.close", [this](Player& player) -> ll::Expected<frontend::ArrayRef> {
                    return this->isClose(player)
                        .transform([](bool value) -> frontend::ArrayRef {
                            auto values = std::make_shared<frontend::ArrayValue>();
                            values->elements.emplace_back(value);

                            return values;
                        });
                });

                form::GUIManager::getInstance().registerRequest("notice.view", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    if (args->elements.size() != 1)
                        return ll::makeStringError("notice.view: must take exactly one parameter");

                    return LanguagePlugin::getShared()->getLanguage(player)
                        .and_then([this, args, &player](const std::string&) -> ll::Expected<frontend::ArrayRef> {
                            std::string id;

                            if (std::holds_alternative<int>(args->elements[0])) {
                                auto ids = getSortedNoticeIds(*this->getDatabase());
                                int index = std::get<int>(args->elements[0]);
                                if (index < 0 || index >= static_cast<int>(ids.size()))
                                    return ll::makeStringError("notice.view: index out of range");

                                id = ids.at(static_cast<size_t>(index));
                            } else if (std::holds_alternative<std::string>(args->elements[0])) {
                                id = std::get<std::string>(args->elements[0]);
                            } else {
                                return ll::makeStringError("notice.view: must take an int or string parameter");
                            }

                            if (!this->getDatabase()->has(id))
                                return ll::makeStringError("notice.view: notice does not exist");

                            auto title = this->getDatabase()->get_ptr<std::string>("/" + id + "/title").value_or("");
                            auto content = this->getDatabase()->get_ptr<nlohmann::ordered_json>("/" + id + "/content").value_or(nlohmann::ordered_json::array());

                            std::string body;
                            for (const auto& line : content) {
                                if (!body.empty())
                                    body += "\n";

                                body += LOICollectionAPI::CallbackUtils::getInstance().translate(line.get<std::string>(), player);
                            }

                            auto values = std::make_shared<frontend::ArrayValue>();
                            values->elements.emplace_back(LOICollectionAPI::CallbackUtils::getInstance().translate(title, player));
                            values->elements.emplace_back(body);

                            return values;
                        });
                });

                form::GUIManager::getInstance().registerRequest("notice.content.data", [this](frontend::ArrayRef args, Player&) -> ll::Expected<frontend::ArrayRef> {
                    if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                        return ll::makeStringError("notice.content.data: must take exactly one string parameter");

                    auto id = std::get<std::string>(args->elements[0]);
                    if (!this->getDatabase()->has(id))
                        return ll::makeStringError("notice.content.data: notice does not exist");

                    auto values = std::make_shared<frontend::ArrayValue>();
                    values->elements.emplace_back(this->getDatabase()->get_ptr<std::string>("/" + id + "/title").value_or(""));
                    values->elements.emplace_back(this->getDatabase()->get_ptr<bool>("/" + id + "/poiontout").value_or(false));

                    return values;
                });

                form::GUIManager::getInstance().registerRequest("notice.lines", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                        return ll::makeStringError("notice.lines: must take exactly one string parameter");

                    auto id = std::get<std::string>(args->elements[0]);
                    if (!this->getDatabase()->has(id))
                        return ll::makeStringError("notice.lines: notice does not exist");

                    return LanguagePlugin::getShared()->getLanguage(player)
                        .and_then([this, id](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                            auto content = this->getDatabase()->get_ptr<nlohmann::ordered_json>("/" + id + "/content").value_or(nlohmann::ordered_json::array());
                            auto values = std::make_shared<frontend::ArrayValue>();

                            for (const auto& [index, line] : std::views::enumerate(content)) {
                                std::string text = line.is_string() ? line.get<std::string>() : "";
                                std::string display = fmt::format(fmt::runtime(tr(language, "notice.gui.edit.line")), index + 1);

                                if (!text.empty())
                                    display += ": " + text;

                                values->elements.emplace_back(display);
                            }

                            return values;
                        });
                });

                form::GUIManager::getInstance().registerRequest("notice.line.data", [this](frontend::ArrayRef args, Player&) -> ll::Expected<frontend::ArrayRef> {
                    if (args->elements.size() != 2 ||
                        !std::holds_alternative<std::string>(args->elements[0]) ||
                        !std::holds_alternative<int>(args->elements[1]))
                        return ll::makeStringError("notice.line.data: must take a string and an int parameter");

                    auto id = std::get<std::string>(args->elements[0]);
                    int index = std::get<int>(args->elements[1]);

                    auto content = this->getDatabase()->get_ptr<nlohmann::ordered_json>("/" + id + "/content").value_or(nlohmann::ordered_json::array());
                    if (index < 0 || index >= static_cast<int>(content.size()))
                        return ll::makeStringError("notice.line.data: index out of range");

                    auto values = std::make_shared<frontend::ArrayValue>();
                    values->elements.emplace_back(content.at(static_cast<size_t>(index)).is_string()
                        ? content.at(static_cast<size_t>(index)).get<std::string>()
                        : std::string(""));

                    return values;
                });

                form::GUIManager::getInstance().registerRequest("notice.auto", [this](frontend::ArrayRef, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    return LanguagePlugin::getShared()->getLanguage(player)
                        .and_then([this, &player](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                            nlohmann::ordered_json data = this->getDatabase()->get();

                            std::vector<std::pair<std::string, int>> entries;
                            for (auto it = data.begin(); it != data.end(); ++it) {
                                if (!it.value().value("poiontout", false))
                                    continue;

                                entries.emplace_back(it.key(), it.value().value("priority", 0));
                            }

                            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
                                return a.second < b.second;
                            });

                            std::string body;
                            for (const auto& [id, priority] : entries) {
                                auto title = data.at(id).value("title", "");
                                auto content = data.at(id).value("content", nlohmann::ordered_json::array());

                                if (!body.empty())
                                    body += "\n\n";

                                body += LOICollectionAPI::CallbackUtils::getInstance().translate(title, player);
                                for (const auto& line : content)
                                    body += "\n" + LOICollectionAPI::CallbackUtils::getInstance().translate(line.get<std::string>(), player);
                            }

                            auto values = std::make_shared<frontend::ArrayValue>();
                            values->elements.emplace_back(tr(language, "notice.gui.title"));
                            values->elements.emplace_back(body);

                            return values;
                        });
                });

                form::GUIManager::getInstance().registerCallback("notice.setting.save", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                    if (args->elements.size() != 1 || !std::holds_alternative<bool>(args->elements[0]))
                        return ll::makeStringError("notice.setting.save: must take exactly one bool parameter");

                    return this->setClose(player, std::get<bool>(args->elements[0]));
                });

                form::GUIManager::getInstance().registerCallback("notice.add.submit", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                    if (args->elements.size() != 4 ||
                        !std::holds_alternative<std::string>(args->elements[0]) ||
                        !std::holds_alternative<std::string>(args->elements[1]) ||
                        !std::holds_alternative<std::string>(args->elements[2]) ||
                        !std::holds_alternative<bool>(args->elements[3]))
                        return ll::makeStringError("notice.add.submit: must take three string and one bool parameters");

                    auto id = std::get<std::string>(args->elements[0]);
                    auto title = std::get<std::string>(args->elements[1]);
                    if (id.empty() || title.empty()) {
                        return LanguagePlugin::getShared()->getLanguage(player)
                            .and_then([&player](const std::string& language) -> ll::Expected<void> {
                                player.sendMessage(tr(language, "generic.tips.noinput"));

                                return {};
                            });
                    }

                    int priority = SystemUtils::toInt(std::get<std::string>(args->elements[2]), 0);
                    bool show = std::get<bool>(args->elements[3]);

                    return this->create(id, title, priority, show)
                        .transform([this, id, &player]() -> void {
                            this->getLogger()->info(fmt::runtime(LOICollectionAPI::CallbackUtils::getInstance().translate(tr({}, "notice.log2"), player)), id);
                        });
                });

                form::GUIManager::getInstance().registerCallback("notice.content.save", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                    if (args->elements.size() != 3 ||
                        !std::holds_alternative<std::string>(args->elements[0]) ||
                        !std::holds_alternative<std::string>(args->elements[1]) ||
                        !std::holds_alternative<bool>(args->elements[2]))
                        return ll::makeStringError("notice.content.save: must take two string and one bool parameters");

                    auto id = std::get<std::string>(args->elements[0]);

                    this->getDatabase()->set_ptr("/" + id + "/title", std::get<std::string>(args->elements[1]));
                    this->getDatabase()->set_ptr("/" + id + "/poiontout", std::get<bool>(args->elements[2]));

                    return this->getDatabase()->save()
                        .transform([this, &player]() -> void {
                            this->getLogger()->info(LOICollectionAPI::CallbackUtils::getInstance().translate(tr({}, "notice.log1"), player));
                        });
                });

                form::GUIManager::getInstance().registerCallback("notice.line.add", [this](frontend::ArrayRef args, Player&) -> ll::Expected<void> {
                    if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                        return ll::makeStringError("notice.line.add: must take exactly one string parameter");

                    auto id = std::get<std::string>(args->elements[0]);
                    auto content = this->getDatabase()->get_ptr<nlohmann::ordered_json>("/" + id + "/content").value_or(nlohmann::ordered_json::array());

                    content.push_back("");
                    this->getDatabase()->set_ptr("/" + id + "/content", content);

                    return this->getDatabase()->save();
                });

                form::GUIManager::getInstance().registerCallback("notice.line.remove", [this](frontend::ArrayRef args, Player&) -> ll::Expected<void> {
                    if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                        return ll::makeStringError("notice.line.remove: must take exactly one string parameter");

                    auto id = std::get<std::string>(args->elements[0]);
                    auto content = this->getDatabase()->get_ptr<nlohmann::ordered_json>("/" + id + "/content").value_or(nlohmann::ordered_json::array());

                    if (!content.empty())
                        content.erase(content.end() - 1);

                    this->getDatabase()->set_ptr("/" + id + "/content", content);

                    return this->getDatabase()->save();
                });

                form::GUIManager::getInstance().registerCallback("notice.line.save", [this](frontend::ArrayRef args, Player&) -> ll::Expected<void> {
                    if (args->elements.size() != 3 ||
                        !std::holds_alternative<std::string>(args->elements[0]) ||
                        !std::holds_alternative<int>(args->elements[1]) ||
                        !std::holds_alternative<std::string>(args->elements[2]))
                        return ll::makeStringError("notice.line.save: must take two string and one int parameters");

                    auto id = std::get<std::string>(args->elements[0]);
                    int index = std::get<int>(args->elements[1]);
                    auto content = this->getDatabase()->get_ptr<nlohmann::ordered_json>("/" + id + "/content").value_or(nlohmann::ordered_json::array());

                    if (index < 0 || index >= static_cast<int>(content.size()))
                        return ll::makeStringError("notice.line.save: index out of range");

                    content.at(static_cast<size_t>(index)) = std::get<std::string>(args->elements[2]);
                    this->getDatabase()->set_ptr("/" + id + "/content", content);

                    return this->getDatabase()->save();
                });

                form::GUIManager::getInstance().registerCallback("notice.remove", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<void> {
                    if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                        return ll::makeStringError("notice.remove: must take exactly one string parameter");

                    auto id = std::get<std::string>(args->elements[0]);

                    return this->remove(id)
                        .transform([this, id, &player]() -> void {
                            this->getLogger()->info(fmt::runtime(LOICollectionAPI::CallbackUtils::getInstance().translate(tr({}, "notice.log3"), player)), id);
                        });
                });
            });
    }

    void NoticePlugin::listenEvent() {
        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        this->mImpl->PlayerJoinEventListener = eventBus.emplaceListener<ll::event::PlayerJoinEvent>([this](ll::event::PlayerJoinEvent& event) mutable -> void {
            if (event.self().isSimulatedPlayer())
                return;

            std::string uuid = event.self().getUuid().asString();

            this->mImpl->db2->has("Notice", uuid)
                .and_then([this, uuid, name = event.self().getRealName()](bool exists) -> ll::Expected<void> {
                    if (!exists) {
                        std::unordered_map<std::string, std::string> data = {
                            { "name", name },
                            { "close", "false" }
                        };

                        return this->mImpl->db2->set("Notice", uuid, data);
                    }

                    return {};
                })
                .or_else(modules::defaultErrorHandler<NoticePlugin>);
            
            this->isClose(event.self())
                .and_then([this, &event](bool exists) -> ll::Expected<void> {
                    if (exists || !this->hasEnabledNotice())
                        return {};

                    auto ctx = std::make_shared<frontend::ArrayValue>();
                    ctx->elements.emplace_back("auto");
                    ctx->elements.emplace_back("");

                    return form::GUIManager::getInstance().open("notice", "notice.auto", form::GUIManagerType::CustomForm, event.self(), ctx);
                })
                .or_else(modules::defaultErrorHandler<NoticePlugin>);
        });

        this->mImpl->NoticeCreateEventListener = eventBus.emplaceListener<LOICollection::server::Events::NoticeCreateEvent>([](LOICollection::server::Events::NoticeCreateEvent& event) mutable -> void {
            ll::command::CommandRegistrar::getInstance(false).addSoftEnumValues(NoticeObjectName, { event.getTarget() });
        });

        this->mImpl->NoticeDeleteEventListener = eventBus.emplaceListener<LOICollection::server::Events::NoticeDeleteEvent>([](LOICollection::server::Events::NoticeDeleteEvent& event) mutable -> void {
            ll::command::CommandRegistrar::getInstance(false).removeSoftEnumValues(NoticeObjectName, { event.getTarget() });
        });
    }

    void NoticePlugin::unlistenEvent() {
        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        eventBus.removeListener(this->mImpl->PlayerJoinEventListener);
        eventBus.removeListener(this->mImpl->NoticeCreateEventListener);
        eventBus.removeListener(this->mImpl->NoticeDeleteEventListener);
    }

    ll::Expected<void> NoticePlugin::create(const std::string& id, const std::string& title, int priority, bool poiontout) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(NoticePluginErrorCode::Invalid));

        nlohmann::ordered_json data = {
            { "title", title },
            { "content", nlohmann::ordered_json::array() },
            { "priority", priority },
            { "poiontout", poiontout }
        };

        this->getDatabase()->set(id, data);

        return this->getDatabase()->save();
    }

    ll::Expected<void> NoticePlugin::remove(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(NoticePluginErrorCode::Invalid));

        this->getDatabase()->remove(id);

        return this->getDatabase()->save();
    }

    ll::Expected<void> NoticePlugin::setClose(Player& player, bool enable) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(NoticePluginErrorCode::Invalid));

        return this->mImpl->db2->set("Notice", player.getUuid().asString(), "close", enable ? "true" : "false");
    }

    ll::Expected<bool> NoticePlugin::has(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(NoticePluginErrorCode::Invalid));

        return this->getDatabase()->has(id);
    }

    ll::Expected<bool> NoticePlugin::isClose(Player& player) {
        if (!this->isValid()) 
            return ll::makeErrorCodeError(makeErrorCode(NoticePluginErrorCode::Invalid));

        std::string uuid = player.getUuid().asString();

        if (this->mImpl->CloseCache.contains(uuid))
            return *this->mImpl->CloseCache.get(uuid).value();

        return this->mImpl->db2->get("Notice", uuid, "close", "false")
            .transform([this, uuid](const std::string& value) -> bool {
                bool result = (value == "true");
                
                this->mImpl->CloseCache.put(uuid, result);

                return result;
            });
    }

    bool NoticePlugin::hasEnabledNotice() {
        nlohmann::ordered_json data = this->getDatabase()->get();

        for (auto it = data.begin(); it != data.end(); ++it) {
            if (it.value().value("poiontout", false))
                return true;
        }

        return false;
    }

    bool NoticePlugin::isValid() {
        return this->getLogger() != nullptr && this->getDatabase() != nullptr && this->mImpl->db2 != nullptr;
    }

    std::string NoticePlugin::getName() {
        return "NoticePlugin";
    }

    modules::ModulePriority NoticePlugin::getPriority() {
        return modules::ModulePriority::High;
    }

    ll::Expected<bool> NoticePlugin::load() {
        if (!ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Notice)
            return false;

        auto mDataPath = std::filesystem::path(ServiceProvider::getInstance().getService<std::string>("ConfigPath")->data());

        this->mImpl->db = std::make_shared<JsonStorage>(mDataPath / "notice.json");
        this->mImpl->db2 = ServiceProvider::getInstance().getService<SQLiteStorage>("SettingsDB");
        this->mImpl->logger = ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA");
        this->mImpl->ModuleEnabled = true;
        this->mImpl->mGuiPath = (std::filesystem::path(ServiceProvider::getInstance().getService<std::string>("GuiPath")->data()) / "notice.lcui").string();

        return this->mImpl->db->load()
            .transform([]() -> bool {
                return true;
            });
    }

    ll::Expected<bool> NoticePlugin::unload() {
        if (!this->mImpl->ModuleEnabled)
            return false;

        this->mImpl->db.reset();
        this->mImpl->db2.reset();
        this->mImpl->logger.reset();
        this->mImpl->ModuleEnabled = false;

        if (this->mImpl->mRegistered.load(std::memory_order_acquire))
            this->unlistenEvent();

        return true;
    }

    ll::Expected<bool> NoticePlugin::registry() {
        if (!this->mImpl->ModuleEnabled)
            return false;

        return this->mImpl->db2->create("Notice", [](SQLiteStorage::ColumnCallback ctor) -> void {
            ctor("name");
            ctor("close");
        }).and_then([this]() -> ll::Expected<void> {
            return this->registeryUI();
        }).transform([this]() -> bool {
            this->registeryCommand();
            this->listenEvent();

            this->mImpl->mRegistered.store(true, std::memory_order_release);

            return true;
        });
    }

    ll::Expected<bool> NoticePlugin::unregistry() {
        if (!this->mImpl->ModuleEnabled)
            return false;

        this->unlistenEvent();

        return this->getDatabase()->save()
            .transform([this]() -> bool {
                this->mImpl->mRegistered.store(false, std::memory_order_release);

                return true;
            });
    }
}
