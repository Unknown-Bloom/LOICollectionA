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
    enum class MutePluginErrorCode : int {
        Invalid = 1,
        NotFound = 2,
        PermissionDenied = 3
    };

    struct MutePluginErrorCategory : std::error_category {
        [[nodiscard]] const char* name() const noexcept override {
            return "MutePluginError";
        }

        [[nodiscard]] std::string message(int ev) const override {
            switch (static_cast<MutePluginErrorCode>(ev)) {
                case MutePluginErrorCode::Invalid: return "Plugin is invalid";
                case MutePluginErrorCode::NotFound: return "Mute data not found";
                case MutePluginErrorCode::PermissionDenied: return "Cannot add a players with excessive permissions to the Mute";
                default:
                    return "Unknown";
            }
        }
    };

    class MutePlugin : public std::enable_shared_from_this<MutePlugin>, 
                       public modules::ModuleBase,
                       public modules::AutoRegister<MutePlugin> {
    public:
        ~MutePlugin();

        MutePlugin(MutePlugin const&) = delete;
        MutePlugin(MutePlugin&&) = delete;
        MutePlugin& operator=(MutePlugin const&) = delete;
        MutePlugin& operator=(MutePlugin&&) = delete;    

    public:
        LOICOLLECTION_A_NDAPI static std::shared_ptr<MutePlugin> getShared();
        LOICOLLECTION_A_NDAPI static std::error_code makeErrorCode(MutePluginErrorCode e);

        LOICOLLECTION_A_NDAPI std::shared_ptr<BlockRepository> getDatabase();
        LOICOLLECTION_A_NDAPI std::shared_ptr<ll::io::Logger> getLogger();

        LOICOLLECTION_A_NDAPI ll::Expected<void> addMute(Player& player, const std::string& cause, int time);
        LOICOLLECTION_A_NDAPI ll::Expected<void> delMute(Player& player);
        LOICOLLECTION_A_NDAPI ll::Expected<void> delMute(const std::string& id);

        LOICOLLECTION_A_NDAPI ll::Expected<std::string> getMute(Player& player);
        
        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getMutes(int limit = -1);

        LOICOLLECTION_A_NDAPI ll::Expected<std::unordered_map<std::string, std::string>> getMuteData(const std::string& id);
        
        LOICOLLECTION_A_NDAPI ll::Expected<bool> hasMute(const std::string& id);
        LOICOLLECTION_A_NDAPI ll::Expected<bool> isMute(Player& player);

        LOICOLLECTION_A_NDAPI bool isValid();

    public:
        LOICOLLECTION_A_NDAPI std::string getName() override;

        LOICOLLECTION_A_NDAPI modules::ModulePriority getPriority() override;

        LOICOLLECTION_A_API   ll::Expected<bool> load() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unload() override;
        LOICOLLECTION_A_API   ll::Expected<bool> registry() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unregistry() override;

    private:
        MutePlugin();

        ll::Expected<void> registeryUI();

        void registeryCommand();
        void listenEvent();
        void unlistenEvent();

        struct operation;

        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
