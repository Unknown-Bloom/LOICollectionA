#pragma once

#include <string>
#include <functional>

#include "LOICollectionA/include/client/Plugins/music/NowPlaying.h"

#include "LOICollectionA/base/Macro.h"

namespace LOICollection::client::Plugins::music {
    struct NowPlayingToastStyle {
        float MaxWidth = 560.0f;
        float MinWidth = 300.0f;

        float Padding = 12.0f;

        float TitleFontSize = 24.0f;
        float ArtistFontSize = 16.0f;
        float AlbumFontSize = 14.0f;

        float LineSpacing = 6.0f;

        float SlideDistance = 48.0f;

        float EnterDuration = 0.5f;
        float ExitDuration = 0.4f;

        float CoverSize = 64.0f;

        float VisualizerHeight = 22.0f;

        float VisualizerBarGap = 2.0f;
    };

    struct NowPlayingCard {
        std::string Title;
        std::string Subtitle;
        std::string Album;

        float Width = 0.0f;
        float Height = 0.0f;

        bool  HasCover = false;
        float CoverSize = 0.0f;
    };

    using TextMeasureFn = std::function<float(std::string_view, float)>;

    class NowPlayingToast {
    public:
        inline static constexpr float EaseOvershoot = 1.70158f;
        inline static constexpr float DefaultHoldDuration = 8.0f;
        inline static constexpr float MinimumAnimationDuration = 0.001f;

        NowPlayingToast() = default;

        LOICOLLECTION_A_API void update(float deltaTime);

        LOICOLLECTION_A_API void show(const NowPlayingTrack& track, float holdDuration);
        LOICOLLECTION_A_API void dismiss();

        LOICOLLECTION_A_NDAPI bool isVisible() const;
        LOICOLLECTION_A_NDAPI bool isFinished() const;
        LOICOLLECTION_A_NDAPI bool isExiting() const;
        LOICOLLECTION_A_NDAPI bool isEntering() const;

        LOICOLLECTION_A_NDAPI const NowPlayingTrack& getTrack() const;

        LOICOLLECTION_A_NDAPI float getProgress() const;

        LOICOLLECTION_A_NDAPI float getEasedProgress() const;

        LOICOLLECTION_A_NDAPI NowPlayingCard getCard(const TextMeasureFn& measurer = {}) const;

        LOICOLLECTION_A_NDAPI bool hasArtwork() const;

        LOICOLLECTION_A_API void setArtworkAvailable(bool available);

        LOICOLLECTION_A_NDAPI const NowPlayingToastStyle& getStyle() const;

        LOICOLLECTION_A_API void setStyle(const NowPlayingToastStyle& style);

    private:
        enum class Phase : int {
            Hidden    = 0,
            Entered   = 1,
            Dismissed = 2
        };

        [[nodiscard]] float easeOutBack(float t) const;

        [[nodiscard]] std::string fit(
            std::string_view     text,
            float                availableWidth,
            float                fontSize,
            const TextMeasureFn& measurer
        ) const;

    private:
        NowPlayingToastStyle mStyle;
        NowPlayingTrack mTrack;

        bool mArtworkAvailable { false };

        float mProgress { 0.0f };

        float mElapsed { 0.0f };
        float mDuration { 1.0f };
        float mStartProgress { 0.0f };
        float mTargetProgress { 1.0f };

        float mHoldElapsed { 0.0f };
        float mHoldDuration { DefaultHoldDuration };

        Phase mPhase { Phase::Hidden };
    };
}
