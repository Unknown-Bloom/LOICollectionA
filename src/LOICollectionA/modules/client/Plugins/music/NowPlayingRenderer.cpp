#include <string>
#include <memory>
#include <mutex>
#include <algorithm>

#include <imgui.h>

#include <ll/api/io/Logger.h>

#include "LOICollectionA/include/client/display/overlay/Overlay.h"
#include "LOICollectionA/include/client/display/overlay/OverlayTexture.h"

#include "LOICollectionA/include/client/Plugins/music/MusicText.h"
#include "LOICollectionA/include/client/Plugins/music/NowPlayingToast.h"
#include "LOICollectionA/include/client/Plugins/music/NowPlayingRenderer.h"

namespace LOICollection::client::Plugins::music {
    std::string shortAppName(std::string_view sourceAppId) {
        if (sourceAppId.empty())
            return {};

        size_t start = sourceAppId.find_last_of("\\/");

        std::string_view name = start == std::string_view::npos ? sourceAppId : sourceAppId.substr(start + 1);

        if (name.empty())
            name = sourceAppId;

        return std::string(name);
    }

    struct NowPlayingRenderer::Impl {
        NowPlayingToast& Toast;

        NowPlayingLayout Layout;

        mutable std::mutex PendingMutex;

        NowPlayingTrack PendingTrack;
        float           HoldDuration { NowPlayingToast::DefaultHoldDuration };
        bool            HasPending { false };

        NowPlayingTrack LastTrack;

        ImTextureID CoverTexture { 0 };
        std::string OwnedArtworkKey;

        bool  HasCardRect { false };
        float CardX0 { 0.0f };
        float CardY0 { 0.0f };
        float CardX1 { 0.0f };
        float CardY1 { 0.0f };

        audio::SpectrumFrame Spectrum;

        bool LoggedFirstDraw { false };

        std::shared_ptr<ll::io::Logger> Logger;

        explicit Impl(NowPlayingToast& value) : Toast(value) {}

        void releaseCover() {
            display::overlay::destroyTexture(this->CoverTexture);
            this->OwnedArtworkKey.clear();
        }
    };

    NowPlayingRenderer::NowPlayingRenderer(NowPlayingToast& toast) : mImpl(std::make_unique<Impl>(toast)) {}
    NowPlayingRenderer::~NowPlayingRenderer() = default;

    void NowPlayingRenderer::setLogger(std::shared_ptr<ll::io::Logger> logger) {
        this->mImpl->Logger = std::move(logger);
    }

    void NowPlayingRenderer::setSpectrum(const audio::SpectrumFrame& spectrum) {
        this->mImpl->Spectrum = spectrum;
    }

    void NowPlayingRenderer::setLayout(const NowPlayingLayout& layout) {
        this->mImpl->Layout = layout;
    }

    const NowPlayingLayout& NowPlayingRenderer::getLayout() const {
        return this->mImpl->Layout;
    }

    void NowPlayingRenderer::enqueue(const NowPlayingTrack& track, float holdDuration) {
        std::scoped_lock lock(this->mImpl->PendingMutex);

        this->mImpl->PendingTrack = track;
        this->mImpl->HoldDuration = holdDuration;
        this->mImpl->HasPending   = true;
    }

    void NowPlayingRenderer::replay() {
        std::scoped_lock lock(this->mImpl->PendingMutex);

        if (this->mImpl->LastTrack.empty())
            return;

        this->mImpl->PendingTrack = this->mImpl->LastTrack;
        this->mImpl->HasPending   = true;
    }

    void NowPlayingRenderer::dismiss() {
        this->mImpl->Toast.dismiss();
    }

