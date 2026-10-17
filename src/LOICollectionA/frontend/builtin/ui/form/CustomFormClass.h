#pragma once

#include <memory>
#include <string>

#include <ll/api/ui/form/CustomForm.h>

#include "LOICollectionA/frontend/AST.h"

namespace CustomFormClass {
    struct CustomFormHandle : LOICollection::frontend::NativeHandle {
        std::unique_ptr<ll::ui::CustomForm> base;

        LOICollection::frontend::FunctionRefPtr show;
        std::string scriptId;

        void release() override {
            this->show.reset();
        }

        void trace(LOICollection::frontend::ValueVisitor& visitor) override {
            visitor.visitClosure(this->show);
        }

        ~CustomFormHandle() override { this->release(); }
    };

    void registerClasses(const std::string& name);
}
