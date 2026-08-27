#pragma once

#include <memory>
#include <string>

#include <ll/api/ui/form/MessageBox.h>

#include "LOICollectionA/frontend/AST.h"

namespace MessageBoxClass {
    struct MessageBoxHandle : LOICollection::frontend::NativeHandle {
        std::unique_ptr<ll::ui::MessageBox> base;

        LOICollection::frontend::FunctionRefPtr show;
        std::string scriptId;
    };

    void registerClasses(const std::string& name);
}
