#pragma once

#include <string>

#include <ll/api/ui/base/UIRawMessage.h>

#include "LOICollectionA/frontend/AST.h"

namespace UIRawMessageClass {
    struct UIRawMessageHandle : LOICollection::frontend::NativeHandle {
        ll::ui::UIRawMessage base;
    };

    void registerClasses(const std::string& name);
}
