#pragma once

#include <memory>
#include <string>
#include <vector>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/include/ModuleBase.h"
#include "LOICollectionA/include/ModManager.h"

#include "LOICollectionA/include/server/Plugins/types/statistic/StatisticCommonType.h"

class Player;
class BlockRepository;

namespace ll {
    namespace io {
        class Logger;
    }

    namespace coro {
        class Executor;
    }
}

namespace LOICollection::server::Plugins {
    enum class StatisticsPluginErrorCode : int {
        Invalid = 1
    };

    struct StatisticsPluginErrorCategory : std::error_category {
        [[nodiscard]] const char* name() const noexcept override {
            return "BehaviorEventPluginError";
        }

        [[nodiscard]] std::string message(int ev) const override {
            switch (static_cast<StatisticsPluginErrorCode>(ev)) {
                case StatisticsPluginErrorCode::Invalid: return "Plugin is invalid";
                default:
                    return "Unknown";
            }
        }
    };

    class StatisticsPlugin : public std::enable_shared_from_this<StatisticsPlugin>,
                             public modules::ModuleBase,
                             public modules::AutoRegister<StatisticsPlugin> {
    public:
        ~StatisticsPlugin();

        StatisticsPlugin(StatisticsPlugin const&) = delete;
        StatisticsPlugin(StatisticsPlugin&&) = delete;
        StatisticsPlugin& operator=(StatisticsPlugin const&) = delete;
        StatisticsPlugin& operator=(StatisticsPlugin&&) = delete;

    public:
        LOICOLLECTION_A_NDAPI static std::shared_ptr<StatisticsPlugin> getShared();
        LOICOLLECTION_A_NDAPI static std::error_code makeErrorCode(StatisticsPluginErrorCode e);

        LOICOLLECTION_A_NDAPI std::shared_ptr<BlockRepository> getDatabase();
        LOICOLLECTION_A_NDAPI std::shared_ptr<ll::io::Logger> getLogger();
        
        LOICOLLECTION_A_NDAPI ll::Expected<void> setExecutor(const ll::coro::Executor& executor);
        
        LOICOLLECTION_A_NDAPI ll::Expected<std::string> getStatisticName(StatisticType type);
        LOICOLLECTION_A_NDAPI ll::Expected<std::string> getPlayerInfo(const std::string& uuid);

        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::pair<std::string, int>>> getRankingList(StatisticType type, int limit = -1);
        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::pair<std::string, int>>> getStatistics(StatisticType type, int limit = -1);

        LOICOLLECTION_A_NDAPI ll::Expected<int> getStatistic(const std::string& uuid, StatisticType type);
        LOICOLLECTION_A_NDAPI ll::Expected<int> getStatistic(Player& player, StatisticType type);

        LOICOLLECTION_A_NDAPI ll::Expected<void> addStatistic(Player& player, StatisticType type, int value);
        
        LOICOLLECTION_A_NDAPI bool isValid();

    public:
        LOICOLLECTION_A_NDAPI int getRankingPlayerCount();

    public:
        LOICOLLECTION_A_NDAPI std::string getName() override;

        LOICOLLECTION_A_NDAPI modules::ModulePriority getPriority() override;

        LOICOLLECTION_A_API   ll::Expected<bool> load() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unload() override;
        LOICOLLECTION_A_API   ll::Expected<bool> registry() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unregistry() override;

    private:
        StatisticsPlugin();

        void startWirteDatabaseTask();

        ll::Expected<void> registeryUI();
        
        void registeryCommand();
        void listenEvent();
        void unlistenEvent();

        struct operation;

        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
