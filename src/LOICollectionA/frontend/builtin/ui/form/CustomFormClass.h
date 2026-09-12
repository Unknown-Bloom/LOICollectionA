#pragma once

#include <memory>
#include <string>

#include <ll/api/ui/form/CustomForm.h>

#include "LOICollectionA/frontend/AST.h"

namespace LCCustomFormClass {
    struct LCCustomFormHandle : LOICollection::frontend::NativeHandle {
        std::unique_ptr<ll::ui::CustomForm> base;

        LOICollection::frontend::FunctionRefPtr show;
        std::string scriptId;

        void release() override {
            this->show.reset();
        }

        ~LCCustomFormHandle() override { this->release(); }
    };

    void registerClasses(const std::string& name);
}
