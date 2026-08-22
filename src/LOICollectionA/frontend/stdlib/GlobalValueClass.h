#pragma once

#include <string>

#include "LOICollectionA/frontend/AST.h"

namespace GlobalValueClass {
    struct GlobalValueHandle : LOICollection::frontend::NativeHandle {
        LOICollection::frontend::ValueNode::ValueType value;
    };

    void registerClasses(const std::string& name);
}
