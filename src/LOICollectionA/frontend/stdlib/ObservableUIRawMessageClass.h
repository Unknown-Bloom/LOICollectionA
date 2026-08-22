#pragma once

#include <memory>
#include <string>

#include <ll/api/ui/base/Observable.h>

#include "LOICollectionA/frontend/AST.h"

namespace ObservableUIRawMessageClass {
    struct ObservableUIRawMessageHandle : LOICollection::frontend::NativeHandle {
        std::unique_ptr<ll::ui::ObservableUIRawMessage> base;
    };

    void registerClasses(const std::string& name);
}
