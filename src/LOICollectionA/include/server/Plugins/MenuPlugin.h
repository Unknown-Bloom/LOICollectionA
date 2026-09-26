#pragma once

#include <memory>
#include <string>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/include/ModuleBase.h"
#include "LOICollectionA/include/ModManager.h"

#include "LOICollectionA/include/server/Plugins/form/MenuData.h"
#include "LOICollectionA/include/server/Plugins/types/menu/MenuCommonType.h"

class Player;

namespace ll::io {
    class Logger;
}

namespace LOICollection::server::Plugins {
    enum class MenuPluginErrorCode : int {
        Invalid = 1,
        PermissionDenied = 2,
        InsufficientScore = 3,
        NotFound = 4,
        UnknownType = 5
    };

    struct MenuPluginErrorCategory : std::error_category {
        [[nodiscard]] const char* name() const noexcept override {
            return "MenuPluginError";
        }

        [[nodiscard]] std::string message(int ev) const override {
            switch (static_cast<MenuPluginErrorCode>(ev)) {
                case MenuPluginErrorCode::Invalid: return "Plugin is invalid";
                case MenuPluginErrorCode::PermissionDenied: return "Permission denied";
                case MenuPluginErrorCode::InsufficientScore: return "Insufficient score";
                case MenuPluginErrorCode::NotFound: return "Menu data not found";
                case MenuPluginErrorCode::UnknownType: return "Unknown menu type";
                default:
                    return "Unknown";
            }
        }
    };

    class MenuPlugin : public std::enable_shared_from_this<MenuPlugin>,
                       public modules::ModuleBase,
                       public modules::AutoRegister<MenuPlugin> {
    public:
        ~MenuPlugin();

        MenuPlugin(MenuPlugin const&) = delete;
        MenuPlugin(MenuPlugin&&) = delete;
        MenuPlugin& operator=(MenuPlugin const&) = delete;
        MenuPlugin& operator=(MenuPlugin&&) = delete;

    public:
        LOICOLLECTION_A_NDAPI static std::shared_ptr<MenuPlugin> getShared();
        LOICOLLECTION_A_NDAPI static std::error_code makeErrorCode(MenuPluginErrorCode e);

        LOICOLLECTION_A_NDAPI std::shared_ptr<ll::io::Logger> getLogger();

        LOICOLLECTION_A_NDAPI ll::Expected<MenuActionResult> handleAction(Player& player, const MenuItemData& action, const MenuData& original);

        LOICOLLECTION_A_NDAPI ll::Expected<void> open(Player& player, const std::string& id);

        LOICOLLECTION_A_NDAPI bool isValid();

    public:
        LOICOLLECTION_A_NDAPI std::string getName() override;

        LOICOLLECTION_A_NDAPI modules::ModulePriority getPriority() override;

        LOICOLLECTION_A_API   ll::Expected<bool> load() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unload() override;
        LOICOLLECTION_A_API   ll::Expected<bool> registry() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unregistry() override;

    private:
        MenuPlugin();

        ll::Expected<void> registeryUI();

        void registeryCommand();
        void listenEvent();
        void unlistenEvent();

        struct operation;
        
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
