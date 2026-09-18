#include <atomic>
#include <chrono>
#include <memory>
#include <ranges>
#include <string>
#include <vector>
#include <utility>
#include <concepts>
#include <algorithm>
#include <functional>
#include <filesystem>
#include <unordered_map>

#include <fmt/core.h>
#include <concurrentqueue.h>
#include <magic_enum/magic_enum.hpp>

#include <ll/api/Expected.h>
#include <ll/api/io/Logger.h>
#include <ll/api/io/LoggerRegistry.h>

#include <ll/api/coro/CoroTask.h>
#include <ll/api/chrono/GameChrono.h>
#include <ll/api/thread/ThreadPoolExecutor.h>
#include <ll/api/thread/ServerThreadExecutor.h>

#include <ll/api/command/Command.h>
#include <ll/api/command/CommandHandle.h>
#include <ll/api/command/CommandRegistrar.h>

#include <ll/api/event/EventBus.h>
#include <ll/api/event/ListenerBase.h>
#include <ll/api/event/player/PlayerConnectEvent.h>
#include <ll/api/event/player/PlayerDisconnectEvent.h>
#include <ll/api/event/player/PlayerChatEvent.h>
#include <ll/api/event/player/PlayerAddExperienceEvent.h>
#include <ll/api/event/player/PlayerAttackEvent.h>
#include <ll/api/event/player/PlayerChangePermEvent.h>
#include <ll/api/event/player/PlayerDestroyBlockEvent.h>
#include <ll/api/event/player/PlayerPlaceBlockEvent.h>
#include <ll/api/event/player/PlayerDieEvent.h>
#include <ll/api/event/player/PlayerPickUpItemEvent.h>
#include <ll/api/event/player/PlayerInteractBlockEvent.h>
#include <ll/api/event/player/PlayerRespawnEvent.h>
#include <ll/api/event/player/PlayerUseItemEvent.h>

#include <mc/deps/nbt/Tag.h>
#include <mc/deps/nbt/CompoundTag.h>

#include <mc/deps/core/math/Vec3.h>
#include <mc/deps/core/string/HashedString.h>
#include <mc/deps/shared_types/legacy/actor/ActorDamageCause.h>

#include <mc/world/level/BlockPos.h>
#include <mc/world/level/block/Block.h>
#include <mc/world/level/block/actor/BlockActor.h>
#include <mc/world/level/dimension/Dimension.h>

#include <mc/world/actor/ActorDamageSource.h>
#include <mc/world/actor/item/ItemActor.h>
#include <mc/world/actor/player/Player.h>

#include <mc/world/item/ItemStack.h>
#include <mc/world/item/SaveContext.h>
#include <mc/world/item/SaveContextFactory.h>

#include <mc/server/commands/CommandOrigin.h>
#include <mc/server/commands/CommandOutput.h>
#include <mc/server/commands/CommandSelector.h>
#include <mc/server/commands/CommandPosition.h>
#include <mc/server/commands/CommandPermissionLevel.h>
#include <mc/server/commands/CommandOutputMessageType.h>
#include <mc/server/commands/PlayerPermissionLevel.h>

#include "LOICollectionA/include/server/Events/world/BlockExplodedEvent.h"
#include "LOICollectionA/include/server/Events/player/PlayerContainerEvent.h"

#include "LOICollectionA/coro/TimerManager.h"

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/mc-server/BlockUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"

#include "LOICollectionA/base/Wrapper.h"
#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/BehaviorEvent/BehaviorEventPlugin.h"

