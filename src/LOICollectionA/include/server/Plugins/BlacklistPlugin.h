#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/include/ModuleBase.h"
#include "LOICollectionA/include/ModManager.h"

class Player;
class BlockRepository;

namespace ll::io {
    class Logger;
}

namespace LOICollection::server::Plugins {
    enum class BlacklistPluginErrorCode : int {
        Invalid = 1,
        NotFound = 2,
        PermissionDenied = 3
    };

    struct BlacklistPluginErrorCategory : std::error_category {
        [[nodiscard]] const char* name() const noexcept override {
            return "BlacklistPluginError";
        }

        [[nodiscard]] std::string message(int ev) const override {
            switch (static_cast<BlacklistPluginErrorCode>(ev)) {
                case BlacklistPluginErrorCode::Invalid: return "Plugin is invalid";
                case BlacklistPluginErrorCode::NotFound: return "Blacklist data not found";
                case BlacklistPluginErrorCode::PermissionDenied: return "Cannot add a players with excessive permissions to the blacklist";
                default:
                    return "Unknown";
            }
        }
    };

    class BlacklistPlugin : public std::enable_shared_from_this<BlacklistPlugin>, 
                            public modules::ModuleBase,
                            public modules::AutoRegister<BlacklistPlugin> {
    public:
        ~BlacklistPlugin();

        BlacklistPlugin(BlacklistPlugin const&) = delete;
        BlacklistPlugin(BlacklistPlugin&&) = delete;
        BlacklistPlugin& operator=(BlacklistPlugin const&) = delete;
        BlacklistPlugin& operator=(BlacklistPlugin&&) = delete;

    public:
        LOICOLLECTION_A_NDAPI static std::shared_ptr<BlacklistPlugin> getShared();
        LOICOLLECTION_A_NDAPI static std::error_code makeErrorCode(BlacklistPluginErrorCode e);

        LOICOLLECTION_A_NDAPI std::shared_ptr<BlockRepository> getDatabase();
        LOICOLLECTION_A_NDAPI std::shared_ptr<ll::io::Logger> getLogger();

        LOICOLLECTION_A_NDAPI ll::Expected<void> addBlacklist(Player& player, const std::string& cause, int time);
        LOICOLLECTION_A_NDAPI ll::Expected<void> delBlacklist(const std::string& id);

        LOICOLLECTION_A_NDAPI ll::Expected<std::string> getBlacklist(Player& player);

        LOICOLLECTION_A_NDAPI ll::Expected<std::unordered_map<std::string, std::string>> getBlacklistData(const std::string& id);

        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getBlacklists(int limit = -1);
        
        LOICOLLECTION_A_NDAPI ll::Expected<bool> hasBlacklist(const std::string& id);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> isBlacklist(Player& player);

        LOICOLLECTION_A_NDAPI bool isValid();
    
    public:
        LOICOLLECTION_A_NDAPI std::string getName() override;

        LOICOLLECTION_A_NDAPI modules::ModulePriority getPriority() override;

        LOICOLLECTION_A_API   ll::Expected<bool> load() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unload() override;
        LOICOLLECTION_A_API   ll::Expected<bool> registry() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unregistry() override;
        
    private:
        BlacklistPlugin();

        ll::Expected<void> registeryUI();

        void registeryCommand();
        void listenEvent();
        void unlistenEvent();

        struct operation;

        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
