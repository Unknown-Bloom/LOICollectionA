#pragma once

#include <memory>
#include <string>

#include <ll/api/ui/base/Observable.h>

#include "LOICollectionA/frontend/AST.h"

namespace ObservableNumberClass {
    struct ObservableNumberHandle : LOICollection::frontend::NativeHandle {
        std::unique_ptr<ll::ui::ObservableNumber> base;
    };

    void registerClasses(const std::string& name);
}
