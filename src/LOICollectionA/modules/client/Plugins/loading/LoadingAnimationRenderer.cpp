#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <algorithm>

#include <imgui.h>

#include <ll/api/io/Logger.h>

#include "LOICollectionA/include/client/display/overlay/Overlay.h"

#include "LOICollectionA/include/client/Plugins/loading/LoadingScreenDetector.h"
#include "LOICollectionA/include/client/Plugins/loading/LoadingAnimationRenderer.h"

namespace LOICollection::client::Plugins::loading::detail {
    constexpr float Pi = 3.14159265358979323846f;

    float clamp01(float value) {
        return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    }

    float lerp(float a, float b, float t) {
        return a + (b - a) * t;
    }

    float easeOutCubic(float t) {
        float inv = 1.0f - clamp01(t);
        return 1.0f - inv * inv * inv;
    }

    float easeInCubic(float t) {
        t = clamp01(t);
        return t * t * t;
    }

    float easeInOutSine(float t) {
        return 0.5f - 0.5f * std::cos(clamp01(t) * Pi);
    }

    float smoothstep(float t) {
        t = clamp01(t);
        return t * t * (3.0f - 2.0f * t);
    }

    float hash01(int seed) {
        float x = std::sin(static_cast<float>(seed) * 12.9898f + 78.233f) * 43758.5453f;
        return x - std::floor(x);
    }

    ImU32 withAlpha(ImU32 color, float alpha) {
        auto a = static_cast<unsigned int>((color >> 24) & 0xFFu);
        a = static_cast<unsigned int>(static_cast<float>(a) * clamp01(alpha));

        return (color & 0x00FFFFFFu) | (static_cast<ImU32>(a) << 24);
    }

    struct VoxelMetrics {
        float W; 
        float T; 
        float D; 
        float Gap; 
        float TopExtent;
        float BottomExtent;
    };

    VoxelMetrics voxelMetrics(float cubeSize, float heightScale) {
        VoxelMetrics m{};

        m.W = cubeSize;
        m.T = cubeSize * 0.5f;
        m.D = cubeSize * heightScale;
        m.Gap = 1.04f;

        m.TopExtent = 2.0f * m.D * m.Gap + m.T;
        m.BottomExtent = 4.0f * m.T * m.Gap + m.T + m.D;

        return m;
    }

    void drawCube(
        ImDrawList* drawList,
        ImVec2 center,
        float size,
        float heightScale,
        float scale,
        float alpha,
        ImU32 topColor,
        ImU32 leftColor,
        ImU32 rightColor,
        float shine
    ) {
        float s = size * scale;
        float w = s;
        float t = s * 0.5f;
        float d = s * heightScale;

        ImVec2 tp { center.x, center.y - t };
        ImVec2 rp { center.x + w, center.y };
        ImVec2 bp { center.x, center.y + t };
        ImVec2 lp { center.x - w, center.y };

        drawList->AddQuadFilled(
            lp,
            bp,
            ImVec2(bp.x, bp.y + d),
            ImVec2(lp.x, lp.y + d),
            withAlpha(leftColor, alpha)
        );
        drawList->AddQuadFilled(
            rp,
            bp,
            ImVec2(bp.x, bp.y + d),
            ImVec2(rp.x, rp.y + d),
            withAlpha(rightColor, alpha)
        );
        drawList->AddQuadFilled(tp, rp, bp, lp, withAlpha(topColor, alpha));

        if (shine > 0.001f)
            drawList->AddQuadFilled(tp, rp, bp, lp, withAlpha(IM_COL32(255, 255, 255, 70), alpha * shine));
    }
}

namespace LOICollection::client::Plugins::loading {
    using namespace detail;

    struct LoadingAnimationRenderer::Impl {
        std::shared_ptr<ll::io::Logger> Logger;

        LoadingAnimationStyle Style;

        float Visibility { 0.0f };
        float Time { 0.0f };

        float DisplayProgress { 0.0f };
        bool  ProgressKnown { false };

        std::string ShownTip;
        float       TipAlpha { 0.0f };

        static constexpr int VoxelCount  = 27;
        static constexpr int ParticleNum = 16;
        static constexpr int FlowTriNum  = 12;

