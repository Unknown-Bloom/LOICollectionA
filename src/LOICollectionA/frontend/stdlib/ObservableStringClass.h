#pragma once

#include <memory>
#include <string>
#include <vector>

#include <ll/api/ui/base/Observable.h>

#include "LOICollectionA/frontend/AST.h"

namespace ObservableStringClass {
    struct ObservableStringHandle : LOICollection::frontend::NativeHandle {
        std::unique_ptr<ll::ui::ObservableString> base;
        using SubscriptionId = ll::ui::ObservableString::SubscriptionId;
        std::vector<SubscriptionId> subscriptions;

        void release() override {
            if (this->base)
                for (SubscriptionId id : this->subscriptions)
                    this->base->unsubscribe(id);

            this->subscriptions.clear();
        }

        ~ObservableStringHandle() override { this->release(); }
    };
    
    void registerClasses(const std::string& name);
}
