#include <string>
#include <memory>
#include <utility>
#include <objbase.h>
#include <windows.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include "LOICollectionA/include/client/Plugins/music/MusicText.h"
#include "LOICollectionA/include/client/Plugins/music/ThumbnailDecoder.h"
#include "LOICollectionA/include/client/Plugins/music/MediaSessionSource.h"
#include "LOICollectionA/include/client/Plugins/music/NowPlaying.h"

namespace LOICollection::client::Plugins::music {
    namespace {
        namespace MediaControl = winrt::Windows::Media::Control;
        namespace Streams      = winrt::Windows::Storage::Streams;

        std::string toUtf8(std::wstring_view source) {
            if (source.empty())
                return {};

            int length = ::WideCharToMultiByte(
                CP_UTF8,
                0,
                source.data(),
                static_cast<int>(source.size()),
                nullptr,
                0,
                nullptr,
                nullptr
            );
            if (length <= 0)
                return {};

            std::string result(static_cast<size_t>(length), '\0');
            ::WideCharToMultiByte(
                CP_UTF8,
                0,
                source.data(),
                static_cast<int>(source.size()),
                result.data(),
                length,
                nullptr,
                nullptr
            );

            return result;
        }

        NowPlayingStatus convertStatus(
            MediaControl::GlobalSystemMediaTransportControlsSessionPlaybackStatus status
        ) {
            using Source = MediaControl::GlobalSystemMediaTransportControlsSessionPlaybackStatus;

            switch (status) {
                case Source::Closed:   return NowPlayingStatus::Closed;
                case Source::Opened:   return NowPlayingStatus::Opened;
                case Source::Changing: return NowPlayingStatus::Changing;
                case Source::Stopped:  return NowPlayingStatus::Stopped;
                case Source::Playing:  return NowPlayingStatus::Playing;
                case Source::Paused:   return NowPlayingStatus::Paused;
                default:               return NowPlayingStatus::Unknown;
            }
        }

        template <typename TAsync, typename TAsyncResult = decltype(std::declval<TAsync>().GetResults())>
        bool pumpUntilComplete(TAsync const& operation, uint64_t budgetMs, TAsyncResult& result) {
            if (!operation)
                return false;

            ULONGLONG deadline = ::GetTickCount64() + budgetMs;

            while (operation.Status() == winrt::Windows::Foundation::AsyncStatus::Started) {
                MSG message {};
                while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    ::TranslateMessage(&message);
                    ::DispatchMessageW(&message);
                }

                if (::GetTickCount64() >= deadline)
                    return false;

                ::Sleep(1);
            }

            if (operation.Status() != winrt::Windows::Foundation::AsyncStatus::Completed)
                return false;

            result = operation.GetResults();

