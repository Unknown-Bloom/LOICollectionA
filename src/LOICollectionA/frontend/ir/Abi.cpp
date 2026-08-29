#include "LOICollectionA/frontend/Callback.h"

#include "LOICollectionA/utils/core/Sha256.h"

#include "LOICollectionA/frontend/ir/Abi.h"

namespace LOICollection::frontend::ir {
    std::string abiFingerprint() {
        return utils::Sha256::compute(
            FunctionCall::getInstance().exportShape()
            + MacroCall::getInstance().exportShape()
            + ClassCall::getInstance().exportShape()
        );
    }
}
