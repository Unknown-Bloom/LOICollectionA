#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/include/ModuleBase.h"
#include "LOICollectionA/include/ModManager.h"

#include "LOICollectionA/include/server/Plugins/types/TpaType.h"

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
    enum class TpaPluginErrorCode : int {
        Invalid = 1,
        RequestExists = 2,
        RequestNotFound = 3,
        BlacklistNotFound = 4
    };

    struct TpaPluginErrorCategory : std::error_category {
        [[nodiscard]] const char* name() const noexcept override {
            return "TpaPluginError";
        }

        [[nodiscard]] std::string message(int ev) const override {
            switch (static_cast<TpaPluginErrorCode>(ev)) {
                case TpaPluginErrorCode::Invalid: return "Plugin is invalid";
                case TpaPluginErrorCode::RequestExists: return "Request already exists";
                case TpaPluginErrorCode::RequestNotFound: return "Request not found";
                case TpaPluginErrorCode::BlacklistNotFound: return "Blacklist data not found";
                default:
                    return "Unknown";
            }
        }
    };

    class TpaPlugin : public std::enable_shared_from_this<TpaPlugin>,
                      public modules::ModuleBase,
                      public modules::AutoRegister<TpaPlugin> {
    public:
        ~TpaPlugin();

        TpaPlugin(TpaPlugin const&) = delete;
        TpaPlugin(TpaPlugin&&) = delete;
        TpaPlugin& operator=(TpaPlugin const&) = delete;
        TpaPlugin& operator=(TpaPlugin&&) = delete;

    public:
        LOICOLLECTION_A_NDAPI static std::shared_ptr<TpaPlugin> getShared();
        LOICOLLECTION_A_NDAPI static std::error_code makeErrorCode(TpaPluginErrorCode e);

        LOICOLLECTION_A_NDAPI std::shared_ptr<BlockRepository> getDatabase();
        LOICOLLECTION_A_NDAPI std::shared_ptr<ll::io::Logger> getLogger();

        LOICOLLECTION_A_NDAPI ll::Expected<void> setInvite(Player& player, bool invite);

        LOICOLLECTION_A_NDAPI ll::Expected<void> addBlacklist(Player& player, Player& target);
        LOICOLLECTION_A_NDAPI ll::Expected<void> delBlacklist(Player& player, const std::string& id);

        LOICOLLECTION_A_NDAPI ll::Expected<void> setExecutor(const ll::coro::Executor& executor);

        LOICOLLECTION_A_NDAPI ll::Expected<bool> acceptRequest(Player& player, const std::string& id);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> rejectRequest(Player& player, const std::string& id);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> cancelRequest(const std::string& id);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> hasRequest(const std::string& origin, const std::string& target);
        
        LOICOLLECTION_A_NDAPI ll::Expected<void> clearRequest(const std::string& id);
        LOICOLLECTION_A_NDAPI ll::Expected<void> sendRequest(Player& player, Player& target, const std::string& id, TpaType type);

        LOICOLLECTION_A_NDAPI ll::Expected<std::string> getBlacklist(Player& player, Player& target);

        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getBlacklist(Player& player);
        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getBlacklistFromTarget(const std::vector<std::string>& ids);

        LOICOLLECTION_A_NDAPI ll::Expected<std::unordered_map<std::string, std::string>> getBlacklistData(const std::string& id);

        LOICOLLECTION_A_NDAPI ll::Expected<bool> hasBlacklist(Player& player, const std::string& id);

        LOICOLLECTION_A_NDAPI ll::Expected<bool> forTpaContent(Player& player);
        
        LOICOLLECTION_A_NDAPI ll::Expected<bool> isInvite(Player& player);

        LOICOLLECTION_A_NDAPI ll::Expected<bool> requestInvite(Player& player, Player& target, TpaType type);

        LOICOLLECTION_A_NDAPI bool isValid();

    public:
        LOICOLLECTION_A_NDAPI int getBlacklistUpload();
        LOICOLLECTION_A_NDAPI int getRequestUpload();
        LOICOLLECTION_A_NDAPI int getRequestCount(Player& player);

    public:
        LOICOLLECTION_A_NDAPI std::string getName() override;

        LOICOLLECTION_A_NDAPI modules::ModulePriority getPriority() override;

        LOICOLLECTION_A_API   ll::Expected<bool> load() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unload() override;
        LOICOLLECTION_A_API   ll::Expected<bool> registry() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unregistry() override;

    private:
        TpaPlugin();

        ll::Expected<void> registeryUI();

        ll::Expected<std::vector<std::pair<std::string, std::string>>> getEligiblePlayers(Player& player);
        std::vector<std::pair<std::string, std::string>> getAddablePlayers(Player& player);

        void registeryCommand();
        void listenEvent();
        void unlistenEvent();
        
        struct RequestEntry;
        struct PlayerRequestSetEntry;

        struct operation;

        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