            return true;
        }
    }

    class WindowsMediaSessionSource final : public MediaSessionSource {
    public:
        explicit WindowsMediaSessionSource(MediaSessionFilter filter) : mFilter(std::move(filter)) {}
        ~WindowsMediaSessionSource() override {
            this->detach();
        }

        void release() noexcept override {
            this->mThumbnailRequest = nullptr;

            if (this->mManagerRequest) {
                static_cast<void>(this->mManagerRequest.Cancel());
                this->mManagerRequest = nullptr;
            }

            this->mManager = nullptr;
        }

        bool poll(NowPlayingTrack& track) override {
            if (!this->mInitialized) {
                static_cast<void>(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));

                this->mInitialized = true;
            }

            auto manager = this->getManager();
            if (!manager) {
                this->reportFailure("media session manager unavailable");

                return false;
            }

            this->clearFailure();

            MediaControl::GlobalSystemMediaTransportControlsSession session { nullptr };

            try {
                session = this->pickSession(manager);
            } catch (winrt::hresult_error const& error) {
                this->reportFailure("GetSessions failed: " + toUtf8(error.message()));

                return false;
            }

            if (!session) {
                track = NowPlayingTrack{};

                return true;
            }

            NowPlayingTrack result;

            try {
                result.SourceAppId = toUtf8(session.SourceAppUserModelId());
            } catch (winrt::hresult_error const&) {}

            try {
                auto playbackInfo = session.GetPlaybackInfo();
                if (playbackInfo)
                    result.Status = convertStatus(playbackInfo.PlaybackStatus());
            } catch (winrt::hresult_error const&) {}

            MediaControl::GlobalSystemMediaTransportControlsSessionMediaProperties properties { nullptr };

            bool decoded = false;

            try {
                decoded = pumpUntilComplete(session.TryGetMediaPropertiesAsync(), this->mFilter.Tuning.PumpBudgetMs, properties);
            } catch (winrt::hresult_error const& error) {
                this->reportFailure("TryGetMediaPropertiesAsync failed: " + toUtf8(error.message()));

                return false;
            }

            if (!decoded || !properties)
                return false;

            try {
                result.Title  = toUtf8(properties.Title());
                result.Artist = toUtf8(properties.Artist());
                result.Album  = toUtf8(properties.AlbumTitle());
            } catch (winrt::hresult_error const&) {}

            std::string songKey;

            if (!result.Title.empty() || !result.Artist.empty())
                songKey = result.Title + '\n' + result.Artist;

            if (!songKey.empty()) {
                if (this->mCurrentSongKey != songKey) {
                    this->mCurrentSongKey       = songKey;
                    this->mThumbnailRequest     = nullptr;
                    this->mThumbnailReady       = false;
                    this->mThumbnailLastHash    = 0;
                    this->mThumbnailStableSince = 0;
                    this->mThumbnailNextTry     = 0;
                }

                bool ready = this->readThumbnail(properties, songKey, result.Artwork);

                if (!ready) {
                    track = NowPlayingTrack{};

                    return true;
                }
            }

            track = std::move(result);

            return true;
        }

    private:
        MediaControl::GlobalSystemMediaTransportControlsSession pickSession(
            MediaControl::GlobalSystemMediaTransportControlsSessionManager const& manager
        ) {
            auto sessions = manager.GetSessions();

            MediaControl::GlobalSystemMediaTransportControlsSession playing { nullptr };
            MediaControl::GlobalSystemMediaTransportControlsSession matched { nullptr };

            bool   reportedFiltered = false;
            size_t count            = 0;

            for (auto const& candidate : sessions) {
                if (!candidate)
                    continue;

                ++count;

                MediaControl::GlobalSystemMediaTransportControlsSessionPlaybackInfo playbackInfo { nullptr };

                try {
                    playbackInfo = candidate.GetPlaybackInfo();
                } catch (winrt::hresult_error const&) {}

                if (!playbackInfo)
                    continue;

                if (convertStatus(playbackInfo.PlaybackStatus()) != NowPlayingStatus::Playing)
                    continue;

                if (!playing)
                    playing = candidate;

                std::string sourceAppId;

                try {
                    sourceAppId = toUtf8(candidate.SourceAppUserModelId());
                } catch (winrt::hresult_error const&) {}

                if (this->isMatchSourceApp(sourceAppId)) {
                    if (!matched)
                        matched = candidate;
                } else {
                    if (!reportedFiltered && !this->mReportedFiltered && this->mFilter.OnFiltered) {
                        reportedFiltered        = true;
                        this->mReportedFiltered = true;

                        this->mFilter.OnFiltered(sourceAppId);
                    }
                }
            }

            if (this->mReportedSessionCount != count) {
                this->mReportedSessionCount = count;

                if (this->mFilter.OnSessionCount)
                    this->mFilter.OnSessionCount(count, playing != nullptr);
            }

            if (this->mFilter.SourceAppFilters.empty())
                return playing;

            return matched ? matched : playing;
        }

        void reportFailure(const std::string& reason) {
            if (!this->mFilter.OnError)
                return;

            if (reason == this->mLastFailure)
                return;

            this->mLastFailure = reason;

            this->mFilter.OnError(reason);
        }

        void clearFailure() {
            this->mLastFailure.clear();
        }

        MediaControl::GlobalSystemMediaTransportControlsSessionManager getManager() {
            if (this->mManager)
                return this->mManager;

            if (!this->mManagerRequest) {
                try {
                    this->mManagerRequest =
                        MediaControl::GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
                } catch (winrt::hresult_error const& error) {
                    this->reportFailure("RequestAsync threw: " + toUtf8(error.message()));

                    return nullptr;
                }

                if (!this->mManagerRequest) {
                    this->reportFailure("RequestAsync returned null");

                    return nullptr;
                }
            }

            MediaControl::GlobalSystemMediaTransportControlsSessionManager manager { nullptr };

            try {
                if (!pumpUntilComplete(this->mManagerRequest, this->mFilter.Tuning.PumpBudgetMs, manager))
                    return nullptr;

                this->mManager        = manager;
                this->mManagerRequest = nullptr;
            } catch (winrt::hresult_error const& error) {
                this->mManagerRequest = nullptr;

                this->reportFailure("RequestAsync failed: " + toUtf8(error.message()));

                return nullptr;
            }

            return this->mManager;
        }

        bool isMatchSourceApp(std::string_view sourceAppId) const {
            if (this->mFilter.SourceAppFilters.empty())
                return true;

            for (const std::string& keyword : this->mFilter.SourceAppFilters) {
                if (matchKeyword(sourceAppId, keyword))
                    return true;
            }

            return false;
        }

        bool readThumbnail(
            const MediaControl::GlobalSystemMediaTransportControlsSessionMediaProperties& properties,
            const std::string&                                                            songKey,
            NowPlayingArtwork&                                                            artwork
        ) {
            const auto& tuning = this->mFilter.Tuning;

            try {
                if (this->mThumbnailReady)
                    return true;

                if (this->mThumbnailRequest) {
                    Streams::IRandomAccessStreamWithContentType stream { nullptr };

                    if (!pumpUntilComplete(this->mThumbnailRequest, tuning.PumpBudgetMs, stream))
                        return false;

                    this->mThumbnailRequest = nullptr;

                    if (!stream || stream.Size() == 0 || stream.Size() > tuning.ThumbnailMaxBytes) {
                        this->scheduleThumbnailRetry();

                        return false;
                    }

                    auto reader = Streams::DataReader(stream);

                    uint32_t loaded = 0;

                    if (!pumpUntilComplete(reader.LoadAsync(static_cast<uint32_t>(stream.Size())), tuning.PumpBudgetMs, loaded)
                        || loaded == 0) {
                        this->scheduleThumbnailRetry();

                        return false;
                    }

                    std::vector<uint8_t> source(loaded);
                    reader.ReadBytes(source);

                    if (songKey != this->mCurrentSongKey) {
                        artwork = NowPlayingArtwork{};

                        return false;
                    }

                    NowPlayingArtwork decoded;

                    if (!decodeThumbnail(source, tuning.ThumbnailMaxEdge, decoded)) {
                        artwork = NowPlayingArtwork{};
                        this->scheduleThumbnailRetry();

                        return false;
                    }

                    ULONGLONG now     = ::GetTickCount64();
                    uint64_t  hash    = decoded.contentHash();
                    bool      settled = false;

                    if (hash == this->mThumbnailLastHash) {
                        if (this->mThumbnailStableSince == 0)
                            this->mThumbnailStableSince = now;

                        settled = now >= this->mThumbnailStableSince
                               && (now - this->mThumbnailStableSince) >= tuning.StableWindowMs;
                    } else {
                        this->mThumbnailLastHash    = hash;
                        this->mThumbnailStableSince = now;
                    }

                    if (settled) {
                        artwork               = std::move(decoded);
                        this->mThumbnailReady = true;

                        return true;
                    }

                    artwork = NowPlayingArtwork{};
                    this->scheduleThumbnailRetry();

                    return false;
                }

                ULONGLONG now = ::GetTickCount64();

                if (now < this->mThumbnailNextTry)
                    return false;

                auto reference = properties.Thumbnail();

                if (!reference) {
                    this->scheduleThumbnailRetry();

                    return false;
                }

                this->mThumbnailRequest = reference.OpenReadAsync();

                if (!this->mThumbnailRequest)
                    this->scheduleThumbnailRetry();
            } catch (winrt::hresult_error const&) {
                this->mThumbnailRequest = nullptr;
                artwork                 = NowPlayingArtwork{};

                this->scheduleThumbnailRetry();
            }

            return false;
        }

        void scheduleThumbnailRetry() {
            this->mThumbnailNextTry = ::GetTickCount64() + this->mFilter.Tuning.ThumbnailRetryDelayMs;
        }

        void detach() noexcept {
            static_cast<void>(::winrt::detach_abi(this->mThumbnailRequest));
            static_cast<void>(::winrt::detach_abi(this->mManagerRequest));
            static_cast<void>(::winrt::detach_abi(this->mManager));
        }

    private:
        MediaSessionFilter mFilter;

        bool mInitialized { false };
        bool mReportedFiltered { false };

        size_t mReportedSessionCount { static_cast<size_t>(-1) };

        std::string mLastFailure;
        std::string mCurrentSongKey;

        bool      mThumbnailReady { false };
        uint64_t  mThumbnailLastHash { 0 };
        ULONGLONG mThumbnailStableSince { 0 };
        ULONGLONG mThumbnailNextTry { 0 };

        MediaControl::GlobalSystemMediaTransportControlsSessionManager mManager { nullptr };
        winrt::Windows::Foundation::IAsyncOperation<MediaControl::GlobalSystemMediaTransportControlsSessionManager>
            mManagerRequest { nullptr };
        winrt::Windows::Foundation::IAsyncOperation<Streams::IRandomAccessStreamWithContentType> mThumbnailRequest {
            nullptr
        };
    };

    std::unique_ptr<MediaSessionSource> makeMediaSessionSource(MediaSessionFilter filter) {
        return std::make_unique<WindowsMediaSessionSource>(std::move(filter));
    }
}
