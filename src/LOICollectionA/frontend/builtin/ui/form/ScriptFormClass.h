#pragma once

#include <functional>
#include <memory>
#include <string>

#include <ll/api/ui/form/CustomForm.h>
#include <ll/api/ui/form/MessageBox.h>

#include "LOICollectionA/frontend/AST.h"
#include "LOICollectionA/frontend/Callback.h"

class Player;

namespace ScriptFormClass {
    struct ScriptFormHandle : LOICollection::frontend::NativeHandle {
        std::unique_ptr<ll::ui::CustomForm> base;
        std::unique_ptr<ll::ui::MessageBox> box;

        LOICollection::frontend::FunctionRefPtr show;

        std::function<LOICollection::frontend::ObjectRef()> makeResult;
        std::function<void(Player&)> onClosed;
        std::function<void(const ll::ui::MessageBox::Result&)> onBoxResult;

        std::string scriptId;
        bool pendingSubflow = false;
    };
}