    void NowPlayingRenderer::update(float deltaTime) {
        this->mImpl->Toast.update(std::clamp(deltaTime, 0.0f, this->mImpl->Layout.MaxFrameDeltaTime));

        NowPlayingTrack track;
        float           holdDuration { NowPlayingToast::DefaultHoldDuration };

        {
            std::scoped_lock lock(this->mImpl->PendingMutex);

            if (!this->mImpl->HasPending)
                return;

            track        = this->mImpl->PendingTrack;
            holdDuration = this->mImpl->HoldDuration;
        }

        std::string const artworkKey = track.ArtworkKey();

        if (artworkKey != this->mImpl->OwnedArtworkKey) {
            this->mImpl->releaseCover();

            if (track.hasArtwork()) {
                ImTextureID texture { 0 };

                if (display::overlay::createTexture(track.Artwork.Pixels, track.Artwork.Width, track.Artwork.Height, texture)) {
                    this->mImpl->CoverTexture    = texture;
                    this->mImpl->OwnedArtworkKey = artworkKey;

                    if (this->mImpl->Logger)
                        this->mImpl->Logger->debug(
                            "MusicPlugin - cover texture {}x{} hash={:016x}",
                            track.Artwork.Width,
                            track.Artwork.Height,
                            track.Artwork.contentHash()
                        );
                } else if (this->mImpl->Logger) {
                    this->mImpl->Logger->warn("MusicPlugin - failed to upload cover for '{}'", track.Title);
                }
            }
        }

        bool hasCover = this->mImpl->CoverTexture != 0 && artworkKey == this->mImpl->OwnedArtworkKey;

        this->mImpl->Toast.setArtworkAvailable(hasCover);
        this->mImpl->Toast.show(track, holdDuration);
        this->mImpl->LastTrack = track;

        if (this->mImpl->Logger) {
            this->mImpl->Logger->info(
                "MusicPlugin - now playing: {} - {}{}{}",
                track.Title,
                track.Artist,
                track.Album.empty() ? "" : " - ",
                track.Album
            );

            this->mImpl->Logger->debug("MusicPlugin - toast shown, cover={}", hasCover ? "yes" : "no");
        }

        std::scoped_lock lock(this->mImpl->PendingMutex);

        this->mImpl->PendingTrack = NowPlayingTrack{};
        this->mImpl->HasPending   = false;
    }

    void NowPlayingRenderer::draw(ImDrawList& drawList, float screenWidth, float screenHeight) {
        NowPlayingToast&            toast = this->mImpl->Toast;
        const NowPlayingToastStyle& style = toast.getStyle();
        const NowPlayingLayout&     layout = this->mImpl->Layout;

        NowPlayingCard card = toast.getCard([&style](std::string_view text, float fontSize) -> float {
            ImFont* font = fontSize >= style.TitleFontSize ? display::overlay::getTitleFont() : display::overlay::getArtistFont();

            if (font == nullptr)
                font = ImGui::GetFont();

            if (font == nullptr)
                return static_cast<float>(text.size()) * fontSize * 0.5f;

            return font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.data(), text.data() + text.size()).x;
        });

        float progress = std::clamp(toast.getProgress(), 0.0f, 1.0f);
        float eased    = std::clamp(toast.getEasedProgress(), 0.0f, 1.0f);

        float alpha  = progress;
        float offset = (1.0f - eased) * style.SlideDistance;

        float x1 = screenWidth - layout.RightMargin + offset;
        float x0 = x1 - card.Width;
        float y0 = screenHeight - layout.BottomMargin - card.Height;
        float y1 = y0 + card.Height;

        this->mImpl->HasCardRect = true;
        this->mImpl->CardX0      = x0;
        this->mImpl->CardY0      = y0;
        this->mImpl->CardX1      = x1;
        this->mImpl->CardY1      = y1;

        auto withAlpha = [alpha](ImU32 color) -> ImU32 {
            ImU32 base  = (color >> IM_COL32_A_SHIFT) & 0xFF;
            ImU32 faded = static_cast<ImU32>(static_cast<float>(base) * alpha);

            return (color & ~IM_COL32_A_MASK) | (faded << IM_COL32_A_SHIFT);
        };

        bool hovered = ImGui::IsMouseHoveringRect(ImVec2(x0, y0), ImVec2(x1, y1), false);

        drawList.AddRectFilled(
            ImVec2(x0 + layout.ShadowOffsetX, y0 + layout.ShadowOffsetY),
            ImVec2(x1 + layout.ShadowOffsetX, y1 + layout.ShadowOffsetY),
            withAlpha(IM_COL32(0, 0, 0, 110)),
            layout.Rounding
        );

