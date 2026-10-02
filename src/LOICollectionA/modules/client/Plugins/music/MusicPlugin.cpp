#include <mutex>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <exception>
#include <filesystem>

#include <ll/api/io/Logger.h>
#include <ll/api/io/LoggerRegistry.h>

#include "LOICollectionA/base/Wrapper.h"
#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/client/Plugins/music/MediaSessionSource.h"
#include "LOICollectionA/include/client/Plugins/music/NowPlayingToast.h"
#include "LOICollectionA/include/client/Plugins/music/NowPlayingRenderer.h"
#include "LOICollectionA/include/client/Plugins/music/audio/LoopbackCapture.h"

#include "LOICollectionA/include/client/display/overlay/Overlay.h"

#include "LOICollectionA/include/client/Plugins/music/MusicPlugin.h"

namespace LOICollection::client::Plugins::music {
    struct MusicPlugin::Impl {
        inline static constexpr auto PollInterval = std::chrono::milliseconds(250);
        inline static constexpr auto PollStep     = std::chrono::milliseconds(25);

        NowPlayingToast Toast;

        std::unique_ptr<NowPlayingRenderer> Renderer;
        std::unique_ptr<MediaSessionSource> Source;
        std::unique_ptr<audio::LoopbackCapture> Audio;

        audio::SpectrumFrame Spectrum;

        std::thread      PollThread;
        std::atomic_bool PollRunning { false };

        mutable std::mutex TrackMutex;
        NowPlayingTrack    LastTrack;

        bool ReportedPollError { false };

        float HoldDuration { NowPlayingToast::DefaultHoldDuration };

        std::atomic_bool ModuleEnabled { false };
        std::atomic_bool Registered { false };

        display::overlay::RenderCallbackHandle RenderHandle { 0 };

        std::shared_ptr<ll::io::Logger> logger;
    };

    MusicPlugin::MusicPlugin() : mImpl(std::make_unique<Impl>()) {};
    MusicPlugin::~MusicPlugin() = default;

    std::shared_ptr<MusicPlugin> MusicPlugin::getShared() {
        static auto instance = std::shared_ptr<MusicPlugin>(new MusicPlugin());
        return instance;
    }

    std::error_code MusicPlugin::makeErrorCode(MusicPluginErrorCode e) {
        static MusicPluginErrorCategory cat;
        return std::error_code{ static_cast<int>(e), cat };
    }

    std::shared_ptr<ll::io::Logger> MusicPlugin::getLogger() {
        return this->mImpl->logger;
    }

    NowPlayingTrack MusicPlugin::getCurrentTrack() const {
        std::scoped_lock lock(this->mImpl->TrackMutex);

        return this->mImpl->LastTrack;
    }

    void MusicPlugin::replay() {
        if (!this->isValid() || !this->mImpl->Renderer)
            return;

        this->mImpl->Renderer->replay();
    }

    bool MusicPlugin::isValid() {
        return this->mImpl->ModuleEnabled.load(std::memory_order_acquire) && this->mImpl->logger != nullptr;
    }

    void MusicPlugin::pollLoop() {
        while (this->mImpl->PollRunning.load(std::memory_order_acquire)) {
            NowPlayingTrack track;
            bool            available = false;

            if (this->mImpl->Source) {
                try {
                    available = this->mImpl->Source->poll(track);
                } catch (const std::exception& e) {
                    if (!this->mImpl->ReportedPollError) {
                        this->mImpl->ReportedPollError = true;
                        this->getLogger()->error("MusicPlugin - Poll threw: {}", e.what());
                    }

                    available = false;
                } catch (...) {
                    if (!this->mImpl->ReportedPollError) {
                        this->mImpl->ReportedPollError = true;
                        this->getLogger()->error("MusicPlugin - Poll threw an unknown exception");
                    }

                    available = false;
                }
            }

            if (available && track.isPlaying() && !track.empty() && track.hasArtwork()) {
                bool changed = false;

                {
                    std::scoped_lock lock(this->mImpl->TrackMutex);

                    changed = !track.isSameSong(this->mImpl->LastTrack);

                    if (changed)
                        this->mImpl->LastTrack = track;
                }

                if (changed && this->mImpl->Renderer)
                    this->mImpl->Renderer->enqueue(track, this->mImpl->HoldDuration);
            }

            for (auto waited = std::chrono::milliseconds(0); waited < Impl::PollInterval; waited += Impl::PollStep) {
                if (!this->mImpl->PollRunning.load(std::memory_order_acquire))
                    return;

                std::this_thread::sleep_for(Impl::PollStep);
            }
        }
    }

    void MusicPlugin::startPoll() {
        if (this->mImpl->PollRunning.load(std::memory_order_acquire))
            return;

        this->mImpl->PollRunning.store(true, std::memory_order_release);
        this->mImpl->PollThread = std::thread([this]() -> void {
            this->pollLoop();
        });
    }

    void MusicPlugin::stopPoll() {
        if (!this->mImpl->PollRunning.exchange(false, std::memory_order_acq_rel))
            return;

        if (this->mImpl->PollThread.joinable())
            this->mImpl->PollThread.join();
    }

    std::string MusicPlugin::getName() {
        return "MusicPlugin";
    }

