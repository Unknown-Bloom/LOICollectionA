#pragma once

#include <memory>
#include <mutex>
#include <functional>

#include <imgui.h>

#include "LOICollectionA/include/client/Plugins/music/NowPlaying.h"
#include "LOICollectionA/include/client/Plugins/music/NowPlayingToast.h"
#include "LOICollectionA/include/client/Plugins/music/audio/SpectrumAnalyzer.h"

#include "LOICollectionA/base/Macro.h"

struct ImDrawList;

namespace ll::io {
    class Logger;
}

namespace LOICollection::client::Plugins::music {
    struct NowPlayingLayout {
        float RightMargin { 16.0f };
        float BottomMargin { 24.0f };
        float AccentWidth { 3.0f };
        float Rounding { 6.0f };
        float MaxFrameDeltaTime { 0.1f };
        float MinimumScreenEdge { 64.0f };
        float ShadowOffsetX { 2.0f };
        float ShadowOffsetY { 3.0f };
        float DetailGap { 6.0f };
        float MinimumBarWidth { 1.5f };
        float BarHeightRatio { 0.06f };
        float BarHighlightHeight { 2.0f };
        float BarRoundingRatio { 0.35f };
        float ContentTopRatio { 0.8f };
        float VisualizerGapRatio { 0.5f };
        float VisualizerBottomRatio { 0.4f };
        float HoverRoundingRatio { 0.6f };
        float DetailBottomPaddingRatio { 0.5f };
    };

    class NowPlayingRenderer {
    public:
        using AlphaFn = std::function<ImU32(ImU32)>;

        explicit NowPlayingRenderer(NowPlayingToast& toast);
        ~NowPlayingRenderer();

        NowPlayingRenderer(NowPlayingRenderer const&)            = delete;
        NowPlayingRenderer(NowPlayingRenderer&&)                 = delete;
        NowPlayingRenderer& operator=(NowPlayingRenderer const&) = delete;
        NowPlayingRenderer& operator=(NowPlayingRenderer&&)      = delete;

    public:
        LOICOLLECTION_A_API void enqueue(const NowPlayingTrack& track, float holdDuration);

        LOICOLLECTION_A_API void replay();

        LOICOLLECTION_A_API void dismiss();

        LOICOLLECTION_A_API void setLogger(std::shared_ptr<ll::io::Logger> logger);

        LOICOLLECTION_A_API void setSpectrum(const audio::SpectrumFrame& spectrum);

        LOICOLLECTION_A_API void setLayout(const NowPlayingLayout& layout);

        LOICOLLECTION_A_NDAPI const NowPlayingLayout& getLayout() const;

        LOICOLLECTION_A_API void render(float deltaTime, float screenWidth, float screenHeight);

        LOICOLLECTION_A_API void releaseResources();

    private:
        void update(float deltaTime);

        void draw(ImDrawList& drawList, float screenWidth, float screenHeight);

        void drawVisualizer(
            ImDrawList&                 drawList,
            float                       x0,
            float                       x1,
            float                       y1,
            const NowPlayingToastStyle& style,
            const AlphaFn&              withAlpha
        );

        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };

    LOICOLLECTION_A_NDAPI std::string shortAppName(std::string_view sourceAppId);
}
