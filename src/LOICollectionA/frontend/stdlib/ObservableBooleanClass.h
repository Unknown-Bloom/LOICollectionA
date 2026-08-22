#pragma once

#include <memory>
#include <string>

#include <ll/api/ui/base/Observable.h>

#include "LOICollectionA/frontend/AST.h"

namespace ObservableBooleanClass {
    struct ObservableBooleanHandle : LOICollection::frontend::NativeHandle {
        std::unique_ptr<ll::ui::ObservableBoolean> base;
    };

    void registerClasses(const std::string& name);
}