    modules::ModulePriority MusicPlugin::getPriority() {
        return modules::ModulePriority::Normal;
    }

    ll::Expected<bool> MusicPlugin::load() {
        const Config::C_Config& config = ServiceProvider::getInstance()
            .getService<ReadOnlyWrapper<Config::C_Config>>("Config")
            ->get();

        if (!config.ClientConfig.Music.ModuleEnabled)
            return false;

        auto mDataPath = std::filesystem::path(ServiceProvider::getInstance().getService<std::string>("ConfigPath")->data());

        this->mImpl->logger = ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA");
        this->mImpl->ModuleEnabled.store(true, std::memory_order_release);

        return true;
    }

    ll::Expected<bool> MusicPlugin::unload() {
        if (!this->isValid())
            return false;

        this->stopPoll();

        this->mImpl->Renderer.reset();
        this->mImpl->Source.reset();
        this->mImpl->Audio.reset();
        this->mImpl->logger.reset();
        this->mImpl->ModuleEnabled.store(false, std::memory_order_release);

        return true;
    }

    ll::Expected<bool> MusicPlugin::registry() {
        if (!this->isValid())
            return false;

        const Config::C_Config& config = ServiceProvider::getInstance()
            .getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get();

        const Config::C_Music& music = config.ClientConfig.Music;

        MediaSessionFilter filter;
        filter.SourceAppFilters = music.SourceAppFilters;
        filter.IgnorePaused     = music.IgnorePaused;
        filter.OnFiltered       = [this](const std::string& sourceAppId) -> void {
            if (this->mImpl->logger)
                this->mImpl->logger->debug("MusicPlugin - ignored media session from '{}'", sourceAppId);
        };
        filter.OnError          = [this](const std::string& reason) -> void {
            if (this->mImpl->logger)
                this->mImpl->logger->warn("MusicPlugin - media session read failed: {}", reason);
        };
        filter.OnSessionCount   = [this](size_t count, bool anyPlaying) -> void {
            if (this->mImpl->logger)
                this->mImpl->logger->debug("MusicPlugin - sessions={} anyPlaying={}", count, anyPlaying);
        };

        this->mImpl->Source       = makeMediaSessionSource(std::move(filter));
        this->mImpl->HoldDuration = static_cast<float>(music.HoldDuration);

        NowPlayingToastStyle style;
        style.MaxWidth         = music.CardMaxWidth;
        style.MinWidth         = music.CardMinWidth;
        style.Padding          = music.CardPadding;
        style.TitleFontSize    = music.TitleFontSize;
        style.ArtistFontSize   = music.ArtistFontSize;
        style.AlbumFontSize    = music.AlbumFontSize;
        style.LineSpacing      = music.LineSpacing;
        style.CoverSize        = music.CoverSize;
        style.SlideDistance    = music.SlideDistance;
        style.VisualizerHeight = music.VisualizerHeight;
        style.VisualizerBarGap = music.VisualizerBarGap;
        style.EnterDuration    = music.EnterDuration;
        style.ExitDuration     = music.ExitDuration;

        this->mImpl->Toast.setStyle(style);

        this->mImpl->Renderer = std::make_unique<NowPlayingRenderer>(this->mImpl->Toast);
        this->mImpl->Renderer->setLogger(this->mImpl->logger);

        if (!display::overlay::applyHooks()) {
            this->mImpl->Renderer.reset();

            return ll::makeErrorCodeError(makeErrorCode(MusicPluginErrorCode::Invalid));
        }

        this->mImpl->RenderHandle = display::overlay::addRenderCallback(
            [this](float deltaTime, float screenWidth, float screenHeight) -> void {
                if (!this->mImpl->Renderer)
                    return;

                if (this->mImpl->Audio && this->mImpl->Audio->isRunning()) {
                    this->mImpl->Audio->analyze(this->mImpl->Spectrum);
                    this->mImpl->Renderer->setSpectrum(this->mImpl->Spectrum);
                }

                this->mImpl->Renderer->render(deltaTime, screenWidth, screenHeight);
            }
        );

        this->mImpl->Registered.store(true, std::memory_order_release);

        if (music.VisualizerHeight > 0.0f) {
            this->mImpl->Audio = std::make_unique<audio::LoopbackCapture>();

            if (!this->mImpl->Audio->start() || !this->mImpl->Audio->isRunning()) {
                this->getLogger()->warn(
                    "MusicPlugin - audio visualizer unavailable: {}",
                    this->mImpl->Audio->getLastError()
                );

                this->mImpl->Audio.reset();
            }
        }

        this->startPoll();

        this->getLogger()->info("MusicPlugin - Monitoring system media sessions");

        return true;
    }

    ll::Expected<bool> MusicPlugin::unregistry() {
        if (!this->mImpl->Registered.exchange(false, std::memory_order_acq_rel))
            return false;

        this->stopPoll();

        display::overlay::removeRenderCallback(this->mImpl->RenderHandle);
        this->mImpl->RenderHandle = 0;

        if (this->mImpl->Audio)
            this->mImpl->Audio->stop();

        if (this->mImpl->Renderer)
            this->mImpl->Renderer->releaseResources();

        display::overlay::removeHooks();

        return true;
    }
}