struct Traits : moodycamel::ConcurrentQueueDefaultTraits {
    static const size_t BLOCK_SIZE = 1024;
    static const size_t IMPLICIT_INITIAL_INDEX_SIZE = 2048;
};

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    struct BehaviorEventPlugin::operation {
        CommandPosition PositionOrigin;
        CommandPosition PositionTarget;
        DimensionType Dimension;
        std::string EventId;
        std::string EventName;
        std::string Target;
        std::string Value;
        int Time = 1;
        int Radius = 1;
        int Limit = 100;
    };

    struct BehaviorEventPlugin::Impl {
        moodycamel::ConcurrentQueue<Event, Traits> mEvents;

        std::atomic<bool> mRegistered{ false };

        Config::C_BehaviorEvent options;

        std::shared_ptr<BlockRepository> db;
        std::shared_ptr<ll::io::Logger> logger;

        std::unordered_map<std::string, ll::event::ListenerPtr> mListeners;

        ll::thread::ThreadPoolExecutor mExecutor{ "BehavorEventPlugin", std::max(static_cast<size_t>(std::thread::hardware_concurrency()) - 2, static_cast<size_t>(2)) };

        std::shared_ptr<TimerManager> mTimerManager;

        std::atomic<bool> WriteDatabaseTaskRunning{ true };
        std::atomic<bool> CleanDatabaseTaskRunning{ true };

        Impl() : mTimerManager(std::make_shared<TimerManager>(this->mExecutor)) {}
    };

    BehaviorEventPlugin::BehaviorEventPlugin() : mImpl(std::make_unique<Impl>()) {};
    BehaviorEventPlugin::~BehaviorEventPlugin() = default;

    std::shared_ptr<BehaviorEventPlugin> BehaviorEventPlugin::getShared() {
        static auto instance = std::shared_ptr<BehaviorEventPlugin>(new BehaviorEventPlugin());
        return instance;
    }

    std::error_code BehaviorEventPlugin::makeErrorCode(BehaviorEventPluginErrorCode e) {
        static BehaviorEventPluginErrorCategory cat;
        return std::error_code{ static_cast<int>(e), cat };
    }

    std::shared_ptr<BlockRepository> BehaviorEventPlugin::getDatabase() {
        return this->mImpl->db;
    }

    std::shared_ptr<ll::io::Logger> BehaviorEventPlugin::getLogger() {
        return this->mImpl->logger;
    }

    template<typename T>
    void BehaviorEventPlugin::registeryEvent(
        const std::string& name,
        const std::string& type,
        const std::string& id,
        std::function<bool(BehaviorEventConfig)> config,
        std::function<void(ll::event::Event&, Event&)> process,
        std::function<std::string(std::string, ll::event::Event&)> formatter
    ) {
        auto& eventBus = ll::event::EventBus::getInstance();

        auto listener = eventBus.emplaceListener<T>([this, name, type, id, config, process, formatter](T& event) mutable -> void {
            if (!config(BehaviorEventConfig::ModuleEnabled))
                return;
            
            Vec3 mPosition{};
            int mDimension = 0;

            if constexpr (requires(const T& t) {
                { t.self() } -> std::convertible_to<Player&>;
            }) {
                if (event.self().isSimulatedPlayer())
                    return;

                mDimension = event.self().getDimensionId();
                
                if constexpr (requires(const T& t) { { t.pos() }; }) {
                    mPosition = event.pos();
                } else {
                    mPosition = event.self().getPosition();
                }
            } else if constexpr (requires(const T& t) { { t.pos() }; }) {
                mPosition = event.pos();
            } else if constexpr (requires(const T& t) {
                { t.getPosition() };
                { t.getDimensionId() };
            }) {
                mPosition = event.getPosition();
                mDimension = event.getDimensionId();
            }

            if (config(BehaviorEventConfig::RecordDatabase)) {
                this->getBasicEvent(name, type, mPosition, mDimension)
                    .transform([this, &event, process](Event mEvent) -> void {
                        process(event, mEvent);

                        if (!this->mImpl->mEvents.try_enqueue(mEvent))
                            this->getLogger()->error(fmt::runtime(tr({}, "console.log.error.container")), this->getName());
                    })
                    .or_else(modules::defaultErrorHandler<BehaviorEventPlugin>);
            }

            if (config(BehaviorEventConfig::OutputConsole))
                this->getLogger()->info(formatter(tr({}, id), event));
        }, ll::event::EventPriority::Highest);

        mImpl->mListeners.emplace(name, listener);
    }

    void BehaviorEventPlugin::startWriteDatabaseTask() {
        this->mImpl->mTimerManager->loopSchedule("WirteDatabaseTask", std::chrono::minutes(this->mImpl->options.RefreshIntervalInMinutes), [this]() -> void {
            if (!this->mImpl->WriteDatabaseTaskRunning.load(std::memory_order_acquire))
                return;
            
            Event mEvent{};

            std::vector<Event> mEvents;
            while (this->mImpl->mEvents.try_dequeue(mEvent))
                mEvents.emplace_back(mEvent);

            std::ranges::sort(mEvents.begin(), mEvents.end(), {}, [](const Event& mEvent) {
                return std::tie(mEvent.eventName, mEvent.eventTime, mEvent.eventType, mEvent.posX, mEvent.posY, mEvent.posZ, mEvent.dimension);
            });
            auto [first, last] = std::ranges::unique(mEvents.begin(), mEvents.end(), {}, [](const Event& mEvent) {
                return std::tie(mEvent.eventName, mEvent.eventTime, mEvent.eventType, mEvent.posX, mEvent.posY, mEvent.posZ, mEvent.dimension);
            });
            mEvents.erase(first, last);

            if (mEvents.empty())
                return;

            auto txn = BlockRepository::WriteTransaction::create(*this->getDatabase());
            if (!txn) {
                this->getLogger()->error("BehavorEventPlugin write transaction create failed: {}", txn.error().message());

                return;
            }

            for (const Event& mEvent : mEvents) {
                std::string mTismestamp = SystemUtils::getCurrentTimestamp();

                std::unordered_map<std::string, std::string> mData = {
                    { "event_name", mEvent.eventName },
                    { "event_time", mEvent.eventTime },
                    { "event_type", mEvent.eventType },
                    { "position_x", std::to_string(mEvent.posX) },
                    { "position_y", std::to_string(mEvent.posY) },
                    { "position_z", std::to_string(mEvent.posZ) },
                    { "position_dimension", std::to_string(mEvent.dimension) }
                };

                for (const auto& field : mEvent.extendedFields)
                    mData[field.first] = field.second;

                if (auto r = this->getDatabase()->set(txn.value().connection(), "Events", mTismestamp, mData); !r) {
                    this->getLogger()->error("BehavorEventPlugin write failed: {}", r.error().message());

                    break;
                }
            }

            if (auto r = txn.value().commit(); !r)
                this->getLogger()->error("BehavorEventPlugin write commit failed: {}", r.error().message());
        });
    }

    void BehaviorEventPlugin::startCleanDatabaseTask() {
        this->mImpl->mTimerManager->loopSchedule("CleanDatabaseTask", std::chrono::minutes(this->mImpl->options.CleanDatabaseInterval), [this]() -> void {
            if (!this->mImpl->CleanDatabaseTaskRunning.load(std::memory_order_acquire))
                return;
            
            this->getEvents()
                .and_then([this](const std::vector<std::string>& events) -> ll::Expected<void> {
                    if (static_cast<int>(events.size()) >= this->mImpl->options.CleanThresholdEvent)
                        return this->clean(this->mImpl->options.OrganizeDatabaseInterval);

                    return {};
                })
                .or_else(modules::defaultErrorHandler<BehaviorEventPlugin>);
        });
    }

    void BehaviorEventPlugin::registeryCommand() {
        ll::command::CommandHandle& command = ll::command::CommandRegistrar::getInstance(false)
            .getOrCreateCommand("behaviorevent", tr({}, "commands.behaviorevent.description"), CommandPermissionLevel::GameDirectors, CommandFlagValue::NotCheat | CommandFlagValue::Async);
        command.overload().text("clean").execute([this](CommandOrigin const& origin, CommandOutput& output) -> void {
            this->clean(this->mImpl->options.OrganizeDatabaseInterval).or_else(modules::defaultErrorHandler<BehaviorEventPlugin>);

            output.success(tr(origin.getLocaleCode(), "commands.behaviorevent.success.clean"));
        });
        command.overload<operation>().text("query").text("event").text("info").required("EventId").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                this->getDatabase()->get("Events", param.EventId)
                    .transform([&output, &origin](std::unordered_map<std::string, std::string> data) -> void {
                        if (data.empty()) {
                            output.error(tr(origin.getLocaleCode(), "commands.behaviorevent.error.query"));

                            return;
                        }
                        
                        output.success(tr(origin.getLocaleCode(), "commands.behaviorevent.success.query.info"));
                        for (auto& pair : data) {
                            std::string key = pair.first.substr(pair.first.find_first_of('.') + 1);

                            output.success("{0}: {1}", key, pair.second);
                        }
                    }).or_else(modules::defaultErrorHandler<BehaviorEventPlugin>);
            });
        command.overload<operation>().text("query").text("event").text("name").required("EventName").optional("Limit").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                this->getEvents({{ "event_name", param.EventName }}, {}, param.Limit)
                    .transform([&output, &origin, limit = param.Limit](const std::vector<std::string>& result) -> void {
                        if (result.empty()) {
                            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.behaviorevent.success.query")), limit, "None");

                            return;
                        }

                        output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.behaviorevent.success.query")), limit, fmt::join(result, ", "));
                    }).or_else(modules::defaultErrorHandler<BehaviorEventPlugin>);
            });
        command.overload<operation>().text("query").text("event").text("time").required("Time").optional("Limit").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                this->getEvents({{ "event_time", "" }}, [time = param.Time](std::string value) -> bool {
                    std::string mTime = SystemUtils::toTimeCalculate(value, time * 3600, "0");
                    return !SystemUtils::isPastOrPresent(mTime);
                }, param.Limit).transform([&output, &origin, limit = param.Limit](const std::vector<std::string>& result) -> void {
                    if (result.empty()) {
                        output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.behaviorevent.success.query")), limit, "None");

                        return;
                    }

                    output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.behaviorevent.success.query")), limit, fmt::join(result, ", "));
                }).or_else(modules::defaultErrorHandler<BehaviorEventPlugin>);
            });
        command.overload<operation>().text("query").text("event").text("foundation").required("EventName").required("Time").optional("Limit").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                this->getEvents({{ "event_name", param.EventName }}, {}, param.Limit)
                    .and_then([this, time = param.Time, limit = param.Limit](const std::vector<std::string>& names) -> ll::Expected<std::vector<std::string>> {
                        return this->getEvents({{ "event_time", "" }}, [time](std::string value) -> bool {
                            std::string mTime = SystemUtils::toTimeCalculate(value, time * 3600, "0");
                            return !SystemUtils::isPastOrPresent(mTime);
                        }, limit).transform([&names](const std::vector<std::string>& result) -> std::vector<std::string> {
                            return SystemUtils::getIntersection({ names, result });
                        });
                    }).transform([&output, &origin, limit = param.Limit](const std::vector<std::string>& result) -> void {
                        if (result.empty()) {
                            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.behaviorevent.success.query")), limit, "None");

                            return;
                        }

                        output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.behaviorevent.success.query")), limit, fmt::join(result, ", "));
                    }).or_else(modules::defaultErrorHandler<BehaviorEventPlugin>);
            });
        command.overload<operation>().text("query").text("event").text("position").required("PositionOrigin").optional("Limit").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param, Command const& cmd) -> void {
                Vec3 mPosition = param.PositionOrigin.getPosition(cmd.mVersion, origin, Vec3(0, 0, 0));

                this->getEvents({ 
                    { "position_x", std::to_string(static_cast<int>(mPosition.x)) },
                    { "position_y", std::to_string(static_cast<int>(mPosition.y)) },
                    { "position_z", std::to_string(static_cast<int>(mPosition.z)) }
                }, {}, param.Limit).transform([&output, &origin, limit = param.Limit](const std::vector<std::string>& result) -> void {
                    if (result.empty()) {
                        output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.behaviorevent.success.query")), limit, "None");

                        return;
                    }

                    output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.behaviorevent.success.query")), limit, fmt::join(result, ", "));
                }).or_else(modules::defaultErrorHandler<BehaviorEventPlugin>);
            });
        command.overload<operation>().text("query").text("event").text("dimension").required("Dimension").optional("Limit").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                this->getEvents({{ "Position.dimension", std::to_string(param.Dimension) }}, {}, param.Limit)
                    .transform([&output, &origin, limit = param.Limit](const std::vector<std::string>& result) -> void {
                        if (result.empty()) {
                            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.behaviorevent.success.query")), limit, "None");

                            return;
                        }

                        output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.behaviorevent.success.query")), limit, fmt::join(result, ", "));
                    }).or_else(modules::defaultErrorHandler<BehaviorEventPlugin>);
            });
        command.overload<operation>().text("query").text("event").text("site").required("PositionOrigin").required("Dimension").optional("Limit").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param, Command const& cmd) -> void {
                Vec3 mPosition = param.PositionOrigin.getPosition(cmd.mVersion, origin, Vec3(0, 0, 0));

                this->getEvents({ 
                    { "position_x", std::to_string(static_cast<int>(mPosition.x)) },
                    { "position_y", std::to_string(static_cast<int>(mPosition.y)) },
                    { "position_z", std::to_string(static_cast<int>(mPosition.z)) }
                }, {}, param.Limit).and_then([this, dimension = param.Dimension, limit = param.Limit](const std::vector<std::string>& positions) -> ll::Expected<std::vector<std::string>> {
                    return this->getEvents({{ "position_dimension", std::to_string(dimension) }}, {}, limit)
                        .transform([&positions](const std::vector<std::string>& result) -> std::vector<std::string> { 
                            return SystemUtils::getIntersection({ positions, result });
                        });
                }).transform([&output, &origin, limit = param.Limit](const std::vector<std::string>& result) -> void {
                    if (result.empty()) {
                        output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.behaviorevent.success.query")), limit, "None");

                        return;
                    }

                    output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.behaviorevent.success.query")), limit, fmt::join(result, ", "));
                }).or_else(modules::defaultErrorHandler<BehaviorEventPlugin>);
            });
        command.overload<operation>().text("query").text("event").text("custom").required("Target").required("Value").optional("Limit").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                this->getEvents({ { param.Target, param.Value } }, {}, param.Limit)
                    .transform([&output, &origin, limit = param.Limit](const std::vector<std::string>& result) -> void {
                        if (result.empty()) {
                            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.behaviorevent.success.query")), limit, "None");

                            return;
                        }

                        output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.behaviorevent.success.query")), limit, fmt::join(result, ", "));
                    }).or_else(modules::defaultErrorHandler<BehaviorEventPlugin>);
            });
        command.overload<operation>().text("query").text("action").text("range").required("PositionOrigin").required("Radius").optional("Limit").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param, Command const& cmd) -> void {
                Vec3 mPosition = param.PositionOrigin.getPosition(cmd.mVersion, origin, Vec3(0, 0, 0));

                this->getEventsByPosition(origin.getDimension()->getDimensionId(), [mPosition, radius = param.Radius](int x, int y, int z) -> bool {
                    return Vec3(x, y, z).distanceToSqr(mPosition) <= radius;
                }, param.Limit).transform([&output, &origin, limit = param.Limit](const std::vector<std::string>& result) -> void {
                    if (result.empty()) {
                        output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.behaviorevent.success.query")), limit, "None");

                        return;
                    }

                    output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.behaviorevent.success.query")), limit, fmt::join(result, ", "));
                }).or_else(modules::defaultErrorHandler<BehaviorEventPlugin>);
            });
        command.overload<operation>().text("query").text("action").text("position").required("PositionOrigin").required("PositionTarget").optional("Limit").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param, Command const& cmd) -> void {
                Vec3 mPositionOrigin = param.PositionOrigin.getPosition(cmd.mVersion, origin, Vec3(0, 0, 0));
                Vec3 mPositionTarget = param.PositionTarget.getPosition(cmd.mVersion, origin, Vec3(0, 0, 0));

                Vec3 mPositionMin(std::min(mPositionOrigin.x, mPositionTarget.x), std::min(mPositionOrigin.y, mPositionTarget.y), std::min(mPositionOrigin.z, mPositionTarget.z));
                Vec3 mPositionMax(std::max(mPositionOrigin.x, mPositionTarget.x), std::max(mPositionOrigin.y, mPositionTarget.y), std::max(mPositionOrigin.z, mPositionTarget.z));

                this->getEventsByPosition(origin.getDimension()->getDimensionId(), [mPositionMin, mPositionMax](int x, int y, int z) -> bool {
                    return x >= static_cast<double>(mPositionMin.x) && x <= static_cast<double>(mPositionMax.x) && y >= static_cast<double>(mPositionMin.y) && 
                        y <= static_cast<double>(mPositionMax.y) && z >= static_cast<double>(mPositionMin.z) && z <= static_cast<double>(mPositionMax.z);
                }, param.Limit).transform([&output, &origin, limit = param.Limit](const std::vector<std::string>& result) -> void {
                    if (result.empty()) {
                        output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.behaviorevent.success.query")), limit, "None");

                        return;
                    }

                    output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.behaviorevent.success.query")), limit, fmt::join(result, ", "));
                }).or_else(modules::defaultErrorHandler<BehaviorEventPlugin>);
            });
        command.overload<operation>().text("back").text("range").required("PositionOrigin").required("Radius").required("Time").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param, Command const& cmd) -> void {
                Vec3 mPosition = param.PositionOrigin.getPosition(cmd.mVersion, origin, Vec3(0, 0, 0));

                this->getEventsByPosition(origin.getDimension()->getDimensionId(), [mPosition, radius = (param.Radius * param.Radius)](int x, int y, int z) -> bool {
                    return Vec3(x, y, z).distanceToSqr(mPosition) <= radius;
                }).and_then([this, time = param.Time](const std::vector<std::string>& areas) -> ll::Expected<std::vector<std::string>> {
                    return this->getEvents({{ "event_time", "" }}, [time](std::string value) -> bool {
                        std::string mTime = SystemUtils::toTimeCalculate(value, time * 3600, "0");
                        return !SystemUtils::isPastOrPresent(mTime);
                    }).transform([&areas](const std::vector<std::string>& times) -> std::vector<std::string> {
                        return SystemUtils::getIntersection({ areas, times });
                    });
                }).and_then([this](const std::vector<std::string>& result) -> ll::Expected<std::vector<std::string>> {
                    return this->getEvents({{ "event_type", "Operable" }})
                        .transform([&result](const std::vector<std::string>& types) -> std::vector<std::string> {
                            return SystemUtils::getIntersection({ result, types });
                        });
                }).and_then([this](const std::vector<std::string>& result) -> ll::Expected<std::vector<std::string>> {
                    return this->filter(result);
                }).and_then([this, &output, &origin](const std::vector<std::string>& result) -> ll::Expected<void> {
                    if (result.empty()) {
                        output.error(tr(origin.getLocaleCode(), "commands.behaviorevent.error.back"));

                        return {};
                    }

                    output.success(tr(origin.getLocaleCode(), "commands.behaviorevent.success.back"));

                    return this->back(result);
                }).or_else(modules::defaultErrorHandler<BehaviorEventPlugin>);
            });
        command.overload<operation>().text("back").text("position").required("PositionOrigin").required("PositionTarget").required("Time").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param, Command const& cmd) -> void {
                Vec3 mPositionOrigin = param.PositionOrigin.getPosition(cmd.mVersion, origin, Vec3(0, 0, 0));
                Vec3 mPositionTarget = param.PositionTarget.getPosition(cmd.mVersion, origin, Vec3(0, 0, 0));

                Vec3 mPositionMin(std::min(mPositionOrigin.x, mPositionTarget.x), std::min(mPositionOrigin.y, mPositionTarget.y), std::min(mPositionOrigin.z, mPositionTarget.z));
                Vec3 mPositionMax(std::max(mPositionOrigin.x, mPositionTarget.x), std::max(mPositionOrigin.y, mPositionTarget.y), std::max(mPositionOrigin.z, mPositionTarget.z));

                this->getEventsByPosition(origin.getDimension()->getDimensionId(), [mPositionMin, mPositionMax](int x, int y, int z) -> bool {
                    return x >= static_cast<double>(mPositionMin.x) && x <= static_cast<double>(mPositionMax.x) && y >= static_cast<double>(mPositionMin.y) && 
                        y <= static_cast<double>(mPositionMax.y) && z >= static_cast<double>(mPositionMin.z) && z <= static_cast<double>(mPositionMax.z);
                }).and_then([this, time = param.Time](const std::vector<std::string>& areas) -> ll::Expected<std::vector<std::string>> {
                    return this->getEvents({{ "event_time", "" }}, [time](std::string value) -> bool {
                        std::string mTime = SystemUtils::toTimeCalculate(value, time * 3600, "0");
                        return !SystemUtils::isPastOrPresent(mTime);
                    }).transform([&areas](const std::vector<std::string>& times) -> std::vector<std::string> {
                        return SystemUtils::getIntersection({ areas, times });
                    });
                }).and_then([this](const std::vector<std::string>& result) -> ll::Expected<std::vector<std::string>> {
                    return this->getEvents({{ "event_type", "Operable" }})
                        .transform([&result](const std::vector<std::string>& types) -> std::vector<std::string> {
                            return SystemUtils::getIntersection({ result, types });
                        });
                }).and_then([this](const std::vector<std::string>& result) -> ll::Expected<std::vector<std::string>> {
                    return this->filter(result);
                }).and_then([this, &output, &origin](const std::vector<std::string>& result) -> ll::Expected<void> {
                    if (result.empty()) {
                        output.error(tr(origin.getLocaleCode(), "commands.behaviorevent.error.back"));

                        return {};
                    }

                    output.success(tr(origin.getLocaleCode(), "commands.behaviorevent.success.back"));

                    return this->back(result);
                }).or_else(modules::defaultErrorHandler<BehaviorEventPlugin>);
            });
    }

    void BehaviorEventPlugin::listenEvent() {
        this->mImpl->WriteDatabaseTaskRunning.store(true, std::memory_order_release);
        this->mImpl->CleanDatabaseTaskRunning.store(true, std::memory_order_release);

        this->startWriteDatabaseTask();
        this->startCleanDatabaseTask();

        this->registeryEvent<ll::event::PlayerConnectEvent>("PlayerConnect", "Normal", "behaviorevent.event.playerconnect", [this](BehaviorEventConfig config) -> bool {
            switch (config) {
                case BehaviorEventConfig::ModuleEnabled: return this->mImpl->options.Events.onPlayerConnect.ModuleEnabled;
                case BehaviorEventConfig::RecordDatabase: return this->mImpl->options.Events.onPlayerConnect.RecordDatabase;
                case BehaviorEventConfig::OutputConsole: return this->mImpl->options.Events.onPlayerConnect.OutputConsole;
            }

            return false;
        }, [](ll::event::Event& event, Event& mEvent) -> void {
            auto& sevent = static_cast<ll::event::PlayerConnectEvent&>(event);

            mEvent.extendedFields.emplace_back("player_name", sevent.self().getRealName());
        }, [](std::string message, ll::event::Event& event) -> std::string {
            auto& sevent = static_cast<ll::event::PlayerConnectEvent&>(event);
            
            return fmt::format(fmt::runtime(message), sevent.self().getRealName(), sevent.self().getDimensionId().mValue,
                sevent.self().getPosition().x, sevent.self().getPosition().y, sevent.self().getPosition().z);
        });

        this->registeryEvent<ll::event::PlayerDisconnectEvent>("PlayerDisconnect", "Normal", "behaviorevent.event.playerdisconnect", [this](BehaviorEventConfig config) -> bool {
            switch (config) {
                case BehaviorEventConfig::ModuleEnabled: return this->mImpl->options.Events.onPlayerDisconnect.ModuleEnabled;
                case BehaviorEventConfig::RecordDatabase: return this->mImpl->options.Events.onPlayerDisconnect.RecordDatabase;
                case BehaviorEventConfig::OutputConsole: return this->mImpl->options.Events.onPlayerDisconnect.OutputConsole;
            }

            return false;
        }, [](ll::event::Event& event, Event& mEvent) -> void {
            auto& sevent = static_cast<ll::event::PlayerDisconnectEvent&>(event);

            mEvent.extendedFields.emplace_back("player_name", sevent.self().getRealName());
        }, [](std::string message, ll::event::Event& event) -> std::string {
            auto& sevent = static_cast<ll::event::PlayerDisconnectEvent&>(event);
            
            return fmt::format(fmt::runtime(message), sevent.self().getRealName(), sevent.self().getDimensionId().mValue,
                sevent.self().getPosition().x, sevent.self().getPosition().y, sevent.self().getPosition().z);
        });

        this->registeryEvent<ll::event::PlayerChatEvent>("PlayerChat", "Normal", "behaviorevent.event.playerchat", [this](BehaviorEventConfig config) -> bool {
            switch (config) {
                case BehaviorEventConfig::ModuleEnabled: return this->mImpl->options.Events.onPlayerChat.ModuleEnabled;
                case BehaviorEventConfig::RecordDatabase: return this->mImpl->options.Events.onPlayerChat.RecordDatabase;
                case BehaviorEventConfig::OutputConsole: return this->mImpl->options.Events.onPlayerChat.OutputConsole;
            }

            return false;
        }, [](ll::event::Event& event, Event& mEvent) -> void {
            auto& sevent = static_cast<ll::event::PlayerChatEvent&>(event);

            mEvent.extendedFields.emplace_back("event_message", sevent.message());
            mEvent.extendedFields.emplace_back("player_name", sevent.self().getRealName());
        }, [](std::string message, ll::event::Event& event) -> std::string {
            auto& sevent = static_cast<ll::event::PlayerChatEvent&>(event);
            
            return fmt::format(fmt::runtime(message), sevent.self().getRealName(), sevent.message());
        });

        this->registeryEvent<ll::event::PlayerAddExperienceEvent>("PlayerAddExperience", "Normal", "behaviorevent.event.playeraddexperience", [this](BehaviorEventConfig config) -> bool {
            switch (config) {
                case BehaviorEventConfig::ModuleEnabled: return this->mImpl->options.Events.onPlayerAddExperience.ModuleEnabled;
                case BehaviorEventConfig::RecordDatabase: return this->mImpl->options.Events.onPlayerAddExperience.RecordDatabase;
                case BehaviorEventConfig::OutputConsole: return this->mImpl->options.Events.onPlayerAddExperience.OutputConsole;
            }

            return false;
        }, [](ll::event::Event& event, Event& mEvent) -> void {
            auto& sevent = static_cast<ll::event::PlayerAddExperienceEvent&>(event);

            mEvent.extendedFields.emplace_back("event_experience", std::to_string(sevent.experience()));
            mEvent.extendedFields.emplace_back("player_name", sevent.self().getRealName());
        }, [](std::string message, ll::event::Event& event) -> std::string {
            auto& sevent = static_cast<ll::event::PlayerAddExperienceEvent&>(event);
            
            return fmt::format(fmt::runtime(message), sevent.self().getRealName(), sevent.experience());
        });

        this->registeryEvent<ll::event::PlayerAttackEvent>("PlayerAttack", "Normal", "behaviorevent.event.playerattack", [this](BehaviorEventConfig config) -> bool {
            switch (config) {
                case BehaviorEventConfig::ModuleEnabled: return this->mImpl->options.Events.onPlayerAttack.ModuleEnabled;
                case BehaviorEventConfig::RecordDatabase: return this->mImpl->options.Events.onPlayerAttack.RecordDatabase;
                case BehaviorEventConfig::OutputConsole: return this->mImpl->options.Events.onPlayerAttack.OutputConsole;
            }

            return false;
        }, [](ll::event::Event& event, Event& mEvent) -> void {
            auto& sevent = static_cast<ll::event::PlayerAttackEvent&>(event);

            mEvent.extendedFields.emplace_back("event_target", sevent.target().getTypeName());
            mEvent.extendedFields.emplace_back("event_cause", magic_enum::enum_name(sevent.cause()).data());
            mEvent.extendedFields.emplace_back("player_name", sevent.self().getRealName());
        }, [](std::string message, ll::event::Event& event) -> std::string {
            auto& sevent = static_cast<ll::event::PlayerAttackEvent&>(event);
            
            return fmt::format(fmt::runtime(message), sevent.self().getRealName(), magic_enum::enum_name(sevent.cause()).data(), sevent.target().getTypeName());
        });

        this->registeryEvent<ll::event::PlayerChangePermEvent>("PlayerChangePerm", "Normal", "behaviorevent.event.playerchangeperm", [this](BehaviorEventConfig config) -> bool {
            switch (config) {
                case BehaviorEventConfig::ModuleEnabled: return this->mImpl->options.Events.onPlayerChangePerm.ModuleEnabled;
                case BehaviorEventConfig::RecordDatabase: return this->mImpl->options.Events.onPlayerChangePerm.RecordDatabase;
                case BehaviorEventConfig::OutputConsole: return this->mImpl->options.Events.onPlayerChangePerm.OutputConsole;
            }

            return false;
        }, [](ll::event::Event& event, Event& mEvent) -> void {
            auto& sevent = static_cast<ll::event::PlayerChangePermEvent&>(event);

            mEvent.extendedFields.emplace_back("event_perm", magic_enum::enum_name(sevent.newPerm()).data());
            mEvent.extendedFields.emplace_back("player_name", sevent.self().getRealName());
        }, [](std::string message, ll::event::Event& event) -> std::string {
            auto& sevent = static_cast<ll::event::PlayerChangePermEvent&>(event);
            
            return fmt::format(fmt::runtime(message), sevent.self().getRealName(), magic_enum::enum_name(sevent.newPerm()).data());
        });

        this->registeryEvent<ll::event::PlayerDestroyBlockEvent>("PlayerDestroyBlock", "Operable", "behaviorevent.event.playerdestroyblock", [this](BehaviorEventConfig config) -> bool {
            switch (config) {
                case BehaviorEventConfig::ModuleEnabled: return this->mImpl->options.Events.onPlayerDestroyBlock.ModuleEnabled;
                case BehaviorEventConfig::RecordDatabase: return this->mImpl->options.Events.onPlayerDestroyBlock.RecordDatabase;
                case BehaviorEventConfig::OutputConsole: return this->mImpl->options.Events.onPlayerDestroyBlock.OutputConsole;
            }

            return false;
        }, [](ll::event::Event& event, Event& mEvent) -> void {
            auto& sevent = static_cast<ll::event::PlayerDestroyBlockEvent&>(event);

            if (auto mBlock = BlockUtils::getBlock(sevent.pos(), sevent.self().getDimensionId()); mBlock.has_value())
                mEvent.extendedFields.emplace_back("event_operable", mBlock.value()->mSerializationId->toSnbt(SnbtFormat::Minimize, 0));
            if (auto mBlockEntity = BlockUtils::getBlockEntity(sevent.pos(), sevent.self().getDimensionId()); mBlockEntity.has_value()) {
                CompoundTag mTag;
                mBlockEntity.value()->save(mTag, *SaveContextFactory::createCloneSaveContext());

                mEvent.extendedFields.emplace_back("event_operable_entity", mTag.toSnbt(SnbtFormat::Minimize, 0));
            }
            mEvent.extendedFields.emplace_back("player_name", sevent.self().getRealName());
        }, [](std::string message, ll::event::Event& event) -> std::string {
            auto& sevent = static_cast<ll::event::PlayerDestroyBlockEvent&>(event);

            std::string mBlockType;
            if (auto mBlock = BlockUtils::getBlock(sevent.pos(), sevent.self().getDimensionId()); mBlock.has_value())
                mBlockType = mBlock.value()->getTypeName();
            
            return fmt::format(fmt::runtime(message), sevent.self().getRealName(), sevent.self().getDimensionId().mValue,
                sevent.pos().x, sevent.pos().y, sevent.pos().z, mBlockType.empty() ? "Unknown" : mBlockType);
        });

        this->registeryEvent<ll::event::PlayerPlacedBlockEvent>("PlayerPlaceBlock", "Operable", "behaviorevent.event.playerplaceblock", [this](BehaviorEventConfig config) -> bool {
            switch (config) {
                case BehaviorEventConfig::ModuleEnabled: return this->mImpl->options.Events.onPlayerPlaceBlock.ModuleEnabled;
                case BehaviorEventConfig::RecordDatabase: return this->mImpl->options.Events.onPlayerPlaceBlock.RecordDatabase;
                case BehaviorEventConfig::OutputConsole: return this->mImpl->options.Events.onPlayerPlaceBlock.OutputConsole;
            }

            return false;
        }, [](ll::event::Event& event, Event& mEvent) -> void {
            auto& sevent = static_cast<ll::event::PlayerPlacedBlockEvent&>(event);

            HashedString mHashedId("minecraft:air");

            mEvent.extendedFields.emplace_back("event_operable", Block::tryGetFromRegistry(mHashedId).value().mSerializationId->toSnbt(SnbtFormat::Minimize, 0));
            mEvent.extendedFields.emplace_back("player_name", sevent.self().getRealName());
        }, [](std::string message, ll::event::Event& event) -> std::string {
            auto& sevent = static_cast<ll::event::PlayerPlacedBlockEvent&>(event);

            BlockPos mPosition = sevent.pos();
            
            return fmt::format(fmt::runtime(message), sevent.self().getRealName(), sevent.self().getDimensionId().mValue,
                mPosition.x, mPosition.y, mPosition.z, sevent.self().getCarriedItem().getTypeName());
        });

        this->registeryEvent<ll::event::PlayerDieEvent>("PlayerDie", "Normal", "behaviorevent.event.playerdie", [this](BehaviorEventConfig config) -> bool {
            switch (config) {
                case BehaviorEventConfig::ModuleEnabled: return this->mImpl->options.Events.onPlayerDie.ModuleEnabled;
                case BehaviorEventConfig::RecordDatabase: return this->mImpl->options.Events.onPlayerDie.RecordDatabase;
                case BehaviorEventConfig::OutputConsole: return this->mImpl->options.Events.onPlayerDie.OutputConsole;
            }

            return false;
        }, [](ll::event::Event& event, Event& mEvent) -> void {
            auto& sevent = static_cast<ll::event::PlayerDieEvent&>(event);

            mEvent.extendedFields.emplace_back("event_cause", magic_enum::enum_name(sevent.source().mCause).data());
            mEvent.extendedFields.emplace_back("player_name", sevent.self().getRealName());
        }, [](std::string message, ll::event::Event& event) -> std::string {
            auto& sevent = static_cast<ll::event::PlayerDieEvent&>(event);
            
            return fmt::format(fmt::runtime(message), sevent.self().getRealName(), magic_enum::enum_name(sevent.source().mCause).data(),
                sevent.self().getDimensionId().mValue, sevent.self().getPosition().x, sevent.self().getPosition().y, sevent.self().getPosition().z);
        });

        this->registeryEvent<ll::event::PlayerPickUpItemEvent>("PlayerPickUpItem", "Normal", "behaviorevent.event.playerpickupitem", [this](BehaviorEventConfig config) -> bool {
            switch (config) {
                case BehaviorEventConfig::ModuleEnabled: return this->mImpl->options.Events.onPlayerPickUpItem.ModuleEnabled;
                case BehaviorEventConfig::RecordDatabase: return this->mImpl->options.Events.onPlayerPickUpItem.RecordDatabase;
                case BehaviorEventConfig::OutputConsole: return this->mImpl->options.Events.onPlayerPickUpItem.OutputConsole;
            }

            return false;
        }, [](ll::event::Event& event, Event& mEvent) -> void {
            auto& sevent = static_cast<ll::event::PlayerPickUpItemEvent&>(event);

            mEvent.extendedFields.emplace_back("event_item", sevent.itemActor().item().save(*SaveContextFactory::createCloneSaveContext())->toSnbt(SnbtFormat::Minimize, 0));
            mEvent.extendedFields.emplace_back("event_org_count", std::to_string(sevent.orgCount()));
            mEvent.extendedFields.emplace_back("event_favored_slot", std::to_string(sevent.favoredSlot()));
            mEvent.extendedFields.emplace_back("player_name", sevent.self().getRealName());
        }, [](std::string message, ll::event::Event& event) -> std::string {
            auto& sevent = static_cast<ll::event::PlayerPickUpItemEvent&>(event);

            return fmt::format(fmt::runtime(message), sevent.self().getRealName(), sevent.self().getDimensionId().mValue,
                sevent.self().getPosition().x, sevent.self().getPosition().y, sevent.self().getPosition().z, sevent.itemActor().item().getTypeName());
        });

        this->registeryEvent<ll::event::PlayerRespawnEvent>("PlayerRespawn", "Normal", "behaviorevent.event.playerrespawn", [this](BehaviorEventConfig config) -> bool {
            switch (config) {
                case BehaviorEventConfig::ModuleEnabled: return this->mImpl->options.Events.onPlayerRespawn.ModuleEnabled;
                case BehaviorEventConfig::RecordDatabase: return this->mImpl->options.Events.onPlayerRespawn.RecordDatabase;
                case BehaviorEventConfig::OutputConsole: return this->mImpl->options.Events.onPlayerRespawn.OutputConsole;
            }

            return false;
        }, [](ll::event::Event& event, Event& mEvent) -> void {
            auto& sevent = static_cast<ll::event::PlayerRespawnEvent&>(event);

            mEvent.extendedFields.emplace_back("player_name", sevent.self().getRealName());
        }, [](std::string message, ll::event::Event& event) -> std::string {
            auto& sevent = static_cast<ll::event::PlayerRespawnEvent&>(event);
            
            return fmt::format(fmt::runtime(message), sevent.self().getRealName(), sevent.self().getDimensionId().mValue,
                sevent.self().getPosition().x, sevent.self().getPosition().y, sevent.self().getPosition().z);
        });
        
        this->registeryEvent<ll::event::PlayerUseItemEvent>("PlayerUseItem", "Normal", "behaviorevent.event.playeruseitem", [this](BehaviorEventConfig config) -> bool {
            switch (config) {
                case BehaviorEventConfig::ModuleEnabled: return this->mImpl->options.Events.onPlayerUseItem.ModuleEnabled;
                case BehaviorEventConfig::RecordDatabase: return this->mImpl->options.Events.onPlayerUseItem.RecordDatabase;
                case BehaviorEventConfig::OutputConsole: return this->mImpl->options.Events.onPlayerUseItem.OutputConsole;
            }

            return false;
        }, [](ll::event::Event& event, Event& mEvent) -> void {
            auto& sevent = static_cast<ll::event::PlayerUseItemEvent&>(event);

            mEvent.extendedFields.emplace_back("event_item", sevent.item().save(*SaveContextFactory::createCloneSaveContext())->toSnbt(SnbtFormat::Minimize, 0));
            mEvent.extendedFields.emplace_back("player_name", sevent.self().getRealName());
        }, [](std::string message, ll::event::Event& event) -> std::string {
            auto& sevent = static_cast<ll::event::PlayerUseItemEvent&>(event);

            return fmt::format(fmt::runtime(message), sevent.self().getRealName(), sevent.self().getDimensionId().mValue,
                sevent.self().getPosition().x, sevent.self().getPosition().y, sevent.self().getPosition().z, sevent.item().getTypeName());
        });

        this->registeryEvent<LOICollection::server::Events::PlayerOpenContainerEvent>("PlayerOpenContainer", "Operable", "behaviorevent.event.playeropencontainer", [this](BehaviorEventConfig config) -> bool {
            switch (config) {
                case BehaviorEventConfig::ModuleEnabled: return this->mImpl->options.Events.onPlayerContainerInteract.ModuleEnabled;
                case BehaviorEventConfig::RecordDatabase: return this->mImpl->options.Events.onPlayerContainerInteract.RecordDatabase;
                case BehaviorEventConfig::OutputConsole: return this->mImpl->options.Events.onPlayerContainerInteract.OutputConsole;
            }

            return false;
        }, [](ll::event::Event& event, Event& mEvent) -> void {
            auto& sevent = static_cast<LOICollection::server::Events::PlayerOpenContainerEvent&>(event);

            if (auto mBlockEntity = BlockUtils::getBlockEntity(sevent.getPosition(), sevent.getDimensionId()); mBlockEntity.has_value()) {
                CompoundTag mTag;
                mBlockEntity.value()->save(mTag, *SaveContextFactory::createCloneSaveContext());

                mEvent.extendedFields.emplace_back("event_operable_entity", mTag.toSnbt(SnbtFormat::Minimize, 0));
            }
            mEvent.extendedFields.emplace_back("player_name", sevent.self().getRealName());
        }, [](std::string message, ll::event::Event& event) -> std::string {
            auto& sevent = static_cast<LOICollection::server::Events::PlayerOpenContainerEvent&>(event);

            return fmt::format(fmt::runtime(message), sevent.self().getRealName(), sevent.getDimensionId(),
                sevent.getPosition().x, sevent.getPosition().y, sevent.getPosition().z);
        });

        this->registeryEvent<LOICollection::server::Events::BlockExplodedEvent>("BlockExplode", "Operable", "behaviorevent.event.blockexplode", [this](BehaviorEventConfig config) -> bool {
            switch (config) {
                case BehaviorEventConfig::ModuleEnabled: return this->mImpl->options.Events.onBlockExplode.ModuleEnabled;
                case BehaviorEventConfig::RecordDatabase: return this->mImpl->options.Events.onBlockExplode.RecordDatabase;
                case BehaviorEventConfig::OutputConsole: return this->mImpl->options.Events.onBlockExplode.OutputConsole;
            }

            return false;
        }, [](ll::event::Event& event, Event& mEvent) -> void {
            auto& sevent = static_cast<LOICollection::server::Events::BlockExplodedEvent&>(event);

            mEvent.extendedFields.emplace_back("event_operable", sevent.getBlock().mSerializationId->toSnbt(SnbtFormat::Minimize, 0));
            if (auto mBlockEntity = BlockUtils::getBlockEntity(sevent.getPosition(), sevent.getDimensionId()); mBlockEntity.has_value()) {
                CompoundTag mTag;
                mBlockEntity.value()->save(mTag, *SaveContextFactory::createCloneSaveContext());

                mEvent.extendedFields.emplace_back("event_operable_entity", mTag.toSnbt(SnbtFormat::Minimize, 0));
            }
        }, [](std::string message, ll::event::Event& event) -> std::string {
            auto& sevent = static_cast<LOICollection::server::Events::BlockExplodedEvent&>(event);

            return fmt::format(fmt::runtime(message), sevent.getBlock().getTypeName(), sevent.getDimensionId(),
                sevent.getPosition().x, sevent.getPosition().y, sevent.getPosition().z);
        });
    }

    void BehaviorEventPlugin::unlistenEvent() {
        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        for (auto& listener : this->mImpl->mListeners)
            eventBus.removeListener(listener.second);

        this->mImpl->mListeners.clear();

        this->mImpl->WriteDatabaseTaskRunning.store(false, std::memory_order_release);
        this->mImpl->CleanDatabaseTaskRunning.store(false, std::memory_order_release);

        this->mImpl->mTimerManager->cancelAll();
    }

    ll::Expected<void> BehaviorEventPlugin::setExecutor(const ll::coro::Executor& executor) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(BehaviorEventPluginErrorCode::Invalid));

        this->mImpl->mTimerManager->setExecutor(executor);

        this->startWriteDatabaseTask();
        this->startCleanDatabaseTask();

        return {};
    }

    ll::Expected<BehaviorEventPlugin::Event> BehaviorEventPlugin::getBasicEvent(const std::string& name, const std::string& type, const Vec3& position, int dimension) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(BehaviorEventPluginErrorCode::Invalid));

        Event mEvent;
        mEvent.eventName = name;
        mEvent.eventTime = SystemUtils::getNowTime();
        mEvent.eventType = type;
        mEvent.posX = static_cast<int>(position.x);
        mEvent.posY = static_cast<int>(position.y);
        mEvent.posZ = static_cast<int>(position.z);
        mEvent.dimension = dimension;

        return mEvent;
    }

    ll::Expected<std::vector<std::string>> BehaviorEventPlugin::getEvents(int limit) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(BehaviorEventPluginErrorCode::Invalid));

        return this->getDatabase()->list("Events")
            .transform([limit](const std::vector<std::string>& keys) -> std::vector<std::string> {
                return keys
                    | std::views::take(limit > 0 ? limit : static_cast<int>(keys.size()))
                    | std::ranges::to<std::vector<std::string>>();
            });
    }

    ll::Expected<std::vector<std::string>> BehaviorEventPlugin::getEvents(std::vector<std::pair<std::string, std::string>> conditions, std::function<bool(std::string)> filter, int limit) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(BehaviorEventPluginErrorCode::Invalid));

        return this->getEvents(limit)
            .and_then([this](const std::vector<std::string>& keys) -> ll::Expected<std::unordered_map<std::string, std::unordered_map<std::string, std::string>>> {
                return this->getDatabase()->get("Events", keys);
            })
            .transform([conditions = std::move(conditions), filter = std::move(filter)](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> mData) -> std::vector<std::string> {
                std::vector<std::string> result;
                for (auto& [id, data] : mData) {
                    auto view = conditions | std::views::filter([&data, filter](const std::pair<std::string, std::string>& condition) -> bool {
                        auto it = data.find(condition.first);
                        if (it == data.end())
                            return false;

                        return (!filter && it->second == condition.second) || (filter && filter(it->second));
                    });

                    if (static_cast<size_t>(std::ranges::distance(view)) == conditions.size())
                        result.emplace_back(id);
                }

                return result;
            });
    }

    ll::Expected<std::vector<std::string>> BehaviorEventPlugin::getEventsByPosition(int dimension, std::function<bool(int x, int y, int z)> filter, int limit) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(BehaviorEventPluginErrorCode::Invalid));

        return this->getEvents(limit)
            .and_then([this](const std::vector<std::string>& keys) -> ll::Expected<std::unordered_map<std::string, std::unordered_map<std::string, std::string>>> {
                return this->getDatabase()->get("Events", keys);
            })
            .transform([dimension, filter = std::move(filter)](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> mData) -> std::vector<std::string> {
                std::vector<std::string> mResult;
                for (auto& [id, data] : mData) {
                    if (SystemUtils::toInt(data.at("position_dimension"), 0) != dimension)
                        continue;

                    int x = SystemUtils::toInt(data.at("position_x"), 0);
                    int y = SystemUtils::toInt(data.at("position_y"), 0);
                    int z = SystemUtils::toInt(data.at("position_z"), 0);

                    if (filter(x, y, z))
                        mResult.emplace_back(id);
                }

                return mResult;
            });
    }

    ll::Expected<std::vector<std::string>> BehaviorEventPlugin::filter(std::vector<std::string> ids) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(BehaviorEventPluginErrorCode::Invalid));

        return this->getDatabase()->get("Events", ids)
            .transform([](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> mData) -> std::vector<std::string> {
                std::unordered_map<std::string, std::string> events;
                for (auto& [key, data] : mData) {
                    std::string id = std::format("{}_{}.{}.{}.{}:{}", 
                        data.at("event_name"),
                        data.at("event_time"),
                        data.at("position_x"),
                        data.at("position_y"),
                        data.at("position_z"),
                        data.at("position_dimension")
                    );

                    auto it = events.find(id);
                    if (it == events.end())
                        events[id] = key;
                }

                return std::views::values(events) | std::ranges::to<std::vector<std::string>>();
            });
    }

    ll::Expected<void> BehaviorEventPlugin::write(const std::string& id, const Event& event) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(BehaviorEventPluginErrorCode::Invalid));

        std::unordered_map<std::string, std::string> mData = {
            { "event_name", event.eventName },
            { "event_time", event.eventTime },
            { "event_type", event.eventType },
            { "position_x", std::to_string(event.posX) },
            { "position_y", std::to_string(event.posY) },
            { "position_z", std::to_string(event.posZ) },
            { "position_dimension", std::to_string(event.dimension) }
        };

        for (auto& fieled : event.extendedFields)
            mData[fieled.first] = fieled.second;

        return this->getDatabase()->set("Events", id, mData);
    }

    ll::Expected<void> BehaviorEventPlugin::back(const std::vector<std::string>& ids) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(BehaviorEventPluginErrorCode::Invalid));

        std::vector<std::vector<std::string>> chunks = ids
            | std::views::chunk(std::max(this->mImpl->options.SingleBacktrackingQuantity, 1))
            | std::ranges::to<std::vector<std::vector<std::string>>>();

        ll::coro::keepThis([this, chunks]() -> ll::coro::CoroTask<> {
            for (auto& chunk : chunks) {
                co_await ll::chrono::ticks(1);

                this->getDatabase()->get("Events", chunk)
                    .and_then([this, chunk](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> mData) -> ll::Expected<void> {
                        for (auto& [id, data] : mData) {
                            BlockPos mPosition(
                                SystemUtils::toInt(data.at("position_x"), 0),
                                SystemUtils::toInt(data.at("position_y"), 0),
                                SystemUtils::toInt(data.at("position_z"), 0)
                            );
                            int mDimension = SystemUtils::toInt(data.at("position_dimension"), 0);

                            if (data.contains("event_operable")) {
                                CompoundTag mNbt = CompoundTag::fromSnbt(data.at("event_operable"))->mTags;

                                BlockUtils::setBlock(mPosition, mDimension, mNbt);
                            } else if (data.contains("event_operable_entity")) {
                                CompoundTag mNbt = CompoundTag::fromSnbt(data.at("event_operable_entity"))->mTags;

                                BlockUtils::setBlockEntity(mPosition, mDimension, mNbt);
                            }
                        }

                        return this->getDatabase()->del("Events", chunk);
                    })
                    .or_else(modules::defaultErrorHandler<BehaviorEventPlugin>);
            }

            co_return;
        }).launch(ll::thread::ServerThreadExecutor::getDefault());

        return {};
    }

    ll::Expected<void> BehaviorEventPlugin::clean(int hours) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(BehaviorEventPluginErrorCode::Invalid));

        return this->getEvents()
            .and_then([this](const std::vector<std::string>& ids) -> ll::Expected<std::unordered_map<std::string, std::unordered_map<std::string, std::string>>> {
                return this->getDatabase()->get("Events", ids);
            })
            .and_then([this, hours](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> mData) -> ll::Expected<void> {
                std::vector<std::string> ids;
                for (auto& [id, data] : mData) {
                    std::string time = SystemUtils::toTimeCalculate(data.at("event_time"), hours * 3600, "0");
                    if (SystemUtils::isPastOrPresent(time))
                        ids.emplace_back(id);
                }

                if (!ids.empty())
                    return this->getDatabase()->del("Events", ids);

                return {};
            });
    }

    bool BehaviorEventPlugin::isValid() {
        return this->getLogger() != nullptr && this->getDatabase() != nullptr;
    }

    std::string BehaviorEventPlugin::getName() {
        return "BehaviorEventPlugin";
    }

    modules::ModulePriority BehaviorEventPlugin::getPriority() {
        return modules::ModulePriority::High;
    }

    ll::Expected<bool> BehaviorEventPlugin::load() {
        if (!ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.BehaviorEvent.ModuleEnabled)
            return false;

        auto mDataPath = std::filesystem::path(ServiceProvider::getInstance().getService<std::string>("DataPath")->data());

        if (auto repo = BlockRepository::open((mDataPath / "behaviorevent.db").string(), 4); !repo)
            return ll::makeStringError(repo.error().message());
        else
            this->mImpl->db = std::move(*repo);

        this->mImpl->logger = ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA");
        this->mImpl->options = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.BehaviorEvent;

        return true;
    }

    ll::Expected<bool> BehaviorEventPlugin::unload() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        this->mImpl->db.reset();
        this->mImpl->logger.reset();
        this->mImpl->options = {};

        if (this->mImpl->mRegistered.load(std::memory_order_acquire))
            this->unlistenEvent();

        return true;
    }

    ll::Expected<bool> BehaviorEventPlugin::registry() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        return this->getDatabase()->create("Events", [](BlockRepository::ColumnCallback ctor) -> void {
            ctor("event_name");
            ctor("event_time");
            ctor("event_type");
            ctor("position_x");
            ctor("position_y");
            ctor("position_z");
            ctor("position_dimension");
            ctor("event_operable");
            ctor("event_operable_entity");
            ctor("event_item");
            ctor("event_org_count");
            ctor("event_favored_slot");
            ctor("event_cause");
            ctor("event_perm");
            ctor("event_target");
            ctor("event_experience");
            ctor("event_message");
            ctor("player_name");
        }).transform([this]() -> bool {
            this->registeryCommand();
            this->listenEvent();

            this->mImpl->mRegistered.store(true, std::memory_order_release);

            return true;
        });
    }
    
    ll::Expected<bool> BehaviorEventPlugin::unregistry() {
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
