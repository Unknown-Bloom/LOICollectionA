#include <cstdint>
#include <vector>

#include <windows.h>
#include <d3d11.h>

#include <imgui.h>

#include "LOICollectionA/include/client/display/overlay/Overlay.h"
#include "LOICollectionA/include/client/display/overlay/OverlayTexture.h"

namespace LOICollection::client::display::overlay {
    bool createTexture(
        const std::vector<uint8_t>& pixels,
        uint32_t                    width,
        uint32_t                    height,
        ImTextureID&                texture
    ) {
        texture = 0;

        if (pixels.empty() || width == 0 || height == 0)
            return false;

        ID3D11Device* device = getDevice();

        if (device == nullptr)
            return false;

        D3D11_TEXTURE2D_DESC description {};
        description.Width            = width;
        description.Height           = height;
        description.MipLevels        = 1;
        description.ArraySize        = 1;
        description.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage            = D3D11_USAGE_DEFAULT;
        description.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initialData {};
        initialData.pSysMem     = pixels.data();
        initialData.SysMemPitch = width * 4;

        ID3D11Texture2D* resource { nullptr };

        if (FAILED(device->CreateTexture2D(&description, &initialData, &resource)))
            return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription {};
        viewDescription.Format                    = description.Format;
        viewDescription.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        viewDescription.Texture2D.MipLevels       = 1;
        viewDescription.Texture2D.MostDetailedMip = 0;

        ID3D11ShaderResourceView* view { nullptr };
        HRESULT                   result = device->CreateShaderResourceView(resource, &viewDescription, &view);

        resource->Release();

        if (FAILED(result) || view == nullptr)
            return false;

        texture = reinterpret_cast<ImTextureID>(view);

        return true;
    }

    void destroyTexture(ImTextureID& texture) {
        if (texture == 0)
            return;

        reinterpret_cast<ID3D11ShaderResourceView*>(texture)->Release();

        texture = 0;
    }
}