        void advance(float deltaTime) {
            LoadingScreenDetector& detector = LoadingScreenDetector::getInstance();

            bool modal  = detector.isBlockingModalActive();
            bool active = detector.isLoadingScreenActive() && !modal;

            float duration = active ? this->Style.FadeInDuration : this->Style.FadeOutDuration;
            float rate     = duration > 0.0f ? deltaTime / duration : 1.0f;

            if (active)
                this->Visibility = std::min(this->Visibility + rate, 1.0f);
            else
                this->Visibility = std::max(this->Visibility - rate, 0.0f);

            float target = detector.loadingProgress();

            this->ProgressKnown = target >= 0.0f;

            if (this->ProgressKnown) {
                float speed = target >= this->DisplayProgress ? 6.0f : 3.0f;
                float blend = std::min(1.0f, deltaTime * speed);

                this->DisplayProgress += (target - this->DisplayProgress) * blend;
            }

            if (this->Visibility > 0.0f) {
                this->Time += deltaTime;
            } else {
                this->Time            = 0.0f;
                this->DisplayProgress = 0.0f;
            }

            std::string tip = detector.progressMessage();

            if (tip.empty() && !this->Style.Tips.empty()) {
                size_t index = static_cast<size_t>(this->Time / 3.0f) % this->Style.Tips.size();
                tip = this->Style.Tips[index];
            }

            if (tip != this->ShownTip) {
                this->ShownTip = std::move(tip);
                this->TipAlpha = 0.0f;
            }

            if (!this->ShownTip.empty())
                this->TipAlpha = std::min(1.0f, this->TipAlpha + deltaTime / 0.25f);
        }

        void drawBackground(ImDrawList* drawList, float width, float height, float alpha) {
            drawList->AddRectFilledMultiColor(
                ImVec2(0.0f, 0.0f),
                ImVec2(width, height),
                withAlpha(this->Style.BackgroundTopColor, alpha),
                withAlpha(this->Style.BackgroundTopColor, alpha),
                withAlpha(this->Style.BackgroundBottomColor, alpha),
                withAlpha(this->Style.BackgroundBottomColor, alpha)
            );

            for (int n = 0; n < ParticleNum; ++n) {
                float seedX = hash01(n * 7 + 1);
                float seedY = hash01(n * 7 + 2);
                float seedS = hash01(n * 7 + 3);

                float speed = 12.0f + seedS * 18.0f;
                float span  = height + 60.0f;

                float x = seedX * width + std::sin(this->Time * 0.4f + static_cast<float>(n)) * 14.0f;
                float y = std::fmod(seedY * span + this->Time * speed, span) - 30.0f;

                float size   = 2.0f + seedS * 3.0f;
                float flicker = 0.5f + 0.5f * std::sin(this->Time * 1.6f + static_cast<float>(n) * 1.7f);
                float pAlpha  = alpha * (0.05f + 0.09f * flicker);

                ImU32 color = (n % 3 == 0) ? IM_COL32(124, 189, 59, 255) : IM_COL32(91, 168, 155, 255);

                drawList->AddRectFilled(
                    ImVec2(x, y),
                    ImVec2(x + size, y + size),
                    withAlpha(color, pAlpha)
                );
            }

            this->drawFlowTriangles(drawList, width, height, alpha);
        }

