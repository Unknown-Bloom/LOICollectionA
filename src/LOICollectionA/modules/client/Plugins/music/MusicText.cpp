#include <cctype>
#include <string>

#include "LOICollectionA/include/client/Plugins/music/MusicText.h"
#include "LOICollectionA/include/client/Plugins/music/NowPlaying.h"

namespace LOICollection::client::Plugins::music {
    bool isAsciiOnly(std::string_view text) {
        for (unsigned char character : text) {
            if (character >= 0x80)
                return false;
        }

        return true;
    }

    size_t nextCodepointOffset(std::string_view text, size_t offset) {
        if (offset >= text.size())
            return text.size();

        size_t next = offset + 1;

        while (next < text.size() && (static_cast<unsigned char>(text[next]) & 0xC0) == 0x80)
            ++next;

        return next;
    }

    std::string lowerAscii(std::string_view text) {
        std::string result;
        result.reserve(text.size());

        for (char character : text)
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));

        return result;
    }

    bool matchKeyword(std::string_view text, std::string_view keyword) {
        if (keyword.empty())
            return true;

        return lowerAscii(text).find(lowerAscii(keyword)) != std::string::npos;
    }

    std::string formatTrackTitle(const NowPlayingTrack& track) {
        if (track.Title.empty())
            return std::string(MusicTextPlaceholder);

        return track.Title;
    }

    std::string formatTrackArtist(const NowPlayingTrack& track) {
        if (track.Artist.empty())
            return track.Album.empty() ? std::string(MusicTextPlaceholder) : track.Album;

        if (track.Album.empty() || track.Album == track.Artist)
            return track.Artist;

        return track.Artist + " · " + track.Album;
    }

    std::string formatTrackAlbum(const NowPlayingTrack& track) {
        if (track.Album.empty())
            return {};

        return track.Album;
    }
}
