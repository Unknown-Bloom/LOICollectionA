#include <atomic>
#include <memory>
#include <ranges>
#include <string>
#include <unordered_map>

#include <fmt/core.h>

#include <ll/api/Expected.h>
#include <ll/api/io/Logger.h>
#include <ll/api/io/LoggerRegistry.h>
#include <ll/api/command/Command.h>
#include <ll/api/command/CommandHandle.h>
#include <ll/api/command/CommandRegistrar.h>
#include <ll/api/event/EventBus.h>
#include <ll/api/event/ListenerBase.h>
#include <ll/api/event/player/PlayerConnectEvent.h>

#include <mc/world/actor/player/Player.h>

#include <mc/server/commands/CommandOrigin.h>
#include <mc/server/commands/CommandOutput.h>
#include <mc/server/commands/CommandPermissionLevel.h>

#include "LOICollectionA/utils/I18nUtils.h"

#include "LOICollectionA/data/SQLiteStorage.h"

#include "LOICollectionA/frontend/AST.h"

#include "LOICollectionA/include/form/GUIManager.h"

#include "LOICollectionA/include/CallbackUtils.h"

#include "LOICollectionA/base/Cache.h"
#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/include/server/Plugins/LanguagePlugin.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    struct LanguagePlugin::Impl {
        LRUKCache<std::string, std::string> Cache;

        std::atomic<bool> mRegistered{ false };

        std::string mGuiPath;

        std::shared_ptr<SQLiteStorage> db;
        std::shared_ptr<ll::io::Logger> logger;
        
        ll::event::ListenerPtr PlayerConnectEventListener;

        Impl() : Cache(100, 100) {}
    }; 

    LanguagePlugin::LanguagePlugin() : mImpl(std::make_unique<Impl>()) {};
    LanguagePlugin::~LanguagePlugin() = default;

    std::shared_ptr<LanguagePlugin> LanguagePlugin::getShared() {
        static auto instance = std::shared_ptr<LanguagePlugin>(new LanguagePlugin());
        return instance;
    }

    std::shared_ptr<ll::io::Logger> LanguagePlugin::getLogger() {
        return this->mImpl->logger;
    }

    void LanguagePlugin::registeryCommand() {
        ll::command::CommandHandle& command = ll::command::CommandRegistrar::getInstance(false)
            .getOrCreateCommand("language", tr({}, "commands.language.description"), CommandPermissionLevel::Any, CommandFlagValue::NotCheat | CommandFlagValue::Async);
        command.overload().text("setting").execute([](CommandOrigin const& origin, CommandOutput& output) -> void {
            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);

            form::GUIManager::getInstance().open("language", "language.gui", form::GUIManagerType::CustomForm, player)
                .or_else(modules::defaultErrorHandler<LanguagePlugin>);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
        command.overload().text("reload").execute([this](CommandOrigin const& origin, CommandOutput& output) -> void {
            if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors)
                return output.error(tr(origin.getLocaleCode(), "commands.generic.permission"));

            output.success(tr(origin.getLocaleCode(), "commands.generic.reload"));
            
            form::GUIManager::getInstance().load("language", this->mImpl->mGuiPath)
                .transform([&origin, &output]() -> void {
                    output.success(tr(origin.getLocaleCode(), "commands.generic.reload.success"));
                })
                .or_else(modules::defaultErrorHandler<LanguagePlugin>);
        });
    }

    ll::Expected<void> LanguagePlugin::registeryUI() {
        return form::GUIManager::getInstance().load("language", this->mImpl->mGuiPath)
            .transform([this]() -> void {
                auto data = I18nUtils::getInstance()->data
                    | std::views::keys
                    | std::views::enumerate
                    | std::views::transform([](auto&& t) {
                        auto [idx, k] = t;
                        
                        auto obj = std::make_shared<frontend::Object>();
                        obj->className = "DropdownItem";
                        obj->classIndex = -1;
                        obj->assign("label", k);
                        obj->assign("value", static_cast<int>(idx));

                        return obj;
                    });

                auto arrPtr = std::make_shared<frontend::ArrayValue>();
                arrPtr->elements = data | std::ranges::to<std::vector<frontend::TypedValue>>();

                form::GUIManager::getInstance().registerValue("language.names", [arrPtr](Player&) -> frontend::ArrayRef {
                    return arrPtr;
                });

                form::GUIManager::getInstance().registerCallback("language.callback", [this, arrPtr](frontend::ArrayRef args, Player& player) mutable -> ll::Expected<void> {
                    if (args->elements.size() != 1 || !std::holds_alternative<float>(args->elements[0]))
                        return ll::makeStringError("language.callback: must take exactly one float parameter");

                    auto& data = std::get<frontend::ObjectRef>(
                        arrPtr->elements.at(static_cast<int>(std::get<float>(args->elements[0])))
                    );

                    auto* label = data->find("label");
                    if (!label)
                        return ll::makeStringError("language.callback: DropdownItem is missing 'label'");

                    return this->set(player, std::get<std::string>(*label))
                        .transform([this, &player]() -> void {
                            this->getLogger()->info(LOICollectionAPI::CallbackUtils::getInstance().translate(tr({}, "language.log"), player));
                        });
                });
            });
    }

    void LanguagePlugin::listenEvent() {
        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        this->mImpl->PlayerConnectEventListener = eventBus.emplaceListener<ll::event::PlayerConnectEvent>([this](ll::event::PlayerConnectEvent& event) mutable -> void {
            if (event.self().isSimulatedPlayer())
                return;

            std::string langcode = event.self().getLocaleCode();
            if (auto data = I18nUtils::getInstance()->data; data.find(langcode) == data.end())
                langcode = I18nUtils::getInstance()->defaultLocale;

            if (!this->mImpl->db->has("Language", event.self().getUuid().asString())) {
                this->set(event.self(), langcode).or_else(modules::defaultErrorHandler<LanguagePlugin>);
            }
        }, ll::event::EventPriority::High);
    }

    void LanguagePlugin::unlistenEvent() {
        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        eventBus.removeListener(this->mImpl->PlayerConnectEventListener);
    }

    ll::Expected<std::string> LanguagePlugin::getLanguage(const std::string& uuid) {
        if (this->mImpl->Cache.contains(uuid))
            return *this->mImpl->Cache.get(uuid).value();
        
        return this->mImpl->db->get("Language", uuid, "value", I18nUtils::getInstance()->defaultLocale)
            .transform([this, &uuid](std::string langcode) -> std::string {
                this->mImpl->Cache.put(uuid, langcode);
                return langcode;  
            });
    }

    ll::Expected<std::string> LanguagePlugin::getLanguage(Player& player) {
        return this->getLanguage(player.getUuid().asString());
    }

    ll::Expected<void> LanguagePlugin::set(Player& player, const std::string& langcode) {
        std::string mObject = player.getUuid().asString();

        std::unordered_map<std::string, std::string> mData = {
            { "name", player.getRealName() },
            { "value", langcode }
        };

        return this->mImpl->db->set("Language", mObject, mData)
            .transform([this, &mObject, &langcode]() -> void {
                this->mImpl->Cache.put(mObject, langcode);
            });
    }

    std::string LanguagePlugin::getName() {
        return "LanguagePlugin";
    }

    modules::ModulePriority LanguagePlugin::getPriority() {
        return modules::ModulePriority::Normal;
    }

    ll::Expected<bool> LanguagePlugin::load() {
        this->mImpl->mGuiPath = (std::filesystem::path(ServiceProvider::getInstance().getService<std::string>("GuiPath")->data()) / "language.lcui").string();

        this->mImpl->db = ServiceProvider::getInstance().getService<SQLiteStorage>("SettingsDB");
        this->mImpl->logger = ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA");

        return true;
    }

    ll::Expected<bool> LanguagePlugin::unload() {
        this->mImpl->db.reset();
        this->mImpl->logger.reset();

        if (this->mImpl->mRegistered.load(std::memory_order_acquire))
            this->unlistenEvent();

        return true;
    }

    ll::Expected<bool> LanguagePlugin::registry() {
        return this->mImpl->db->create("Language", [](SQLiteStorage::ColumnCallback ctor) -> void {
            ctor("name");
            ctor("value");
        }).and_then([this]() -> ll::Expected<void> {
            return registeryUI();
        }).transform([this]() -> bool {
            this->registeryCommand();
            this->listenEvent();

            this->mImpl->mRegistered.store(true, std::memory_order_release);

            return true;
        });
    }

    ll::Expected<bool> LanguagePlugin::unregistry() {
        this->unlistenEvent();

        return this->mImpl->db->exec("VACUUM;")
            .transform([this]() -> bool {
                this->mImpl->mRegistered.store(false, std::memory_order_release);

                return true;
            });
    }
}
