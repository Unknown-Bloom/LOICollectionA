#pragma once

#include <cstdint>
#include <vector>

#include "LOICollectionA/include/client/Plugins/music/NowPlaying.h"

#include "LOICollectionA/base/Macro.h"

namespace LOICollection::client::Plugins::music {
    LOICOLLECTION_A_NDAPI bool decodeThumbnail(
        const std::vector<uint8_t>& source,
        uint32_t                    maxEdge,
        NowPlayingArtwork&          artwork
    );
}
