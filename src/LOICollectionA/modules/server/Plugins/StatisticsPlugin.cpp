#include <atomic>
#include <memory>
#include <ranges>
#include <string>
#include <filesystem>
#include <unordered_map>

#include <fmt/format.h>
#include <magic_enum/magic_enum.hpp>

#include <ll/api/io/Logger.h>
#include <ll/api/io/LoggerRegistry.h>
#include <ll/api/service/Bedrock.h>
#include <ll/api/command/Command.h>
#include <ll/api/command/CommandHandle.h>
#include <ll/api/command/CommandRegistrar.h>
#include <ll/api/base/Containers.h>

#include <ll/api/thread/ThreadPoolExecutor.h>

#include <ll/api/event/EventBus.h>
#include <ll/api/event/ListenerBase.h>
#include <ll/api/event/entity/MobDieEvent.h>
#include <ll/api/event/player/PlayerConnectEvent.h>
#include <ll/api/event/player/PlayerRespawnEvent.h>
#include <ll/api/event/player/PlayerPlaceBlockEvent.h>
#include <ll/api/event/player/PlayerDisconnectEvent.h>
#include <ll/api/event/player/PlayerDestroyBlockEvent.h>

#include <mc/server/commands/CommandOrigin.h>
#include <mc/server/commands/CommandOutput.h>
#include <mc/server/commands/CommandSelector.h>
#include <mc/server/commands/CommandPermissionLevel.h>
#include <mc/server/commands/CommandOutputMessageType.h>

#include <mc/world/level/Level.h>
#include <mc/world/actor/player/Player.h>

#include "LOICollectionA/include/server/Plugins/LanguagePlugin.h"

#include "LOICollectionA/coro/TimerManager.h"

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/data/SQLiteStorage.h"

#include "LOICollectionA/frontend/AST.h"

#include "LOICollectionA/include/form/GUIManager.h"

