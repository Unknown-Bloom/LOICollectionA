#include <atomic>
#include <memory>
#include <string>

#include <ll/api/io/Logger.h>
#include <ll/api/io/LoggerRegistry.h>

#include "LOICollectionA/base/Wrapper.h"
#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/client/display/overlay/Overlay.h"

#include "LOICollectionA/include/client/Plugins/loading/LoadingScreenPlugin.h"
#include "LOICollectionA/include/client/Plugins/loading/LoadingScreenDetector.h"
#include "LOICollectionA/include/client/Plugins/loading/LoadingAnimationRenderer.h"

namespace LOICollection::client::Plugins::loading {
    struct LoadingScreenPlugin::Impl {
        std::atomic_bool ModuleEnabled { false };
        std::atomic_bool Registered { false };

        display::overlay::RenderCallbackHandle RenderHandle { 0 };

        std::shared_ptr<ll::io::Logger> logger;
    };

    LoadingScreenPlugin::LoadingScreenPlugin() : mImpl(std::make_unique<Impl>()) {};
    LoadingScreenPlugin::~LoadingScreenPlugin() = default;

    std::shared_ptr<LoadingScreenPlugin> LoadingScreenPlugin::getShared() {
        static auto instance = std::shared_ptr<LoadingScreenPlugin>(new LoadingScreenPlugin());
        return instance;
    }

    std::error_code LoadingScreenPlugin::makeErrorCode(LoadingScreenPluginErrorCode e) {
        static LoadingScreenPluginErrorCategory cat;
        return std::error_code{ static_cast<int>(e), cat };
    }

    std::shared_ptr<ll::io::Logger> LoadingScreenPlugin::getLogger() {
        return this->mImpl->logger;
    }

    bool LoadingScreenPlugin::isValid() {
        return this->mImpl->ModuleEnabled.load(std::memory_order_acquire) && this->mImpl->logger != nullptr;
    }

    std::string LoadingScreenPlugin::getName() {
        return "LoadingScreenPlugin";
    }

    modules::ModulePriority LoadingScreenPlugin::getPriority() {
        return modules::ModulePriority::Normal;
    }

    ll::Expected<bool> LoadingScreenPlugin::load() {
        const Config::C_Config& config = ServiceProvider::getInstance()
            .getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get();

        if (!config.ClientConfig.LoadingScreen.ModuleEnabled)
            return false;

        this->mImpl->logger = ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA");
        this->mImpl->ModuleEnabled.store(true, std::memory_order_release);

        return true;
    }

    ll::Expected<bool> LoadingScreenPlugin::unload() {
        if (!this->isValid())
            return false;

        this->mImpl->logger.reset();
        this->mImpl->ModuleEnabled.store(false, std::memory_order_release);

        return true;
    }

    ll::Expected<bool> LoadingScreenPlugin::registry() {
        if (!this->isValid())
            return false;

        const Config::C_LoadingScreen& loading = ServiceProvider::getInstance()
            .getService<ReadOnlyWrapper<Config::C_Config>>("Config")
            ->get()
            .ClientConfig.LoadingScreen;

        LoadingScreenDetector::getInstance().setLogger(this->mImpl->logger);
        LoadingScreenDetector::getInstance().setRouteKeywords(loading.RouteKeywords);

        LoadingAnimationStyle style;
        style.CubeSize = loading.CubeSize;
        style.BarWidth = loading.BarWidth;
        style.Title    = loading.Title;
        style.Tips     = loading.Tips;

        LoadingAnimationRenderer::getInstance().setLogger(this->mImpl->logger);
        LoadingAnimationRenderer::getInstance().setStyle(style);

        if (!LoadingScreenDetector::getInstance().install()) {
            this->getLogger()->error("LoadingScreenPlugin - no usable loading screen signal source");

            return ll::makeErrorCodeError(makeErrorCode(LoadingScreenPluginErrorCode::Invalid));
        }

        if (!LoadingScreenDetector::getInstance().isSceneHookActive())
            this->getLogger()->warn(
                "LoadingScreenPlugin - running in degraded mode: only level transition events will trigger "
                "the loading animation"
            );

        if (!display::overlay::applyHooks()) {
            LoadingScreenDetector::getInstance().uninstall();

            return ll::makeErrorCodeError(makeErrorCode(LoadingScreenPluginErrorCode::Invalid));
        }

        this->mImpl->RenderHandle = display::overlay::addRenderCallback(
            [](float deltaTime, float screenWidth, float screenHeight) -> void {
                LoadingAnimationRenderer::getInstance().render(deltaTime, screenWidth, screenHeight);
            }
        );

        display::overlay::setInputBlocker([]() -> bool {
            return LoadingScreenDetector::getInstance().isLoadingScreenActive();
        });

        this->mImpl->Registered.store(true, std::memory_order_release);

        this->getLogger()->info("LoadingScreenPlugin - OreUI loading screens will be covered by custom animation");

        return true;
    }

    ll::Expected<bool> LoadingScreenPlugin::unregistry() {
        if (!this->mImpl->Registered.exchange(false, std::memory_order_acq_rel))
            return false;

        display::overlay::setInputBlocker(nullptr);

        display::overlay::removeRenderCallback(this->mImpl->RenderHandle);
        this->mImpl->RenderHandle = 0;

        LoadingAnimationRenderer::getInstance().reset();

        display::overlay::removeHooks();

        LoadingScreenDetector::getInstance().uninstall();

        return true;
    }
}
