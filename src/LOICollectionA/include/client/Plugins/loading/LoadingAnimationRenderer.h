#pragma once

#include <memory>
#include <string>
#include <vector>

#include <imgui.h>

#include "LOICollectionA/base/Macro.h"

namespace ll::io {
    class Logger;
}

namespace LOICollection::client::Plugins::loading {
    struct LoadingAnimationStyle {
        ImU32 BackgroundTopColor    { IM_COL32(10, 12, 16, 255) };
        ImU32 BackgroundBottomColor { IM_COL32(16, 24, 30, 255) };

        ImU32 GrassTopColor    { IM_COL32(124, 189, 59, 255) };
        ImU32 GrassTopShine    { IM_COL32(138, 210, 78, 255) };
        ImU32 DirtLeftColor    { IM_COL32(121, 85, 58, 255) };
        ImU32 DirtRightColor   { IM_COL32(96, 66, 44, 255) };

        ImU32 TitleColor { IM_COL32(232, 232, 232, 255) };
        ImU32 TipColor   { IM_COL32(154, 168, 158, 255) };

        ImU32 BarTrackColor    { IM_COL32(18, 22, 28, 255) };
        ImU32 BarFrameColor    { IM_COL32(74, 82, 92, 255) };
        ImU32 BarFillColor     { IM_COL32(106, 164, 50, 255) };
        ImU32 BarFillTopColor  { IM_COL32(155, 216, 84, 255) };
        ImU32 BarShineColor    { IM_COL32(216, 255, 176, 255) };

        float CubeSize        { 19.0f };
        float CubeHeightScale { 1.18f };
        float CubeFallHeight  { 340.0f };

        float LoopDuration  { 4.0f };
        float DissolveBegin { 2.9f };

        float BarWidth  { 440.0f };
        float BarHeight { 12.0f };

        float FadeInDuration  { 0.35f };
        float FadeOutDuration { 0.45f };

        std::string Title { "LOADING" };
        std::vector<std::string> Tips  { "Loading terrain..." };
    };

    class LoadingAnimationRenderer {
    public:
        ~LoadingAnimationRenderer();

        LoadingAnimationRenderer(LoadingAnimationRenderer const&)            = delete;
        LoadingAnimationRenderer(LoadingAnimationRenderer&&)                 = delete;
        LoadingAnimationRenderer& operator=(LoadingAnimationRenderer const&) = delete;
        LoadingAnimationRenderer& operator=(LoadingAnimationRenderer&&)      = delete;

    public:
        LOICOLLECTION_A_NDAPI static LoadingAnimationRenderer& getInstance();

        LOICOLLECTION_A_API void setLogger(std::shared_ptr<ll::io::Logger> logger);
        LOICOLLECTION_A_API void setStyle(const LoadingAnimationStyle& style);

        LOICOLLECTION_A_API void reset();
        LOICOLLECTION_A_API void render(float deltaTime, float screenWidth, float screenHeight);

    private:
        LoadingAnimationRenderer();

        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
