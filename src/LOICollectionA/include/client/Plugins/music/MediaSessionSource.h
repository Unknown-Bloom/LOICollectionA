#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include <cstdint>
#include <functional>

#include "LOICollectionA/include/client/Plugins/music/NowPlaying.h"

#include "LOICollectionA/base/Macro.h"

namespace LOICollection::client::Plugins::music {
    struct MediaSessionTuning {
        uint32_t ThumbnailMaxEdge { 128 };
        uint32_t ThumbnailMaxBytes { 4u * 1024u * 1024u };

        uint64_t PumpBudgetMs { 100 };
        uint64_t ThumbnailRetryDelayMs { 400 };
        uint64_t StableWindowMs { 1200 };
    };

    struct MediaSessionFilter {
        std::vector<std::string> SourceAppFilters;

        bool IgnorePaused { true };

        MediaSessionTuning Tuning;

        std::function<void(const std::string&)> OnFiltered;
        std::function<void(const std::string&)> OnError;
        std::function<void(size_t, bool)>       OnSessionCount;
    };

    class MediaSessionSource {
    public:
        MediaSessionSource()          = default;
        virtual ~MediaSessionSource() = default;

        MediaSessionSource(MediaSessionSource const&)            = delete;
        MediaSessionSource(MediaSessionSource&&)                 = delete;
        MediaSessionSource& operator=(MediaSessionSource const&) = delete;
        MediaSessionSource& operator=(MediaSessionSource&&)      = delete;

    public:
        LOICOLLECTION_A_NDAPI virtual bool poll(NowPlayingTrack& track) = 0;

        virtual void release() noexcept {}
    };

    LOICOLLECTION_A_NDAPI std::unique_ptr<MediaSessionSource> makeMediaSessionSource(MediaSessionFilter filter);
}