        void drawFlowTriangles(ImDrawList* drawList, float width, float height, float alpha) {
            float span = width + 160.0f;

            for (int n = 0; n < FlowTriNum; ++n) {
                float seedX = hash01(n * 31 + 3);
                float seedY = hash01(n * 31 + 5);
                float seedS = hash01(n * 31 + 7);
                float seedV = hash01(n * 31 + 11);

                float speed = 26.0f + seedV * 44.0f;
                float x     = std::fmod(seedX * span + this->Time * speed, span) - 80.0f;
                float y     = seedY * height + std::sin(this->Time * 0.6f + static_cast<float>(n) * 1.3f) * 10.0f;
                float size  = 6.0f + seedS * 9.0f;

                float edge = clamp01(std::min(x + 80.0f, span - 80.0f - x) / 120.0f);

                float triAlpha = alpha * edge * (0.05f + 0.06f * seedS);

                if (triAlpha <= 0.004f)
                    continue;

                float tilt = (hash01(n * 31 + 13) - 0.5f) * 0.5f;
                float cs   = std::cos(tilt);
                float sn   = std::sin(tilt);

                const ImVec2 local[3] = {
                    ImVec2( 0.90f * size,  0.00f),
                    ImVec2(-0.60f * size, -0.72f * size),
                    ImVec2(-0.60f * size,  0.72f * size)
                };

                ImVec2 pts[3];

                for (int p = 0; p < 3; ++p)
                    pts[p] = ImVec2(
                        x + local[p].x * cs - local[p].y * sn,
                        y + local[p].x * sn + local[p].y * cs
                    );

                const float layerScale[3] = { 2.2f, 1.55f, 1.0f };
                const float layerAlpha[3] = { 0.22f, 0.40f, 1.0f };

                for (int layer = 0; layer < 3; ++layer) {
                    ImVec2 q[3];

                    for (int p = 0; p < 3; ++p)
                        q[p] = ImVec2(
                            x + (pts[p].x - x) * layerScale[layer],
                            y + (pts[p].y - y) * layerScale[layer]
                        );

                    drawList->AddTriangleFilled(
                        q[0],
                        q[1],
                        q[2],
                        withAlpha(IM_COL32(255, 255, 255, 255), triAlpha * layerAlpha[layer])
                    );
                }
            }
        }

        void drawVoxelBlock(ImDrawList* drawList, ImVec2 center, const VoxelMetrics& m, float alpha) {
            float s = this->Style.CubeSize;

            float loopTime = std::fmod(this->Time, this->Style.LoopDuration);

            float assembled = 0.0f;

            for (int n = 0; n < VoxelCount; ++n) {
                int i = n % 3;
                int j = (n / 3) % 3;
                int k = n / 9;

                float jitter = hash01(n * 13 + 5);

                float delay    = static_cast<float>(i + j + k) * 0.085f + jitter * 0.22f;
                float flySpan  = 0.45f;
                float progress = clamp01((loopTime - delay) / flySpan);

                float posX = static_cast<float>(i - k) * m.W * m.Gap;
                float posY = (static_cast<float>(i + k) * m.T - static_cast<float>(j) * m.D) * m.Gap;

                float cubeAlpha = alpha;
                float scale     = 1.0f;
                float offsetY   = 0.0f;

                if (progress < 1.0f) {
                    float ease  = easeOutCubic(progress);
                    offsetY    -= (1.0f - ease) * this->Style.CubeFallHeight;
                    scale       = 0.65f + 0.35f * ease;
                    cubeAlpha  *= clamp01(progress * 2.2f);
                } else {
                    assembled += 1.0f / static_cast<float>(VoxelCount);

                    scale = 1.0f + 0.016f * std::sin(this->Time * 3.0f + static_cast<float>(n) * 0.35f);
                }

                if (loopTime > this->Style.DissolveBegin) {
                    float fadeDelay = hash01(n * 17 + 11) * 0.3f;
                    float fadeSpan  = this->Style.LoopDuration - this->Style.DissolveBegin - 0.3f;
                    float fade      = clamp01((loopTime - this->Style.DissolveBegin - fadeDelay) / fadeSpan);

                    if (fade > 0.0f) {
                        float ease = easeInCubic(fade);
                        offsetY   -= ease * 260.0f;
                        cubeAlpha *= 1.0f - ease;
                    }
                }

                if (cubeAlpha <= 0.004f)
                    continue;

                float shine = 0.0f;

                if (loopTime > 1.35f && loopTime < this->Style.DissolveBegin) {
                    float sweep  = (loopTime - 1.35f) / (this->Style.DissolveBegin - 1.35f);
                    float sweepX = lerp(-3.4f * m.W, 3.4f * m.W, easeInOutSine(sweep));
                    float dist   = std::fabs(posX - sweepX);

                    shine = clamp01(1.0f - dist / (m.W * 1.6f));
                }

                drawCube(
                    drawList,
                    ImVec2(center.x + posX, center.y + posY + offsetY),
                    s,
                    this->Style.CubeHeightScale,
                    scale,
                    cubeAlpha,
                    shine > 0.35f ? this->Style.GrassTopShine : this->Style.GrassTopColor,
                    this->Style.DirtLeftColor,
                    this->Style.DirtRightColor,
                    shine
                );
            }

            float shadowAlpha = alpha * 0.35f * smoothstep(assembled * 1.6f);

            if (shadowAlpha > 0.004f) {
                float shadowScale = 0.6f + 0.4f * assembled;

                drawList->AddEllipseFilled(
                    ImVec2(center.x, center.y + m.BottomExtent + 0.16f * s),
                    ImVec2(3.0f * m.W * shadowScale, 0.78f * s * shadowScale),
                    withAlpha(IM_COL32(0, 0, 0, 200), shadowAlpha),
                    0.0f,
                    40
                );
            }
        }

