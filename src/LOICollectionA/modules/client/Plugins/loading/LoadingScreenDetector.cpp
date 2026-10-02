#include <array>
#include <mutex>
#include <atomic>
#include <cctype>
#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <optional>
#include <string_view>
#include <unordered_set>

#include <ll/api/io/Logger.h>
#include <ll/api/memory/Hook.h>
#include <ll/api/memory/Memory.h>

#include <ll/api/event/EventBus.h>
#include <ll/api/event/client/ClientCancelJoinLevelEvent.h>
#include <ll/api/event/client/ClientExitLevelEvent.h>
#include <ll/api/event/client/ClientJoinLevelEvent.h>
#include <ll/api/event/client/ClientStartJoinLevelEvent.h>

#include <mc/client/gui/SceneType.h>
#include <mc/client/gui/oreui/Scene.h>
#include <mc/client/gui/ProgressHandler.h>
#include <mc/client/gui/screens/ScreenContext.h>
#include <mc/client/gui/screens/controllers/ProgressScreenController.h>
#include <mc/client/gui/screens/models/MinecraftScreenModel.h>

#include <mc/deps/json/Value.h>

#include "LOICollectionA/include/client/Plugins/loading/LoadingScreenDetector.h"

namespace LOICollection::client::Plugins::loading {
    namespace {
        inline constexpr std::int64_t SceneActiveStaleMs = 150;
        inline constexpr std::int64_t RouteProbeIntervalMs = 200;
        inline constexpr std::int64_t JoinReleaseGraceMs = 600;
        inline constexpr std::int64_t ExitReleaseGraceMs = 900;

        LL_TYPE_INSTANCE_HOOK(
            LoadingSceneRenderHook,
            ll::memory::HookPriority::Normal,
            ::OreUI::Scene,
            &::OreUI::Scene::$render,
            void,
            ::ScreenContext&           screenContext,
            ::FrameRenderObject const& frameRenderObject
        ) {
            origin(screenContext, frameRenderObject);

            LoadingScreenDetector::getInstance().onSceneRendered(*this);
        }

        LL_TYPE_INSTANCE_HOOK(
            LoadingProgressTickHook,
            ll::memory::HookPriority::Normal,
            ::ProgressScreenController,
            &::ProgressScreenController::$tick,
            ::ui::DirtyFlag
        ) {
            ::ui::DirtyFlag result = origin();

            LoadingScreenDetector::getInstance().onProgressScreenTicked(*this);

            return result;
        }

        LL_TYPE_INSTANCE_HOOK(
            LoadingProgressVarsHook,
            ll::memory::HookPriority::Normal,
            ::ProgressScreenController,
            &::ProgressScreenController::$addStaticScreenVars,
            void,
            ::Json::Value& globalVars
        ) {
            origin(globalVars);

            LoadingScreenDetector::getInstance().onProgressScreenVars(*this, globalVars);
        }

        float normalizeProgress(float value) {
            if (!(value >= 0.0f))
                return -1.0f;

            if (value > 1.0001f)
                value /= 100.0f;

            return value <= 1.0001f ? (value > 1.0f ? 1.0f : value) : -1.0f;
        }

        struct ProgressSample {
            float Model;
            float Handler;
            float Accumulated;
            int   Handlers;
        };

        ProgressSample sampleProgressScreen(::ProgressScreenController& controller) noexcept {
            ProgressSample sample { -1.0f, -1.0f, -1.0f, 0 };

            sample.Accumulated = controller.mAccumulatedProgressPercentageForHandlers;
            sample.Handlers    = controller.mTotalNumberOfProgressHandlers;

            ::MinecraftScreenModel* model   = controller.mMinecraftScreenModel.get();
            ::ProgressHandler*      handler = controller.mProgressHandler.get();

            if (model != nullptr)
                sample.Model = model->getLoadingProgress();

            if (model != nullptr && handler != nullptr)
                sample.Handler = handler->getLoadingProgress(*model);

            return sample;
        }

