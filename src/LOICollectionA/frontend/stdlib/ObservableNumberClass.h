#pragma once

#include <memory>
#include <string>
#include <vector>

#include <ll/api/ui/base/Observable.h>

#include "LOICollectionA/frontend/AST.h"

namespace ObservableNumberClass {
    struct ObservableNumberHandle : LOICollection::frontend::NativeHandle {
        std::unique_ptr<ll::ui::ObservableNumber> base;
        using SubscriptionId = ll::ui::ObservableNumber::SubscriptionId;
        std::vector<SubscriptionId> subscriptions;

        void release() override {
            if (this->base)
                for (SubscriptionId id : this->subscriptions)
                    this->base->unsubscribe(id);

            this->subscriptions.clear();
        }

        ~ObservableNumberHandle() override { this->release(); }
    };

    void registerClasses(const std::string& name);
}