        void drawProgressBar(ImDrawList* drawList, float width, float barY, float alpha) {
            float barW = std::min(this->Style.BarWidth, width * 0.5f);
            float barH = this->Style.BarHeight;
            float barX = (width - barW) * 0.5f;

            float radius = barH * 0.5f;
            float inset  = 3.0f;
            float innerX = barX + inset;
            float innerY = barY + inset;
            float innerW = barW - inset * 2.0f;
            float innerH = barH - inset * 2.0f;
            float innerR = innerH * 0.5f;

            drawList->AddRectFilled(
                ImVec2(barX - 2.0f, barY + 1.0f),
                ImVec2(barX + barW + 2.0f, barY + barH + 4.0f),
                withAlpha(IM_COL32(0, 0, 0, 255), alpha * 0.35f),
                radius + 2.0f
            );

            drawList->AddRectFilled(
                ImVec2(barX, barY),
                ImVec2(barX + barW, barY + barH),
                withAlpha(this->Style.BarTrackColor, alpha),
                radius
            );

            drawList->AddRectFilled(
                ImVec2(barX + 1.0f, barY + 1.0f),
                ImVec2(barX + barW - 1.0f, barY + barH * 0.5f),
                withAlpha(IM_COL32(0, 0, 0, 255), alpha * 0.28f),
                radius
            );

            bool determinate = this->ProgressKnown && this->DisplayProgress > 0.002f;

            float fillLeft  = innerX;
            float fillRight = innerX;

            if (determinate) {
                fillRight = innerX + innerW * clamp01(this->DisplayProgress);
            } else {
                float phase = std::fmod(this->Time / 1.9f, 1.0f);
                float ping  = phase < 0.5f ? phase * 2.0f : 2.0f - phase * 2.0f;

                fillRight = innerX + lerp(0.18f, 1.0f, easeInOutSine(ping)) * innerW;
                fillLeft  = fillRight - innerW * 0.26f;
            }

            if (fillRight - fillLeft > 0.5f) {
                ImU32 shine = this->Style.BarShineColor;

                drawList->PushClipRect(
                    ImVec2(fillLeft, innerY - 1.0f),
                    ImVec2(fillRight, innerY + innerH + 1.0f),
                    true
                );

                drawList->AddRectFilled(
                    ImVec2(fillLeft, innerY),
                    ImVec2(fillRight, innerY + innerH),
                    withAlpha(this->Style.BarFillColor, alpha),
                    innerR
                );

                drawList->AddRectFilled(
                    ImVec2(fillLeft, innerY + 0.5f),
                    ImVec2(fillRight, innerY + innerH * 0.52f),
                    withAlpha(this->Style.BarFillTopColor, alpha * 0.60f),
                    innerR * 0.85f
                );

                drawList->AddRectFilled(
                    ImVec2(fillLeft, innerY + innerH * 0.70f),
                    ImVec2(fillRight, innerY + innerH),
                    withAlpha(IM_COL32(0, 0, 0, 255), alpha * 0.12f),
                    innerR * 0.85f
                );

                float bandW     = innerH * 1.4f;
                float halfW     = bandW * 0.5f;
                float sweepSpan = innerW + bandW * 2.0f;
                float sweepX    = innerX + std::fmod(this->Time * 58.0f, sweepSpan) - bandW;
                ImU32 sheerMid  = withAlpha(shine, alpha * 0.22f);
                ImU32 sheerEnd  = withAlpha(shine, 0.0f);

                drawList->AddRectFilledMultiColor(
                    ImVec2(sweepX, innerY),
                    ImVec2(sweepX + halfW, innerY + innerH),
                    sheerEnd,
                    sheerMid,
                    sheerMid,
                    sheerEnd
                );
                drawList->AddRectFilledMultiColor(
                    ImVec2(sweepX + halfW, innerY),
                    ImVec2(sweepX + bandW, innerY + innerH),
                    sheerMid,
                    sheerEnd,
                    sheerEnd,
                    sheerMid
                );

                drawList->AddLine(
                    ImVec2(fillLeft, innerY + 0.5f),
                    ImVec2(fillRight, innerY + 0.5f),
                    withAlpha(IM_COL32(255, 255, 255, 255), alpha * 0.16f),
                    1.0f
                );

                float glowW = 56.0f;

                drawList->AddRectFilledMultiColor(
                    ImVec2(fillRight - glowW, innerY),
                    ImVec2(fillRight, innerY + innerH),
                    withAlpha(shine, 0.0f),
                    withAlpha(shine, alpha * 0.50f),
                    withAlpha(shine, alpha * 0.50f),
                    withAlpha(shine, 0.0f)
                );

                float breath = 0.72f + 0.28f * std::sin(this->Time * 3.2f);
                float headX  = fillRight - innerR;

                drawList->AddCircleFilled(
                    ImVec2(headX, innerY + innerH * 0.5f),
                    innerR,
                    withAlpha(shine, alpha * (0.45f + 0.40f * breath)),
                    24
                );

                drawList->PopClipRect();

                if (determinate) {
                    drawList->AddCircleFilled(
                        ImVec2(headX, innerY + innerH * 0.5f),
                        innerH * (1.35f + 0.35f * std::sin(this->Time * 3.2f)),
                        withAlpha(shine, alpha * 0.10f),
                        28
                    );
                }
            }

            drawList->AddRect(
                ImVec2(barX + 0.5f, barY + 0.5f),
                ImVec2(barX + barW - 0.5f, barY + barH - 0.5f),
                withAlpha(this->Style.BarFrameColor, alpha * 0.85f),
                radius,
                0,
                1.0f
            );

            if (determinate) {
                ImFont* percentFont = display::overlay::getArtistFont();

                if (percentFont != nullptr) {
                    char percentText[8];

                    std::snprintf(
                        percentText,
                        sizeof(percentText),
                        "%d%%",
                        static_cast<int>(std::lround(this->DisplayProgress * 100.0f + 0.5f))
                    );

                    float percentSize = 14.0f;

                    ImVec2 extent = percentFont->CalcTextSizeA(percentSize, FLT_MAX, 0.0f, percentText);

                    drawList->AddText(
                        percentFont,
                        percentSize,
                        ImVec2(barX + barW + 14.0f, barY + (barH - extent.y) * 0.5f),
                        withAlpha(this->Style.BarFillTopColor, alpha * 0.95f),
                        percentText
                    );
                }
            }
        }

