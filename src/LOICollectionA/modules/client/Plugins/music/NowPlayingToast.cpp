#include <string>
#include <cmath>
#include <algorithm>

#include "LOICollectionA/include/client/Plugins/music/MusicText.h"
#include "LOICollectionA/include/client/Plugins/music/NowPlayingToast.h"

namespace LOICollection::client::Plugins::music::detail {
    float estimatedUnits(std::string_view text) {
        float units = 0.0f;

        for (size_t index = 0; index < text.size();) {
            size_t next = nextCodepointOffset(text, index);

            units += isAsciiOnly(text.substr(index, next - index)) ? 1.0f : 2.0f;

            index = next;
        }

        return units;
    }

    float measureEstimated(std::string_view text, float fontSize) {
        return estimatedUnits(text) * fontSize;
    }
}

namespace LOICollection::client::Plugins::music {
    using namespace detail;

    float NowPlayingToast::easeOutBack(float t) const {
        float clamped = std::clamp(t, 0.0f, 1.0f);
        float p       = clamped - 1.0f;

        return 1.0f + (EaseOvershoot + 1.0f) * p * p * p + EaseOvershoot * p * p;
    }

    float NowPlayingToast::getEasedProgress() const {
        return this->easeOutBack(this->mProgress);
    }

    float NowPlayingToast::getProgress() const {
        return this->mProgress;
    }

    void NowPlayingToast::update(float deltaTime) {
        if (deltaTime <= 0.0f || this->mPhase == Phase::Hidden)
            return;

        this->mElapsed += deltaTime;

        float ratio = std::clamp(this->mElapsed / std::max(this->mDuration, MinimumAnimationDuration), 0.0f, 1.0f);

        if (this->mPhase == Phase::Entered) {
            this->mProgress = this->mStartProgress + (this->mTargetProgress - this->mStartProgress) * ratio;

            if (ratio >= 1.0f) {
                this->mProgress = this->mTargetProgress;

                this->mHoldElapsed += deltaTime;

                if (this->mHoldElapsed >= this->mHoldDuration)
                    this->dismiss();
            }

            return;
        }

        this->mProgress = this->mTargetProgress * (1.0f - ratio);

        if (ratio >= 1.0f) {
            this->mProgress = 0.0f;
            this->mPhase    = Phase::Hidden;
        }
    }

    void NowPlayingToast::show(const NowPlayingTrack& track, float holdDuration) {
        this->mTrack        = track;
        this->mHoldDuration = std::max(holdDuration, this->mStyle.EnterDuration + this->mStyle.ExitDuration);
        this->mHoldElapsed  = 0.0f;

        this->mStartProgress  = this->mProgress;
        this->mTargetProgress = 1.0f;

        float remaining = 1.0f - this->mStartProgress;

        this->mDuration = this->mStyle.EnterDuration * remaining;
        this->mElapsed  = 0.0f;

        this->mPhase = Phase::Entered;

        if (this->mDuration <= MinimumAnimationDuration) {
            this->mStartProgress = 0.0f;
            this->mDuration      = this->mStyle.EnterDuration;
        }
    }

    void NowPlayingToast::dismiss() {
        if (this->mPhase == Phase::Hidden || this->mPhase == Phase::Dismissed)
            return;

        this->mDuration       = this->mStyle.ExitDuration;
        this->mElapsed        = 0.0f;
        this->mTargetProgress = this->mProgress;

        this->mPhase = Phase::Dismissed;
    }

    bool NowPlayingToast::isVisible() const {
        return this->mPhase != Phase::Hidden;
    }

    bool NowPlayingToast::isFinished() const {
        return this->mPhase == Phase::Hidden;
    }

    bool NowPlayingToast::isExiting() const {
        return this->mPhase == Phase::Dismissed;
    }

    bool NowPlayingToast::isEntering() const {
        return this->mPhase == Phase::Entered && this->mProgress < 1.0f;
    }

