#pragma once

#include <memory>
#include <string>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/include/ModuleBase.h"
#include "LOICollectionA/include/ModManager.h"

namespace ll::io {
    class Logger;
}

namespace LOICollection::client::Plugins::loading {
    enum class LoadingScreenPluginErrorCode : int {
        Invalid = 1
    };

    struct LoadingScreenPluginErrorCategory : std::error_category {
        [[nodiscard]] const char* name() const noexcept override {
            return "LoadingScreenPluginError";
        }

        [[nodiscard]] std::string message(int ev) const override {
            switch (static_cast<LoadingScreenPluginErrorCode>(ev)) {
                case LoadingScreenPluginErrorCode::Invalid: return "Plugin is invalid";
                default:                                    return "Unknown";
            }
        }
    };

    class LoadingScreenPlugin : public std::enable_shared_from_this<LoadingScreenPlugin>,
                                public modules::ModuleBase,
                                public modules::AutoRegister<LoadingScreenPlugin> {
    public:
        ~LoadingScreenPlugin();

        LoadingScreenPlugin(LoadingScreenPlugin const&)            = delete;
        LoadingScreenPlugin(LoadingScreenPlugin&&)                 = delete;
        LoadingScreenPlugin& operator=(LoadingScreenPlugin const&) = delete;
        LoadingScreenPlugin& operator=(LoadingScreenPlugin&&)      = delete;

    public:
        LOICOLLECTION_A_NDAPI static std::shared_ptr<LoadingScreenPlugin> getShared();
        LOICOLLECTION_A_NDAPI static std::error_code makeErrorCode(LoadingScreenPluginErrorCode e);

        LOICOLLECTION_A_NDAPI std::shared_ptr<ll::io::Logger> getLogger();

        LOICOLLECTION_A_NDAPI bool isValid();

    public:
        LOICOLLECTION_A_NDAPI std::string getName() override;

        LOICOLLECTION_A_NDAPI modules::ModulePriority getPriority() override;

        LOICOLLECTION_A_API   ll::Expected<bool> load() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unload() override;
        LOICOLLECTION_A_API   ll::Expected<bool> registry() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unregistry() override;

    private:
        LoadingScreenPlugin();

        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