        void drawText(ImDrawList* drawList, float width, float barY, float alpha) {
            ImFont* titleFont = display::overlay::getTitleFont();

            if (titleFont == nullptr)
                return;

            std::string title = this->Style.Title;

            int dotCount = static_cast<int>(this->Time * 2.0f) % 4;
            title.append(static_cast<size_t>(dotCount), '.');

            float titleSize = 30.0f;

            ImVec2 titleExtent = titleFont->CalcTextSizeA(titleSize, FLT_MAX, 0.0f, title.c_str());

            float titleX = (width - titleExtent.x) * 0.5f;

            drawList->AddText(
                titleFont,
                titleSize,
                ImVec2(titleX + 1.0f, barY + 25.0f),
                withAlpha(IM_COL32(0, 0, 0, 255), alpha * 0.35f),
                title.c_str()
            );

            drawList->AddText(
                titleFont,
                titleSize,
                ImVec2(titleX, barY + 24.0f),
                withAlpha(this->Style.TitleColor, alpha),
                title.c_str()
            );
        }

        void drawTips(ImDrawList* drawList, float width, float height, float alpha) {
            if (this->ShownTip.empty())
                return;

            ImFont* tipFont = display::overlay::getArtistFont();
            if (tipFont == nullptr)
                return;

            float tipSize = 16.0f;

            ImVec2 extent = tipFont->CalcTextSizeA(tipSize, FLT_MAX, 0.0f, this->ShownTip.c_str());

            float tipAlpha = alpha * easeOutCubic(this->TipAlpha);
            if (tipAlpha <= 0.004f)
                return;

            float x = (width - extent.x) * 0.5f;
            float y = height * 0.72f;

            drawList->AddText(
                tipFont,
                tipSize,
                ImVec2(x + 1.0f, y + 1.0f),
                withAlpha(IM_COL32(0, 0, 0, 255), tipAlpha * 0.45f),
                this->ShownTip.c_str()
            );

            drawList->AddText(
                tipFont,
                tipSize,
                ImVec2(x, y),
                withAlpha(this->Style.TipColor, tipAlpha),
                this->ShownTip.c_str()
            );

            float gap   = 16.0f;
            float lineW = 44.0f;
            float cy    = y + extent.y * 0.5f;

            ImU32 lineColor = withAlpha(this->Style.TipColor, tipAlpha * 0.32f);

            if (x - gap - lineW > 12.0f)
                drawList->AddLine(ImVec2(x - gap - lineW, cy), ImVec2(x - gap, cy), lineColor, 1.0f);

            if (x + extent.x + gap + lineW < width - 12.0f)
                drawList->AddLine(ImVec2(x + extent.x + gap, cy), ImVec2(x + extent.x + gap + lineW, cy), lineColor, 1.0f);
        }
    };

