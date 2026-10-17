#pragma once

#include <string>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/ir/Mir.h"

namespace LOICollection::frontend::ir {
    LOICOLLECTION_A_NDAPI std::string abiFingerprint();

    LOICOLLECTION_A_NDAPI std::string layoutFingerprint(const MirChunk& chunk);
}
