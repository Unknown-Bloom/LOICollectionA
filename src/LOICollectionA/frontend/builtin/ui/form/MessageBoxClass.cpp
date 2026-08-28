#include <any>
#include <memory>
#include <string>

#include <ll/api/Expected.h>
#include <ll/api/ui/form/MessageBox.h>

#include <mc/world/actor/player/Player.h>

#include "LOICollectionA/include/form/GUIManager.h"

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/Context.h"

#include "LOICollectionA/frontend/builtin/ui/form/CustomFormOptionsClass.h"

#include "LOICollectionA/frontend/builtin/ui/form/MessageBoxClass.h"

using namespace LOICollection::frontend;
using namespace CustomFormOptionsClass;

namespace MessageBoxClass {
    ll::Expected<ObjectRef> makeMessageBox(const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        auto title = toTextValue(args[1]);
        if (!title)
            return ll::Unexpected(title.error());

        std::reference_wrapper<Player> player = std::any_cast<std::reference_wrapper<Player>>(placeholders.at(0));

        auto handle = std::make_shared<MessageBoxHandle>();
        handle->scriptId = Context::scriptIdOf(placeholders);
        handle->base = std::make_unique<ll::ui::MessageBox>(player, *title);

        LOICollection::form::GUIManager::getInstance().registerMessageBoxUI(std::get<std::string>(args[0]), handle, player);

        auto obj = std::make_shared<Object>();
        obj->className = "MessageBox";
        obj->classIndex = -1;
        obj->native = handle;

        return obj;
    }

    ll::Expected<TypedValue> body(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* box = static_cast<MessageBoxHandle*>(self->native.get())->base.get();

        auto value = toTextValue(args[0]);
        if (!value)
            return ll::Unexpected(value.error());

        box->body(*value);
        return self;
    }

    ll::Expected<TypedValue> button1(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* box = static_cast<MessageBoxHandle*>(self->native.get())->base.get();

        auto label = toTextValue(args[0]);
        if (!label)
            return ll::Unexpected(label.error());

        if (args.size() == 2) {
            auto tooltip = toTextValue(args[1]);
            if (!tooltip)
                return ll::Unexpected(tooltip.error());

            box->button1(*label, *tooltip);
        } else {
            box->button1(*label);
        }

        return self;
    }

    ll::Expected<TypedValue> button2(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* box = static_cast<MessageBoxHandle*>(self->native.get())->base.get();

        auto label = toTextValue(args[0]);
        if (!label)
            return ll::Unexpected(label.error());

        if (args.size() == 2) {
            auto tooltip = toTextValue(args[1]);
            if (!tooltip)
                return ll::Unexpected(tooltip.error());

            box->button2(*label, *tooltip);
        } else {
            box->button2(*label);
        }

        return self;
    }

    ll::Expected<TypedValue> show(const ObjectRef& self, const CallbackTypeValues& args) {
        if (args.empty())
            return self;

        auto callback = std::get<FunctionRefPtr>(args[0]);
        if (callback->argCount != 1)
            return ll::makeStringError("show callback must take exactly one parameter");

        static_cast<MessageBoxHandle*>(self->native.get())->show = callback;

        return self;
    }

    ll::Expected<TypedValue> close(const ObjectRef& self, const CallbackTypeValues&) {
        auto result = static_cast<MessageBoxHandle*>(self->native.get())->base->close();
        if (!result)
            return ll::Unexpected(result.error());

        return self;
    }

    ll::Expected<TypedValue> isShowing(const ObjectRef& self, const CallbackTypeValues&) {
        return static_cast<MessageBoxHandle*>(self->native.get())->base->isShowing();
    }

    void registerClasses(const std::string&) {
        ClassCall& classes = ClassCall::getInstance();

        classes.registerClass("MessageBoxResult", { "closeReason", "selection" });
        classes.registerField("MessageBoxResult", "closeReason", 0);
        classes.registerField("MessageBoxResult", "selection", std::monostate{});

        classes.registerClass("MessageBox", {});
        classes.registerConstructor("MessageBox", makeMessageBox, { ParamType::STRING, ParamType::STRING });
        classes.registerConstructor("MessageBox", makeMessageBox, { ParamType::STRING, ParamType::OBJECT });

        classes.registerMethod("MessageBox", "body", body, { ParamType::STRING });
        classes.registerMethod("MessageBox", "body", body, { ParamType::OBJECT });

        classes.registerMethod("MessageBox", "button1", button1, { ParamType::STRING });
        classes.registerMethod("MessageBox", "button1", button1, { ParamType::OBJECT });
        classes.registerMethod("MessageBox", "button1", button1,
            { ParamType::STRING, ParamType::STRING });
        classes.registerMethod("MessageBox", "button1", button1,
            { ParamType::STRING, ParamType::OBJECT });
        classes.registerMethod("MessageBox", "button1", button1,
            { ParamType::OBJECT, ParamType::STRING });
        classes.registerMethod("MessageBox", "button1", button1,
            { ParamType::OBJECT, ParamType::OBJECT });

        classes.registerMethod("MessageBox", "button2", button2, { ParamType::STRING });
        classes.registerMethod("MessageBox", "button2", button2, { ParamType::OBJECT });
        classes.registerMethod("MessageBox", "button2", button2,
            { ParamType::STRING, ParamType::STRING });
        classes.registerMethod("MessageBox", "button2", button2,
            { ParamType::STRING, ParamType::OBJECT });
        classes.registerMethod("MessageBox", "button2", button2,
            { ParamType::OBJECT, ParamType::STRING });
        classes.registerMethod("MessageBox", "button2", button2,
            { ParamType::OBJECT, ParamType::OBJECT });

        classes.registerMethod("MessageBox", "show", show, {});
        classes.registerMethod("MessageBox", "show", show, { ParamType::FUNCTION });

        classes.registerMethod("MessageBox", "close", close, {});
        classes.registerMethod("MessageBox", "isShowing", isShowing, {});
    }
}

REGISTER_CALLBACK(MessageBox, MessageBoxClass::registerClasses)