    LoadingAnimationRenderer::LoadingAnimationRenderer() : mImpl(std::make_unique<Impl>()) {}
    LoadingAnimationRenderer::~LoadingAnimationRenderer() = default;

    LoadingAnimationRenderer& LoadingAnimationRenderer::getInstance() {
        static LoadingAnimationRenderer instance;
        return instance;
    }

    void LoadingAnimationRenderer::setLogger(std::shared_ptr<ll::io::Logger> logger) {
        this->mImpl->Logger = std::move(logger);
    }

    void LoadingAnimationRenderer::setStyle(const LoadingAnimationStyle& style) {
        this->mImpl->Style = style;
    }

    void LoadingAnimationRenderer::reset() {
        this->mImpl->Visibility      = 0.0f;
        this->mImpl->Time            = 0.0f;
        this->mImpl->DisplayProgress = 0.0f;
        this->mImpl->ProgressKnown   = false;
        this->mImpl->TipAlpha        = 0.0f;

        this->mImpl->ShownTip.clear();
    }

    void LoadingAnimationRenderer::render(float deltaTime, float screenWidth, float screenHeight) {
        this->mImpl->advance(deltaTime);

        if (this->mImpl->Visibility <= 0.004f)
            return;

        float alpha = smoothstep(this->mImpl->Visibility);

        ImDrawList* drawList = ImGui::GetForegroundDrawList();

        this->mImpl->drawBackground(drawList, screenWidth, screenHeight, alpha);

        VoxelMetrics metrics = voxelMetrics(this->mImpl->Style.CubeSize, this->mImpl->Style.CubeHeightScale);

        ImVec2 blockCenter {
            screenWidth * 0.5f,
            screenHeight * 0.5f + (metrics.TopExtent - metrics.BottomExtent) * 0.5f - screenHeight * 0.02f
        };

        this->mImpl->drawVoxelBlock(drawList, blockCenter, metrics, alpha);

        float barY = blockCenter.y + metrics.BottomExtent + 30.0f;

        this->mImpl->drawProgressBar(drawList, screenWidth, barY, alpha);
        this->mImpl->drawText(drawList, screenWidth, barY, alpha);
        this->mImpl->drawTips(drawList, screenWidth, screenHeight, alpha);
    }
}
