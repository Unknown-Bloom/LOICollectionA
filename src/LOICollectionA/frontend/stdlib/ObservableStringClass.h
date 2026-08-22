#pragma once

#include <memory>
#include <string>

#include <ll/api/ui/base/Observable.h>

#include "LOICollectionA/frontend/AST.h"

namespace ObservableStringClass {
    struct ObservableStringHandle : LOICollection::frontend::NativeHandle {
        std::unique_ptr<ll::ui::ObservableString> base;
    };
    
    void registerClasses(const std::string& name);
}
