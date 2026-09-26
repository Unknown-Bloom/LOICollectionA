#pragma once

#include <string>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/include/ModuleBase.h"
#include "LOICollectionA/include/ModManager.h"

#include "LOICollectionA/include/server/Plugins/form/ShopData.h"
#include "LOICollectionA/include/server/Plugins/types/shop/ShopCommonType.h"

class Player;

namespace ll::io {
    class Logger;
}

namespace LOICollection::server::Plugins {
    enum class ShopPluginErrorCode : int {
        Invalid = 1,
        NotFound = 2,
        UnknownType = 3
    };

    struct ShopPluginErrorCategory : std::error_category {
        [[nodiscard]] const char* name() const noexcept override {
            return "ShopPluginError";
        }

        [[nodiscard]] std::string message(int ev) const override {
            switch (static_cast<ShopPluginErrorCode>(ev)) {
                case ShopPluginErrorCode::Invalid: return "Plugin is invalid";
                case ShopPluginErrorCode::NotFound: return "Shop data not found";
                case ShopPluginErrorCode::UnknownType: return "Unknown shop type";
                default:
                    return "Unknown";
            }
        }
    };

    class ShopPlugin : public std::enable_shared_from_this<ShopPlugin>,
                       public modules::ModuleBase,
                       public modules::AutoRegister<ShopPlugin> {
    public:
        ~ShopPlugin();

        ShopPlugin(ShopPlugin const&) = delete;
        ShopPlugin(ShopPlugin&&) = delete;
        ShopPlugin& operator=(ShopPlugin const&) = delete;
        ShopPlugin& operator=(ShopPlugin&&) = delete;

    public:
        LOICOLLECTION_A_NDAPI static std::shared_ptr<ShopPlugin> getShared();
        LOICOLLECTION_A_NDAPI static std::error_code makeErrorCode(ShopPluginErrorCode e);

        LOICOLLECTION_A_NDAPI std::shared_ptr<ll::io::Logger> getLogger();

        LOICOLLECTION_A_NDAPI ll::Expected<ShopActionResult> commodity(Player& player, int number, const ShopItemData& data, ShopType type);
        LOICOLLECTION_A_NDAPI ll::Expected<ShopActionResult> title(Player& player, const ShopItemData& data, ShopType type);

        LOICOLLECTION_A_NDAPI ll::Expected<bool> checkModifiedData(Player& player, const ShopItemData& data, int number);

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
        ShopPlugin();

        ll::Expected<void> registeryUI();

        void registeryCommand();

        struct operation;

        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
