#pragma once

#include <cstdint>
#include <vector>

#include <imgui.h>

#include "LOICollectionA/base/Macro.h"

namespace LOICollection::client::display::overlay {
    LOICOLLECTION_A_NDAPI bool createTexture(
        const std::vector<uint8_t>& pixels,
        uint32_t                    width,
        uint32_t                    height,
        ImTextureID&                texture
    );

    LOICOLLECTION_A_API void destroyTexture(ImTextureID& texture);
}
