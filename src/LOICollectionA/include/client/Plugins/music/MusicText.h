#pragma once

#include <string>
#include <cstddef>

#include "LOICollectionA/include/client/Plugins/music/NowPlaying.h"

#include "LOICollectionA/base/Macro.h"

namespace LOICollection::client::Plugins::music {
    inline constexpr std::string_view MusicTextPlaceholder = "-";
    inline constexpr std::string_view MusicTextEllipsis    = "…";

    LOICOLLECTION_A_NDAPI bool isAsciiOnly(std::string_view text);

    LOICOLLECTION_A_NDAPI size_t nextCodepointOffset(std::string_view text, size_t offset);

    LOICOLLECTION_A_NDAPI std::string lowerAscii(std::string_view text);

    LOICOLLECTION_A_NDAPI bool matchKeyword(std::string_view text, std::string_view keyword);

    LOICOLLECTION_A_NDAPI std::string formatTrackTitle(const NowPlayingTrack& track);

    LOICOLLECTION_A_NDAPI std::string formatTrackArtist(const NowPlayingTrack& track);

    LOICOLLECTION_A_NDAPI std::string formatTrackAlbum(const NowPlayingTrack& track);
}
