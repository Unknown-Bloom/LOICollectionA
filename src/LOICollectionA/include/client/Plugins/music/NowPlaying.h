#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace LOICollection::client::Plugins::music {
    enum class NowPlayingStatus : int {
        Unknown  = 0,
        Closed   = 1,
        Opened   = 2,
        Changing = 3,
        Stopped  = 4,
        Playing  = 5,
        Paused   = 6,
    };

    struct NowPlayingArtwork {
        uint32_t Width { 0 };
        uint32_t Height { 0 };

        std::vector<uint8_t> Pixels;

        [[nodiscard]] bool empty() const {
            return this->Width == 0 || this->Height == 0 || this->Pixels.empty();
        }

        [[nodiscard]] uint64_t contentHash() const {
            uint64_t hash = 1469598103934665603ull;

            for (uint8_t byte : this->Pixels) {
                hash ^= byte;
                hash *= 1099511628211ull;
            }

            return hash;
        }
    };

    struct NowPlayingTrack {
        std::string Title;
        std::string Artist;
        std::string Album;
        std::string SourceAppId;

        NowPlayingStatus Status { NowPlayingStatus::Unknown };

        NowPlayingArtwork Artwork;

        [[nodiscard]] bool isPlaying() const {
            return this->Status == NowPlayingStatus::Playing;
        }

        [[nodiscard]] bool isSameSong(const NowPlayingTrack& other) const {
            return this->Title == other.Title
                && this->Artist == other.Artist
                && this->SourceAppId == other.SourceAppId;
        }

        [[nodiscard]] bool empty() const {
            return this->Title.empty() && this->Artist.empty() && this->Album.empty();
        }

        [[nodiscard]] bool hasArtwork() const {
            return !this->Artwork.empty();
        }

        [[nodiscard]] std::string ArtworkKey() const {
            return this->SourceAppId + '\n' + this->Title + '\n' + this->Artist;
        }
    };
}

namespace LOICollection::client::Plugins {
    using NowPlayingStatus = music::NowPlayingStatus;
    using NowPlayingArtwork = music::NowPlayingArtwork;
    using NowPlayingTrack  = music::NowPlayingTrack;
}