        bool isBlockingModal(::ProgressScreenController& controller) {
            if (controller.mCurrentlyShowAddonWarning || controller.mDisconnectScreenDisplayed)
                return true;

            if (controller.mResourcePackPacketReceived && !controller.mDownloadAlreadyConfirmedByUser)
                return !controller.mRequiredPackList.get().empty() || !controller.mOptionalPackList.get().empty();

            return false;
        }

        std::int64_t nowMs() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count();
        }

        std::string toLower(std::string value) {
            for (auto& c : value)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            return value;
        }

        std::string pickMessageFromVars(::Json::Value const& vars) {
            if (!vars.isObject())
                return {};

            std::string best;
            int bestScore = 0;

            for (auto const& key : vars.getMemberNames()) {
                std::string lower = toLower(key);

                int score = lower.find("tip") != std::string::npos ? 3
                          : lower.find("message") != std::string::npos ? 2 : 0;

                if (score == 0 || score <= bestScore)
                    continue;

                ::Json::Value const& value = vars[key];

                std::string text;

                if (value.isString()) {
                    text = value.asString(std::string{});
                } else if (value.isArray() && !value.empty() && value[0].isString()) {
                    text = value[0].asString(std::string{});
                }

                if (text.empty())
                    continue;

                bestScore = score;
                best = std::move(text);
            }

            return best;
        }

        bool containsKeyword(const std::string& haystack, const std::vector<std::string>& keywords) {
            if (haystack.empty())
                return false;

            return std::ranges::any_of(keywords, [&haystack](const std::string& keyword) {
                return !keyword.empty() && haystack.contains(keyword);
            });
        }

