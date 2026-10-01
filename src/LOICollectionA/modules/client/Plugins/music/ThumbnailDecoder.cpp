#include <cstdint>
#include <vector>
#include <algorithm>

#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>

#include "LOICollectionA/include/client/Plugins/music/ThumbnailDecoder.h"
#include "LOICollectionA/include/client/Plugins/music/NowPlaying.h"

using Microsoft::WRL::ComPtr;

namespace LOICollection::client::Plugins::music {
    namespace {
        struct ComInitializer {
            ComInitializer() {
                static_cast<void>(::CoInitializeEx(nullptr, COINIT_MULTITHREADED));
            }
        };

        IWICImagingFactory* imagingFactory() {
            static ComInitializer              initializer;
            static ComPtr<IWICImagingFactory> factory = []() -> ComPtr<IWICImagingFactory> {
                ComPtr<IWICImagingFactory> instance;

                static_cast<void>(::CoCreateInstance(
                    CLSID_WICImagingFactory2,
                    nullptr,
                    CLSCTX_INPROC_SERVER,
                    IID_PPV_ARGS(instance.GetAddressOf())
                ));

                return instance;
            }();

            return factory.Get();
        }
    }

    bool decodeThumbnail(const std::vector<uint8_t>& source, uint32_t maxEdge, NowPlayingArtwork& artwork) {
        artwork = NowPlayingArtwork{};

        if (source.empty() || maxEdge == 0)
            return false;

        IWICImagingFactory* factory = imagingFactory();
        if (factory == nullptr)
            return false;

        ComPtr<IWICStream> stream;
        if (FAILED(factory->CreateStream(stream.GetAddressOf())))
            return false;

        if (FAILED(stream->InitializeFromMemory(
                const_cast<BYTE*>(source.data()),
                static_cast<DWORD>(source.size())
            )))
            return false;

        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(factory->CreateDecoderFromStream(
                stream.Get(),
                nullptr,
                WICDecodeMetadataCacheOnDemand,
                decoder.GetAddressOf()
            )))
            return false;

        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, frame.GetAddressOf())))
            return false;

        UINT width  = 0;
        UINT height = 0;
        if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0)
            return false;

        double scale = std::min(1.0, static_cast<double>(maxEdge) / static_cast<double>(std::max(width, height)));

        UINT targetWidth  = static_cast<UINT>(std::max(1.0, width * scale));
        UINT targetHeight = static_cast<UINT>(std::max(1.0, height * scale));

        targetWidth  = std::min(targetWidth + targetWidth % 4, static_cast<UINT>(256));
        targetHeight = std::min(targetHeight + targetHeight % 4, static_cast<UINT>(256));

        ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf())))
            return false;

        if (FAILED(converter->Initialize(
                frame.Get(),
                GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone,
                nullptr,
                0.0,
                WICBitmapPaletteTypeCustom
            )))
            return false;

        ComPtr<IWICBitmapScaler> scaler;
        if (FAILED(factory->CreateBitmapScaler(scaler.GetAddressOf())))
            return false;

        if (FAILED(scaler->Initialize(converter.Get(), targetWidth, targetHeight, WICBitmapInterpolationModeFant)))
            return false;

        const UINT stride = targetWidth * 4;

        std::vector<uint8_t> pixels(static_cast<size_t>(stride) * targetHeight);

        if (FAILED(scaler->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data())))
            return false;

        // WIC 的 32bppRGBA 实际给出 BGRA 字节序，这里换回真正的 RGBA。
        for (size_t index = 0; index + 3 < pixels.size(); index += 4)
            std::swap(pixels[index], pixels[index + 2]);

        artwork.Width  = targetWidth;
        artwork.Height = targetHeight;
        artwork.Pixels = std::move(pixels);

        return true;
    }
}
