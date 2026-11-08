#pragma once

#include <string>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/ir/Lir.h"

namespace LOICollection::frontend::ir {
    LOICOLLECTION_A_NDAPI std::string abiFingerprint();

    LOICOLLECTION_A_NDAPI std::string layoutFingerprint(const Chunk& chunk);
}