        drawList.AddRectFilled(
            ImVec2(x0, y0),
            ImVec2(x1, y1),
            withAlpha(hovered ? IM_COL32(28, 28, 34, 245) : IM_COL32(18, 18, 22, 240)),
            layout.Rounding
        );

        drawList.AddRectFilled(
            ImVec2(x0, y0 + layout.Rounding),
            ImVec2(x0 + layout.AccentWidth, y1 - layout.Rounding),
            withAlpha(IM_COL32(255, 58, 92, 255)),
            layout.AccentWidth * 0.5f
        );

        drawList.AddRect(
            ImVec2(x0, y0),
            ImVec2(x1, y1),
            withAlpha(hovered ? IM_COL32(255, 255, 255, 90) : IM_COL32(255, 255, 255, 40)),
            layout.Rounding
        );

        float textX0 = x0 + layout.AccentWidth + style.Padding;

        if (card.HasCover && this->mImpl->CoverTexture != 0) {
            float coverY0 = y0 + (card.Height - card.CoverSize) * 0.5f;

            drawList.AddImage(
                this->mImpl->CoverTexture,
                ImVec2(textX0, coverY0),
                ImVec2(textX0 + card.CoverSize, coverY0 + card.CoverSize)
            );

            textX0 += card.CoverSize + style.Padding;
        }

        float topPadding       = style.Padding * layout.ContentTopRatio;
        float visualizerHeight = style.VisualizerHeight > 0.0f ? style.VisualizerHeight : 0.0f;
        float visualizerGap    = visualizerHeight > 0.0f ? style.Padding * layout.VisualizerGapRatio : 0.0f;
        float bottomMargin     = visualizerHeight > 0.0f ? style.Padding * layout.VisualizerBottomRatio : style.Padding;

        float contentHeight = card.Height - topPadding - visualizerHeight - visualizerGap - bottomMargin;

        float textHeight = style.TitleFontSize + style.LineSpacing + style.ArtistFontSize;

        if (!card.Album.empty())
            textHeight += style.LineSpacing + style.AlbumFontSize;

        float textY0 = y0 + topPadding + std::max(0.0f, (contentHeight - textHeight) * 0.5f);

        ImFont* titleFont  = display::overlay::getTitleFont();
        ImFont* artistFont = display::overlay::getArtistFont();

        if (titleFont == nullptr)
            titleFont = ImGui::GetFont();

        if (artistFont == nullptr)
            artistFont = titleFont;

        float cursorY = textY0;

        drawList.AddText(
            titleFont,
            style.TitleFontSize,
            ImVec2(textX0, cursorY),
            withAlpha(IM_COL32(255, 255, 255, 255)),
            card.Title.c_str()
        );

        cursorY += style.TitleFontSize + style.LineSpacing;

        drawList.AddText(
            artistFont,
            style.ArtistFontSize,
            ImVec2(textX0, cursorY),
            withAlpha(IM_COL32(185, 185, 195, 255)),
            card.Subtitle.c_str()
        );

        cursorY += style.ArtistFontSize + style.LineSpacing;

        if (!card.Album.empty()) {
            drawList.AddText(
                artistFont,
                style.AlbumFontSize,
                ImVec2(textX0, cursorY),
                withAlpha(IM_COL32(140, 140, 152, 255)),
                card.Album.c_str()
            );
        }

        if (hovered) {
            std::string detail = shortAppName(toast.getTrack().SourceAppId);

            if (!detail.empty()) {
                ImVec2 textSize = ImGui::CalcTextSize(detail.c_str());

                float detailWidth = textSize.x + style.Padding * 2.0f;
                float detailX0    = x1 - detailWidth;
                float detailY1    = y0 - layout.DetailGap;
                float detailY0    = detailY1 - (style.AlbumFontSize + style.Padding);

                drawList.AddRectFilled(
                    ImVec2(detailX0, detailY0),
                    ImVec2(detailX0 + detailWidth, detailY1),
                    withAlpha(IM_COL32(10, 10, 14, 235)),
                    layout.Rounding * layout.HoverRoundingRatio
                );

                drawList.AddText(
                    artistFont,
                    style.AlbumFontSize,
                    ImVec2(detailX0 + style.Padding, detailY0 + style.Padding * layout.DetailBottomPaddingRatio),
                    withAlpha(IM_COL32(210, 210, 220, 255)),
                    detail.c_str()
                );
            }
        }

