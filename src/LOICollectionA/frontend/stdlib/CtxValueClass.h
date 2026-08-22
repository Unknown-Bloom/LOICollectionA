#pragma once

#include <string>

#include "LOICollectionA/frontend/AST.h"

namespace CtxValueClass {
    struct CtxValueHandle : LOICollection::frontend::NativeHandle {
        LOICollection::frontend::ValueNode::ValueType value;
    };

    void registerClasses(const std::string& name);
}