    const NowPlayingTrack& NowPlayingToast::getTrack() const {
        return this->mTrack;
    }

    bool NowPlayingToast::hasArtwork() const {
        return this->mArtworkAvailable;
    }

    void NowPlayingToast::setArtworkAvailable(bool available) {
        this->mArtworkAvailable = available;
    }

    std::string NowPlayingToast::fit(
        std::string_view     text,
        float                availableWidth,
        float                fontSize,
        const TextMeasureFn& measurer
    ) const {
        if (text.empty() || availableWidth <= 0.0f || fontSize <= 0.0f)
            return std::string(text);

        auto measure = [&measurer, fontSize](std::string_view value) -> float {
            return measurer ? measurer(value, fontSize) : measureEstimated(value, fontSize);
        };

        if (measure(text) <= availableWidth)
            return std::string(text);

        float       reserved = measure(MusicTextEllipsis);
        std::string result;

        for (size_t index = 0; index < text.size();) {
            size_t next = nextCodepointOffset(text, index);

            std::string candidate = result;
            candidate.append(text.substr(index, next - index));

            if (measure(candidate) + reserved > availableWidth)
                break;

            result = std::move(candidate);
            index  = next;
        }

        return result + std::string(MusicTextEllipsis);
    }

    NowPlayingCard NowPlayingToast::getCard(const TextMeasureFn& measurer) const {
        auto measure = [&measurer](std::string_view text, float fontSize) -> float {
            return measurer ? measurer(text, fontSize) : measureEstimated(text, fontSize);
        };

        NowPlayingCard card;

        bool  hasCover  = this->mArtworkAvailable && this->mStyle.CoverSize > 0.0f;
        float coverSpan = hasCover ? this->mStyle.CoverSize + this->mStyle.Padding : 0.0f;

        float availableWidth = this->mStyle.MaxWidth - coverSpan - this->mStyle.Padding * 2.0f - 2.0f;

        card.Title = this->fit(formatTrackTitle(this->mTrack), availableWidth, this->mStyle.TitleFontSize, measurer);

        card.Subtitle = this->fit(
            formatTrackArtist(this->mTrack),
            availableWidth,
            this->mStyle.ArtistFontSize,
            measurer
        );

        std::string album = formatTrackAlbum(this->mTrack);

        card.Album = album.empty()
                   ? std::string{}
                   : this->fit(album, availableWidth, this->mStyle.AlbumFontSize, measurer);

        card.HasCover  = hasCover;
        card.CoverSize = hasCover ? this->mStyle.CoverSize : 0.0f;

        float titleWidth  = measure(card.Title, this->mStyle.TitleFontSize);
        float artistWidth = measure(card.Subtitle, this->mStyle.ArtistFontSize);
        float albumWidth  = card.Album.empty() ? 0.0f : measure(card.Album, this->mStyle.AlbumFontSize);

        card.Width = std::clamp(
            std::max({ titleWidth, artistWidth, albumWidth }) + coverSpan + (this->mStyle.Padding + 2.0f) * 2.0f,
            this->mStyle.MinWidth,
            this->mStyle.MaxWidth
        );

        float textHeight = this->mStyle.TitleFontSize + this->mStyle.LineSpacing + this->mStyle.ArtistFontSize;

        if (!card.Album.empty())
            textHeight += this->mStyle.LineSpacing + this->mStyle.AlbumFontSize;

        float visualizerHeight = this->mStyle.VisualizerHeight > 0.0f ? this->mStyle.VisualizerHeight : 0.0f;

        card.Height = this->mStyle.Padding * 2.0f + std::max(textHeight, card.CoverSize)
                    + (visualizerHeight > 0.0f ? visualizerHeight + this->mStyle.LineSpacing : 0.0f);

        return card;
    }

    const NowPlayingToastStyle& NowPlayingToast::getStyle() const {
        return this->mStyle;
    }

    void NowPlayingToast::setStyle(const NowPlayingToastStyle& style) {
        this->mStyle = style;
    }
}