        if (!this->mImpl->LoggedFirstDraw && this->mImpl->Logger) {
            this->mImpl->LoggedFirstDraw = true;

            this->mImpl->Logger->debug(
                "MusicPlugin - toast rendered at {}x{}, card {}x{}",
                screenWidth,
                screenHeight,
                card.Width,
                card.Height
            );
        }

        this->drawVisualizer(drawList, x0, x1, y1, style, withAlpha);
    }

    void NowPlayingRenderer::drawVisualizer(
        ImDrawList&                 drawList,
        float                       x0,
        float                       x1,
        float                       y1,
        const NowPlayingToastStyle& style,
        const AlphaFn&              withAlpha
    ) {
        auto const& spectrum = this->mImpl->Spectrum;
        auto const& layout   = this->mImpl->Layout;

        if (style.VisualizerHeight <= 0.0f || spectrum.Bands.empty())
            return;

        const std::size_t bandCount = spectrum.Bands.size();

        float left   = x0 + layout.AccentWidth + style.Padding;
        float right  = x1 - style.Padding;
        float bottom = y1 - style.Padding * layout.VisualizerBottomRatio;

        float totalWidth = right - left;

        if (totalWidth <= 4.0f)
            return;

        float barGap = style.VisualizerBarGap;

        float barWidth = (totalWidth - barGap * static_cast<float>(bandCount - 1)) / static_cast<float>(bandCount);

        if (barWidth < layout.MinimumBarWidth) {
            barWidth = totalWidth / static_cast<float>(bandCount);
            barGap   = 0.0f;
        }

        for (std::size_t band = 0; band < bandCount; ++band) {
            float value = std::clamp(spectrum.Bands[band], 0.0f, 1.0f);

            float height = std::max(style.VisualizerHeight * layout.BarHeightRatio, value * style.VisualizerHeight);

            float barX0 = left + static_cast<float>(band) * (barWidth + barGap);
            float barX1 = barX0 + barWidth;
            float barY0 = bottom - height;

            float ratio = bandCount > 1 ? static_cast<float>(band) / static_cast<float>(bandCount - 1) : 0.0f;

            ImU32 color = withAlpha(IM_COL32(
                static_cast<int>(255.0f - 90.0f * ratio),
                static_cast<int>(58.0f + 70.0f * ratio),
                static_cast<int>(92.0f + 150.0f * ratio),
                235
            ));

            drawList.AddRectFilled(
                ImVec2(barX0, barY0),
                ImVec2(barX1, bottom),
                color,
                barWidth * layout.BarRoundingRatio
            );

            if (height > layout.BarHighlightHeight + 1.0f) {
                drawList.AddRectFilled(
                    ImVec2(barX0, barY0),
                    ImVec2(barX1, barY0 + std::min(layout.BarHighlightHeight, height)),
                    withAlpha(IM_COL32(255, 255, 255, 70)),
                    barWidth * layout.BarRoundingRatio
                );
            }
        }
    }

    void NowPlayingRenderer::render(float deltaTime, float screenWidth, float screenHeight) {
        this->update(deltaTime);

        if (!this->mImpl->Toast.isVisible()) {
            this->mImpl->HasCardRect = false;

            return;
        }

        ImDrawList* drawList = ImGui::GetForegroundDrawList();

        if (drawList == nullptr)
            return;

        this->draw(*drawList, screenWidth, screenHeight);

        if (this->mImpl->HasCardRect && ImGui::IsMouseHoveringRect(
                ImVec2(this->mImpl->CardX0, this->mImpl->CardY0),
                ImVec2(this->mImpl->CardX1, this->mImpl->CardY1),
                false
            )
            && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            this->mImpl->Toast.dismiss();
        }
    }

    void NowPlayingRenderer::releaseResources() {
        this->mImpl->releaseCover();
        this->mImpl->Toast.setArtworkAvailable(false);
    }
}