        template <class TRegistrar>
        bool emplaceVerifiedHook(
            std::optional<TRegistrar>&             slot,
            ll::memory::FuncPtr                    target,
            std::shared_ptr<ll::io::Logger> const& logger,
            std::string_view                       name
        ) {
            constexpr size_t ProbeSize = 8;

            std::array<unsigned char, ProbeSize> before {};
            std::memcpy(before.data(), target, ProbeSize);

            slot.emplace();

            std::array<unsigned char, ProbeSize> after {};
            std::memcpy(after.data(), target, ProbeSize);

            bool patched = before != after;

            if (logger) {
                logger->debug(
                    "LoadingScreenDetector - hook ({}) target=0x{:X} patched={}",
                    name,
                    reinterpret_cast<std::uintptr_t>(target),
                    patched
                );
            }

            if (!patched)
                slot.reset();

            return patched;
        }
    }

    struct LoadingScreenDetector::Impl {
        std::shared_ptr<ll::io::Logger> Logger;

        std::mutex               KeywordsMutex;
        std::vector<std::string> Keywords;

        std::optional<ll::memory::HookRegistrar<LoadingSceneRenderHook>>  SceneHook;
        std::optional<ll::memory::HookRegistrar<LoadingProgressTickHook>> ProgressHook;
        std::optional<ll::memory::HookRegistrar<LoadingProgressVarsHook>> VarsHook;

        std::atomic<std::int64_t> SceneActiveTick { 0 };

        std::atomic<std::int64_t> ProgressActiveTick { 0 };

        std::atomic<float>        ProgressValue { -1.0f };
        std::atomic<std::int64_t> ProgressSampleTick { 0 };
        std::atomic<std::int64_t> LastProgressLogTick { 0 };

        std::atomic_bool ModalActive { false };

        mutable std::mutex MessageMutex;
        std::string        ProgressMessage;

        std::atomic_bool          ExternalActive { false };
        std::atomic<std::int64_t> ExternalReleaseTick { 0 };

        std::shared_ptr<ll::event::Listener<ll::event::client::ClientStartJoinLevelEvent>> StartJoinListener;
        std::shared_ptr<ll::event::Listener<ll::event::client::ClientJoinLevelEvent>>      JoinListener;
        std::shared_ptr<ll::event::Listener<ll::event::client::ClientCancelJoinLevelEvent>> CancelJoinListener;
        std::shared_ptr<ll::event::Listener<ll::event::client::ClientExitLevelEvent>>      ExitListener;

        std::atomic<std::int64_t> LastProbeTick { 0 };

        std::mutex  ProbeMutex;
        const void* ProbeScene { nullptr };
        bool        ProbeSceneMatch { false };

        mutable std::mutex RouteMutex;
        std::string        CurrentRoute;

        std::mutex                      SeenMutex;
        std::unordered_set<std::string> SeenRoutes;

        void markExternal(std::int64_t releaseDelayMs) {
            this->ExternalActive.store(true, std::memory_order_release);
            this->ExternalReleaseTick.store(
                releaseDelayMs > 0 ? nowMs() + releaseDelayMs : 0,
                std::memory_order_release
            );
        }

        bool isExternalActive() {
            if (!this->ExternalActive.load(std::memory_order_acquire))
                return false;

            std::int64_t release = this->ExternalReleaseTick.load(std::memory_order_acquire);

            if (release != 0 && nowMs() >= release) {
                this->ExternalActive.store(false, std::memory_order_release);
                this->ExternalReleaseTick.store(0, std::memory_order_release);

                return false;
            }

            return true;
        }

        bool isLoadingScene(::OreUI::Scene& scene) {
            auto sceneType = static_cast<unsigned int>(scene.getSceneType());

            bool bitMatch = (sceneType & static_cast<unsigned int>(ui::SceneType::ProgressScene)) != 0u;

            std::int64_t now = nowMs();

            if (now - this->LastProbeTick.load(std::memory_order_acquire) >= RouteProbeIntervalMs) {
                this->LastProbeTick.store(now, std::memory_order_release);

                std::string route = toLower(scene.getRoute());
                std::string name  = scene.getScreenName();

                {
                    std::scoped_lock lock(this->RouteMutex);
                    this->CurrentRoute = route;
                }

                this->dumpRouteOnce(route, name, sceneType);

                std::vector<std::string> keywords;

                {
                    std::scoped_lock lock(this->KeywordsMutex);
                    keywords = this->Keywords;
                }

                bool match = containsKeyword(route, keywords) || containsKeyword(toLower(name), keywords);

                {
                    std::scoped_lock lock(this->ProbeMutex);
                    this->ProbeScene      = std::addressof(scene);
                    this->ProbeSceneMatch = match;
                }
            }

            if (bitMatch)
                return true;

            std::scoped_lock lock(this->ProbeMutex);

            return this->ProbeScene == std::addressof(scene) && this->ProbeSceneMatch;
        }

        void dumpRouteOnce(const std::string& route, const std::string& name, unsigned int sceneType) {
            if (!this->Logger)
                return;

            std::string key = route + "|" + name;

            {
                std::scoped_lock lock(this->SeenMutex);

                if (!this->SeenRoutes.insert(key).second)
                    return;
            }

            this->Logger->debug(
                "LoadingScreenDetector - scene route='{}' name='{}' type=0x{:X}",
                route,
                name,
                sceneType
            );
        }

        bool installSceneHook() {
            return emplaceVerifiedHook(
                this->SceneHook,
                ll::memory::toFuncPtr(&::OreUI::Scene::$render),
                this->Logger,
                "OreUI::Scene::$render"
            );
        }

        bool installProgressHook() {
            return emplaceVerifiedHook(
                this->ProgressHook,
                ll::memory::toFuncPtr(&::ProgressScreenController::$tick),
                this->Logger,
                "ProgressScreenController::$tick"
            );
        }

        bool installVarsHook() {
            return emplaceVerifiedHook(
                this->VarsHook,
                ll::memory::toFuncPtr(&::ProgressScreenController::$addStaticScreenVars),
                this->Logger,
                "ProgressScreenController::$addStaticScreenVars"
            );
        }

        bool installEventListeners() {
            auto& bus = ll::event::EventBus::getInstance();

            this->StartJoinListener = bus.emplaceListener<ll::event::client::ClientStartJoinLevelEvent>(
                [this](ll::event::client::ClientStartJoinLevelEvent&) -> void {
                    this->markExternal(0);
                }
            );

            this->JoinListener = bus.emplaceListener<ll::event::client::ClientJoinLevelEvent>(
                [this](ll::event::client::ClientJoinLevelEvent&) -> void {
                    this->markExternal(JoinReleaseGraceMs);
                }
            );

            this->CancelJoinListener = bus.emplaceListener<ll::event::client::ClientCancelJoinLevelEvent>(
                [this](ll::event::client::ClientCancelJoinLevelEvent&) -> void {
                    this->ExternalActive.store(false, std::memory_order_release);
                    this->ExternalReleaseTick.store(0, std::memory_order_release);
                }
            );

            this->ExitListener = bus.emplaceListener<ll::event::client::ClientExitLevelEvent>(
                [this](ll::event::client::ClientExitLevelEvent&) -> void {
                    this->markExternal(ExitReleaseGraceMs);
                }
            );

            return this->StartJoinListener != nullptr || this->JoinListener != nullptr
                || this->CancelJoinListener != nullptr || this->ExitListener != nullptr;
        }

        void removeEventListeners() {
            this->StartJoinListener.reset();
            this->JoinListener.reset();
            this->CancelJoinListener.reset();
            this->ExitListener.reset();

            this->ExternalActive.store(false, std::memory_order_release);
            this->ExternalReleaseTick.store(0, std::memory_order_release);
        }

        void onProgressScreenTicked(::ProgressScreenController& controller) {
            std::int64_t now = nowMs();

            this->ProgressActiveTick.store(now, std::memory_order_release);

            ProgressSample sample = sampleProgressScreen(controller);

            float progress = normalizeProgress(sample.Model);

            if (progress < 0.0f && sample.Accumulated >= 0.0f && sample.Handlers > 0 && sample.Handler >= 0.0f)
                progress = normalizeProgress(sample.Accumulated + sample.Handler / static_cast<float>(sample.Handlers));

            if (progress < 0.0f)
                progress = normalizeProgress(sample.Handler);

            this->ProgressValue.store(progress, std::memory_order_release);
            this->ProgressSampleTick.store(now, std::memory_order_release);

            this->ModalActive.store(isBlockingModal(controller), std::memory_order_release);

            std::string message = controller.mCurrentProgressMessage;

            if (!message.empty()) {
                std::scoped_lock lock(this->MessageMutex);
                this->ProgressMessage = std::move(message);
            }

            if (this->Logger && now - this->LastProgressLogTick.load(std::memory_order_acquire) >= 500) {
                this->LastProgressLogTick.store(now, std::memory_order_release);

                this->Logger->debug(
                    "LoadingScreenDetector - progress sample model={} handler={} accumulated={} handlers={} -> {}",
                    sample.Model,
                    sample.Handler,
                    sample.Accumulated,
                    sample.Handlers,
                    progress
                );
            }
        }

        void onProgressScreenVars(::Json::Value& globalVars) {
            std::string message = pickMessageFromVars(globalVars);

            if (message.empty())
                return;

            std::scoped_lock lock(this->MessageMutex);
            this->ProgressMessage = std::move(message);
        }
    };

    LoadingScreenDetector::LoadingScreenDetector() : mImpl(std::make_unique<Impl>()) {}
    LoadingScreenDetector::~LoadingScreenDetector() = default;

    LoadingScreenDetector& LoadingScreenDetector::getInstance() {
        static LoadingScreenDetector instance;
        return instance;
    }

    void LoadingScreenDetector::setLogger(std::shared_ptr<ll::io::Logger> logger) {
        this->mImpl->Logger = std::move(logger);
    }

    void LoadingScreenDetector::setRouteKeywords(std::vector<std::string> keywords) {
        for (auto& keyword : keywords)
            keyword = toLower(keyword);

        std::scoped_lock lock(this->mImpl->KeywordsMutex);
        this->mImpl->Keywords = std::move(keywords);
    }

    bool LoadingScreenDetector::install() {
        bool sceneHookInstalled = this->mImpl->SceneHook.has_value();

        if (!sceneHookInstalled) {
            sceneHookInstalled = this->mImpl->installSceneHook();

            if (!sceneHookInstalled && this->mImpl->Logger)
                this->mImpl->Logger->warn(
                    "LoadingScreenDetector - OreUI scene hook unavailable, "
                    "falling back to level transition events"
                );
        }

        bool progressHookInstalled = this->mImpl->ProgressHook.has_value();

        if (!progressHookInstalled) {
            progressHookInstalled = this->mImpl->installProgressHook();

            if (!progressHookInstalled && this->mImpl->Logger)
                this->mImpl->Logger->warn(
                    "LoadingScreenDetector - progress screen hook unavailable, "
                    "progress bar will use indeterminate animation"
                );
        }

        bool varsHookInstalled = this->mImpl->VarsHook.has_value();

        if (!varsHookInstalled)
            varsHookInstalled = this->mImpl->installVarsHook();

        bool listenersInstalled = this->mImpl->installEventListeners();

        if (!listenersInstalled && this->mImpl->Logger)
            this->mImpl->Logger->warn("LoadingScreenDetector - level transition listeners unavailable");

        return sceneHookInstalled || progressHookInstalled || varsHookInstalled || listenersInstalled;
    }

    void LoadingScreenDetector::uninstall() {
        this->mImpl->removeEventListeners();

        if (this->mImpl->VarsHook.has_value())
            this->mImpl->VarsHook.reset();

        if (this->mImpl->ProgressHook.has_value()) {
            this->mImpl->ProgressHook.reset();

            this->mImpl->ProgressActiveTick.store(0, std::memory_order_release);
            this->mImpl->ProgressSampleTick.store(0, std::memory_order_release);
            this->mImpl->ProgressValue.store(-1.0f, std::memory_order_release);
        }

        if (this->mImpl->SceneHook.has_value()) {
            this->mImpl->SceneHook.reset();

            this->mImpl->SceneActiveTick.store(0, std::memory_order_release);
        }

        this->mImpl->ModalActive.store(false, std::memory_order_release);

        {
            std::scoped_lock lock(this->mImpl->MessageMutex);
            this->mImpl->ProgressMessage.clear();
        }
    }

    bool LoadingScreenDetector::isLoadingScreenActive() const {
        if (this->mImpl->isExternalActive())
            return true;

        std::int64_t now = nowMs();

        std::int64_t sceneTick = this->mImpl->SceneActiveTick.load(std::memory_order_acquire);

        if (sceneTick != 0 && now - sceneTick < SceneActiveStaleMs)
            return true;

        std::int64_t progressTick = this->mImpl->ProgressActiveTick.load(std::memory_order_acquire);

        if (progressTick != 0 && now - progressTick < SceneActiveStaleMs)
            return true;

        return false;
    }

    bool LoadingScreenDetector::isSceneHookActive() const {
        return this->mImpl->SceneHook.has_value();
    }

    bool LoadingScreenDetector::isProgressHookActive() const {
        return this->mImpl->ProgressHook.has_value();
    }

    float LoadingScreenDetector::loadingProgress() const {
        std::int64_t sampleTick = this->mImpl->ProgressSampleTick.load(std::memory_order_acquire);

        if (sampleTick == 0 || nowMs() - sampleTick >= 800)
            return -1.0f;

        return this->mImpl->ProgressValue.load(std::memory_order_acquire);
    }

    std::string LoadingScreenDetector::progressMessage() const {
        std::scoped_lock lock(this->mImpl->MessageMutex);
        return this->mImpl->ProgressMessage;
    }

    bool LoadingScreenDetector::isBlockingModalActive() const {
        return this->mImpl->ModalActive.load(std::memory_order_acquire);
    }

    std::string LoadingScreenDetector::currentRoute() const {
        std::scoped_lock lock(this->mImpl->RouteMutex);
        return this->mImpl->CurrentRoute;
    }

    void LoadingScreenDetector::onSceneRendered(::OreUI::Scene& scene) {
        if (!this->mImpl->isLoadingScene(scene))
            return;

        this->mImpl->SceneActiveTick.store(nowMs(), std::memory_order_release);
    }

    void LoadingScreenDetector::onProgressScreenTicked(::ProgressScreenController& controller) {
        this->mImpl->onProgressScreenTicked(controller);
    }

    void LoadingScreenDetector::onProgressScreenVars(::ProgressScreenController& controller, ::Json::Value& globalVars) {
        (void)controller;
        this->mImpl->onProgressScreenVars(globalVars);
    }
}
