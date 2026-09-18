#pragma once

#include <memory>
#include <string>
#include <vector> 
#include <utility>
#include <functional>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"
#include "LOICollectionA/base/Ownership.h"

#include "LOICollectionA/include/ModuleBase.h"
#include "LOICollectionA/include/ModManager.h"

class Vec3;
class BehaviorEventLog;
class BlockStore;
class ConnectionPool;

namespace ll {
    namespace event {
        class Event;
    }

    namespace io {
        class Logger;
    }

    namespace coro {
        class Executor;
    }
}

namespace LOICollection::server::Plugins {
    enum class BehaviorEventConfig {
        ModuleEnabled,
        RecordDatabase,
        OutputConsole
    };

    enum class BehaviorEventPluginErrorCode : int {
        Invalid = 1
    };

    struct BehaviorEventPluginErrorCategory : std::error_category {
        [[nodiscard]] const char* name() const noexcept override {
            return "BehaviorEventPluginError";
        }

        [[nodiscard]] std::string message(int ev) const override {
            switch (static_cast<BehaviorEventPluginErrorCode>(ev)) {
                case BehaviorEventPluginErrorCode::Invalid: return "Plugin is invalid";
                default:
                    return "Unknown";
            }
        }
    };

    class BehaviorEventPlugin : public std::enable_shared_from_this<BehaviorEventPlugin>,
                                public modules::ModuleBase,
                                public modules::AutoRegister<BehaviorEventPlugin> {
    public:
        struct Event;

        ~BehaviorEventPlugin();

        BehaviorEventPlugin(BehaviorEventPlugin const&) = delete;
        BehaviorEventPlugin(BehaviorEventPlugin&&) = delete;
        BehaviorEventPlugin& operator=(BehaviorEventPlugin const&) = delete;
        BehaviorEventPlugin& operator=(BehaviorEventPlugin&&) = delete;

    public:
        LOICOLLECTION_A_NDAPI static std::shared_ptr<BehaviorEventPlugin> getShared();
        LOICOLLECTION_A_NDAPI static std::error_code makeErrorCode(BehaviorEventPluginErrorCode e);

        LOICOLLECTION_A_NDAPI observer<BehaviorEventLog> getBehaviorEventLog();
        LOICOLLECTION_A_NDAPI std::shared_ptr<ll::io::Logger> getLogger();

        LOICOLLECTION_A_NDAPI ll::Expected<void> setExecutor(const ll::coro::Executor& executor);

        LOICOLLECTION_A_NDAPI ll::Expected<Event> getBasicEvent(const std::string& name, const std::string& type, const Vec3& position, int dimension);

        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getEvents(int limit = -1);
        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getEventsWithin(int hours, int limit = -1);
        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getEvents(std::vector<std::pair<std::string, std::string>> conditions, std::function<bool(std::string)> filter = {}, int limit = -1);
        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getEventsByPosition(int dimension, std::function<bool(int x, int y, int z)> filter, int limit = -1);
        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> filter(std::vector<std::string> ids);

        LOICOLLECTION_A_NDAPI ll::Expected<std::string> write(const Event& event);
        LOICOLLECTION_A_NDAPI ll::Expected<void> back(const std::vector<std::string>& ids);
        LOICOLLECTION_A_NDAPI ll::Expected<void> clean(int hours);

        LOICOLLECTION_A_NDAPI bool isValid();
    
    public:
        LOICOLLECTION_A_NDAPI std::string getName() override;

        LOICOLLECTION_A_NDAPI modules::ModulePriority getPriority() override;

        LOICOLLECTION_A_API   ll::Expected<bool> load() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unload() override;
        LOICOLLECTION_A_API   ll::Expected<bool> registry() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unregistry() override;

    private:
        BehaviorEventPlugin();

        template <typename T>
        void registeryEvent(
            const std::string& name,
            const std::string& type,
            const std::string& id,
            std::function<bool(BehaviorEventConfig)> config,
            std::function<void(ll::event::Event&, Event&)> process,
            std::function<std::string(std::string, ll::event::Event&)> formatter
        );

        void startWriteDatabaseTask();
        void startCleanDatabaseTask();

        void registeryCommand();
        void listenEvent();
        void unlistenEvent();

        struct operation;

        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };

    struct BehaviorEventPlugin::Event {
        std::string eventName;
        std::string eventTime;
        std::string eventType;

        int posX;
        int posY;
        int posZ;
        int dimension;

        std::vector<std::pair<std::string, std::string>> extendedFields;
    };
}