#include "LOICollectionA/base/Wrapper.h"
#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/StatisticsPlugin.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    struct StatisticsPlugin::operation {
        StatisticType Type;
    };

    struct StatisticsPlugin::Impl {
        std::unordered_map<std::string, std::string> mOnilneTime;

        ll::ConcurrentDenseMap<std::string, ll::ConcurrentDenseMap<std::string, int>> mCache;

        std::atomic<bool> mRegistered{ false };

        Config::C_Statistics options;

        std::shared_ptr<SQLiteStorage> db;
        std::shared_ptr<SQLiteStorage> db2;
        std::shared_ptr<ll::io::Logger> logger;

        std::string mGuiPath;

        std::unordered_map<std::string, ll::event::ListenerPtr> mListeners;

        ll::thread::ThreadPoolExecutor mExecutor{ "StatisticsPlugin", std::max(static_cast<size_t>(std::thread::hardware_concurrency()) - 2, static_cast<size_t>(2)) };

        std::shared_ptr<TimerManager> mTimerManager;

        std::atomic<bool> WriteDatabaseTaskRunning{ true };

        Impl() : mTimerManager(std::make_shared<TimerManager>(this->mExecutor)) {}
    };

    StatisticsPlugin::StatisticsPlugin() : mImpl(std::make_unique<Impl>()) {}
    StatisticsPlugin::~StatisticsPlugin() = default;

    std::shared_ptr<StatisticsPlugin> StatisticsPlugin::getShared() {
        static auto instance = std::shared_ptr<StatisticsPlugin>(new StatisticsPlugin());
        return instance;
    }

    std::error_code StatisticsPlugin::makeErrorCode(StatisticsPluginErrorCode e) {
        static StatisticsPluginErrorCategory cat;
        return std::error_code{ static_cast<int>(e), cat };
    }

    std::shared_ptr<SQLiteStorage> StatisticsPlugin::getDatabase() {
        return this->mImpl->db;
    }

    std::shared_ptr<ll::io::Logger> StatisticsPlugin::getLogger() {
        return this->mImpl->logger;
    }

    void StatisticsPlugin::startWirteDatabaseTask() {
        this->mImpl->mTimerManager->loopSchedule("WirteDatabaseTask", std::chrono::minutes(this->mImpl->options.RefreshIntervalInMinutes), [this]() -> void {
            if (!this->mImpl->WriteDatabaseTaskRunning.load(std::memory_order_acquire))
                return;
            
            auto transaction = SQLiteStorageTransaction::create(*this->getDatabase());
            if (!transaction.has_value()) {
                modules::defaultErrorHandler<StatisticsPlugin>(transaction.error());

                return;
            }

            auto connection = transaction.value().connection();

            for (const auto& it : this->mImpl->mCache) {
                for (const auto& it2 : it.second) {
                    this->getDatabase()->set(connection, "Statistics", it.first, it2.first, std::to_string(it2.second))
                        .or_else(modules::defaultErrorHandler<StatisticsPlugin>);
                }
            }

            transaction.value().commit();

            this->mImpl->mCache.clear();
        });
    }

    void StatisticsPlugin::registeryCommand() {
        ll::command::CommandHandle& command = ll::command::CommandRegistrar::getInstance(false)
            .getOrCreateCommand("statistics", tr({}, "commands.statistics.description"), CommandPermissionLevel::Any, CommandFlagValue::NotCheat | CommandFlagValue::Async);
        command.overload().text("gui").execute([](CommandOrigin const& origin, CommandOutput& output) -> void {
            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);

            auto ctx = std::make_shared<frontend::ArrayValue>();
            ctx->elements.emplace_back("");

            form::GUIManager::getInstance().open("statistics", "statistics.open", form::GUIManagerType::PaginatedForm, player, ctx)
                .or_else(modules::defaultErrorHandler<StatisticsPlugin>);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
        command.overload<operation>().text("gui").required("Type").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                Actor* entity = origin.getEntity();
                if (entity == nullptr || !entity->isType(ActorType::Player))
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
                Player& player = *static_cast<Player*>(entity);

                this->getStatisticName(param.Type)
                    .and_then([&player, &output, &origin](const std::string& name) -> ll::Expected<void> {
                        auto ctx = std::make_shared<frontend::ArrayValue>();
                        ctx->elements.emplace_back(name);

                        return form::GUIManager::getInstance().open("statistics", "statistics.specific", form::GUIManagerType::CustomForm, player, ctx)
                            .transform([&output, &origin, &player]() -> void {
                                output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
                            });
                    })
                    .or_else(modules::defaultErrorHandler<StatisticsPlugin>);
            });
        command.overload().text("reload").execute([this](CommandOrigin const& origin, CommandOutput& output) -> void {
            if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors)
                return output.error(tr(origin.getLocaleCode(), "commands.generic.permission"));

            output.success(tr(origin.getLocaleCode(), "commands.generic.reload"));
            
            form::GUIManager::getInstance().load("statistics", this->mImpl->mGuiPath)
                .transform([&origin, &output]() -> void {
                    output.success(tr(origin.getLocaleCode(), "commands.generic.reload.success"));
                })
                .or_else(modules::defaultErrorHandler<StatisticsPlugin>);
        });
    }

    ll::Expected<void> StatisticsPlugin::registeryUI() {
        return form::GUIManager::getInstance().load("statistics", this->mImpl->mGuiPath)
            .transform([this]() -> void {
                form::GUIManager::getInstance().registerValue("statistics.names", [this](Player&) -> ll::Expected<frontend::ArrayRef> {
                    auto values = std::make_shared<frontend::ArrayValue>();

                    for (auto type : magic_enum::enum_entries<StatisticType>()) {
                        auto name = this->getStatisticName(type.first);
                        if (!name.has_value())
                            return ll::Unexpected(name.error());

                        values->elements.emplace_back(name.value());
                    }

                    return values;
                });

                form::GUIManager::getInstance().registerRequest("statistics.ranking", [this](frontend::ArrayRef args, Player& player) -> ll::Expected<frontend::ArrayRef> {
                    if (args->elements.size() != 1 || !std::holds_alternative<std::string>(args->elements[0]))
                        return ll::makeStringError("statistics.ranking: must take exactly one string parameter");

                    auto name = std::get<std::string>(args->elements[0]);

                    const auto entries = magic_enum::enum_entries<StatisticType>();
                    auto it = std::ranges::find_if(entries, [this, &name](const auto& entry) -> bool {
                        auto result = this->getStatisticName(entry.first);
                        return result.has_value() && result.value() == name;
                    });

                    if (it == entries.end())
                        return ll::makeStringError("statistics.ranking: unknown statistic type");

                    StatisticType type = it->first;

                    return LanguagePlugin::getShared()->getLanguage(player)
                        .and_then([this, type, name](const std::string& language) -> ll::Expected<frontend::ArrayRef> {
                            auto lists = this->getRankingList(type, this->getRankingPlayerCount());
                            if (!lists.has_value())
                                return ll::Unexpected(lists.error());

                            std::string lines;
                            for (const auto& [index, pair] : std::views::enumerate(lists.value())) {
                                auto pname = this->getPlayerInfo(pair.first);
                                if (!pname.has_value())
                                    return ll::Unexpected(pname.error());

                                if (!lines.empty())
                                    lines += "\n";

                                lines += fmt::format(
                                    fmt::runtime(tr(language, "statistics.gui.specific.line")),
                                    index + 1,
                                    pname.value(),
                                    pair.second
                                );
                            }

                            auto values = std::make_shared<frontend::ArrayValue>();
                            values->elements.emplace_back(name);
                            values->elements.emplace_back(this->getRankingPlayerCount());
                            values->elements.emplace_back(lines);

                            return values;
                        });
                });
            });
    }

    void StatisticsPlugin::listenEvent() {
        this->mImpl->WriteDatabaseTaskRunning.store(true, std::memory_order_release);

        this->startWirteDatabaseTask();

        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        this->mImpl->mListeners.emplace("PlayerConnect", eventBus.emplaceListener<ll::event::PlayerConnectEvent>([this, option = this->mImpl->options.DatabaseInfo](ll::event::PlayerConnectEvent& event) mutable -> void {
            if (event.self().isSimulatedPlayer())
                return;
            
            if (option.OnlineTime)
                this->mImpl->mOnilneTime.emplace(event.self().getUuid().asString(), SystemUtils::getNowTime());
            
            if (option.Join)
                this->addStatistic(event.self(), StatisticType::join, 1).or_else(modules::defaultErrorHandler<StatisticsPlugin>);
        }));

        this->mImpl->mListeners.emplace("PlayerDisconnect", eventBus.emplaceListener<ll::event::PlayerDisconnectEvent>([this, option = this->mImpl->options.DatabaseInfo](ll::event::PlayerDisconnectEvent& event) mutable -> void {
            if (event.self().isSimulatedPlayer())
                return;
            
            std::string mUuid = event.self().getUuid().asString();
            
            if (option.OnlineTime) {
                int mOnlineTime = SystemUtils::toInt(
                    SystemUtils::getTimeSpan(SystemUtils::getNowTime(), this->mImpl->mOnilneTime[mUuid], "0")
                );

                this->addStatistic(event.self(), StatisticType::onlinetime, mOnlineTime).or_else(modules::defaultErrorHandler<StatisticsPlugin>);

                this->mImpl->mOnilneTime.erase(mUuid);
            }
        }));

        this->mImpl->mListeners.emplace("MobDie", eventBus.emplaceListener<ll::event::MobDieEvent>([this, option = this->mImpl->options.DatabaseInfo](ll::event::MobDieEvent& event) mutable -> void {
            if (option.Kill) {
                Actor* mSource = ll::service::getLevel()->fetchEntity(
                    event.source().isChildEntitySource() ? event.source().getEntityUniqueID() : event.source().getDamagingEntityUniqueID(), false
                );

                if (mSource && mSource->isRemotePlayer() && event.self().isRemotePlayer())
                    this->addStatistic(*static_cast<Player*>(mSource), StatisticType::kills, 1).or_else(modules::defaultErrorHandler<StatisticsPlugin>);
            }

            if (event.self().isRemotePlayer() && option.Death)
                this->addStatistic(static_cast<Player&>(event.self()), StatisticType::deaths, 1).or_else(modules::defaultErrorHandler<StatisticsPlugin>);
        }));

        this->mImpl->mListeners.emplace("PlayerPlaceBlock", eventBus.emplaceListener<ll::event::PlayerPlacedBlockEvent>([this, option = this->mImpl->options.DatabaseInfo](ll::event::PlayerPlacedBlockEvent& event) mutable -> void {
            if (event.self().isSimulatedPlayer())
                return;
            
            if (option.Place)
                this->addStatistic(event.self(), StatisticType::place, 1).or_else(modules::defaultErrorHandler<StatisticsPlugin>);
        }));

        this->mImpl->mListeners.emplace("PlayerDestroyBlock", eventBus.emplaceListener<ll::event::PlayerDestroyBlockEvent>([this, option = this->mImpl->options.DatabaseInfo](ll::event::PlayerDestroyBlockEvent& event) mutable -> void {
            if (event.self().isSimulatedPlayer())
                return;
            
            if (option.Destroy)
                this->addStatistic(event.self(), StatisticType::destroy, 1).or_else(modules::defaultErrorHandler<StatisticsPlugin>);
        }));

        this->mImpl->mListeners.emplace("PlayerRespawn", eventBus.emplaceListener<ll::event::PlayerRespawnEvent>([this, option = this->mImpl->options.DatabaseInfo](ll::event::PlayerRespawnEvent& event) mutable -> void {
            if (event.self().isSimulatedPlayer())
                return;
            
            if (option.Respawn)
                this->addStatistic(event.self(), StatisticType::respawn, 1).or_else(modules::defaultErrorHandler<StatisticsPlugin>);
        }));
    }

    void StatisticsPlugin::unlistenEvent() {
        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        for (auto& listener : this->mImpl->mListeners)
            eventBus.removeListener(listener.second);

        this->mImpl->mListeners.clear();

        this->mImpl->WriteDatabaseTaskRunning.store(false, std::memory_order_release);

        this->mImpl->mTimerManager->cancelAll();
    }

    ll::Expected<void> StatisticsPlugin::setExecutor(const ll::coro::Executor& executor) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(StatisticsPluginErrorCode::Invalid));

        this->mImpl->mTimerManager->setExecutor(executor);

        return {};
    }

    ll::Expected<std::string> StatisticsPlugin::getStatisticName(StatisticType type) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(StatisticsPluginErrorCode::Invalid));

        switch (type) {
            case StatisticType::onlinetime: return "onlinetime";
            case StatisticType::kills: return "kill";
            case StatisticType::deaths: return "death";
            case StatisticType::place: return "place";
            case StatisticType::destroy: return "destroy";
            case StatisticType::respawn: return "respawn";
            case StatisticType::join: return "joins";
        }

        return "Unknown";
    }

    ll::Expected<std::string> StatisticsPlugin::getPlayerInfo(const std::string& uuid) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(StatisticsPluginErrorCode::Invalid));

        return this->getDatabase()->get("Language", uuid, "name", "Unknown");
    }

    ll::Expected<std::vector<std::pair<std::string, int>>> StatisticsPlugin::getRankingList(StatisticType type, int limit) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(StatisticsPluginErrorCode::Invalid));

        return this->getStatistics(type, limit)
            .transform([](std::vector<std::pair<std::string, int>> data) -> std::vector<std::pair<std::string, int>> {
                std::ranges::sort(data, [](const auto& a, const auto& b) {
                    return a.second > b.second;
                });

                return data;
            });
    }

    ll::Expected<std::vector<std::pair<std::string, int>>> StatisticsPlugin::getStatistics(StatisticType type, int limit) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(StatisticsPluginErrorCode::Invalid));

        return this->getDatabase()->list("Statistics")
            .transform([this, type, limit](const std::vector<std::string>& ids) -> std::vector<std::pair<std::string, int>> {
                return ids
                    | std::views::take(limit > 0 ? limit : static_cast<int>(ids.size()))
                    | std::views::transform([this, type](const std::string& key) -> std::pair<std::string, int> { 
                        auto result = this->getStatistic(key, type);
                        if (!result.has_value()) {
                            modules::defaultErrorHandler<StatisticsPlugin>(result.error());

                            return {};
                        }

                        return std::make_pair(key, result.value());
                    })
                    | std::ranges::to<std::vector<std::pair<std::string, int>>>();
            });
    }

    ll::Expected<int> StatisticsPlugin::getStatistic(const std::string& uuid, StatisticType type) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(StatisticsPluginErrorCode::Invalid));

        return this->getStatisticName(type)
            .and_then([this, uuid](const std::string& table) -> ll::Expected<int> {
                if (table.empty())
                    return 0;

                if (this->mImpl->mCache.contains(uuid) && this->mImpl->mCache[uuid].contains(table))
                    return this->mImpl->mCache[uuid][table];

                return this->getDatabase()->get("Statistics", uuid, table, "0")
                    .transform([this, uuid, table](const std::string& value) -> int {
                        int result = SystemUtils::toInt(value);

                        this->mImpl->mCache[uuid][table] = result;

                        return result;
                    });
            });
    }

    ll::Expected<int> StatisticsPlugin::getStatistic(Player& player, StatisticType type) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(StatisticsPluginErrorCode::Invalid));

        return this->getStatistic(player.getUuid().asString(), type);
    }

    ll::Expected<void> StatisticsPlugin::addStatistic(Player& player, StatisticType type, int value) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(StatisticsPluginErrorCode::Invalid));

        std::string uuid = player.getUuid().asString();
        
        return this->getStatisticName(type)
            .and_then([this, type, value, uuid, &player](const std::string& table) -> ll::Expected<void> {
                return this->getStatistic(player, type)
                    .transform([this, value, uuid, table](int result) -> void {
                        int mValue = result + value;

                        this->mImpl->mCache[uuid][table] = mValue;
                    });
            });
    }

    bool StatisticsPlugin::isValid() {
        return this->getLogger() != nullptr && this->getDatabase() != nullptr;
    }

    int StatisticsPlugin::getRankingPlayerCount() {
        return this->mImpl->options.RankingPlayerCount;
    }

    std::string StatisticsPlugin::getName() {
        return "StatisticsPlugin";
    }

    modules::ModulePriority StatisticsPlugin::getPriority() {
        return modules::ModulePriority::High;
    }

    ll::Expected<bool> StatisticsPlugin::load() {
        if (!ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Statistics.ModuleEnabled)
            return false;

        auto mDataPath = std::filesystem::path(ServiceProvider::getInstance().getService<std::string>("DataPath")->data());

        this->mImpl->db = std::make_shared<SQLiteStorage>((mDataPath / "statistics.db").string());
        this->mImpl->db2 = ServiceProvider::getInstance().getService<SQLiteStorage>("SettingsDB");
        this->mImpl->logger = ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA");
        this->mImpl->options = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Statistics;
        this->mImpl->mGuiPath = (std::filesystem::path(ServiceProvider::getInstance().getService<std::string>("GuiPath")->data()) / "statistics.lcui").string();

        return true;
    }

    ll::Expected<bool> StatisticsPlugin::unload() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        this->mImpl->db.reset();
        this->mImpl->logger.reset();
        this->mImpl->options = {};

        if (this->mImpl->mRegistered.load(std::memory_order_acquire))
            this->unlistenEvent();

        return true;
    }

    ll::Expected<bool> StatisticsPlugin::registry() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        return this->getDatabase()->create("Statistics", [](SQLiteStorage::ColumnCallback ctor) -> void {
            ctor("onlinetime");
            ctor("kill");
            ctor("death");
            ctor("place");
            ctor("destroy");
            ctor("respawn");
            ctor("joins");
        }).and_then([this]() -> ll::Expected<void> {
            return this->registeryUI();
        }).transform([this]() -> bool {
            this->registeryCommand();
            this->listenEvent();

            this->mImpl->mRegistered.store(true, std::memory_order_release);

            return true;
        });
    }

    ll::Expected<bool> StatisticsPlugin::unregistry() {
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
