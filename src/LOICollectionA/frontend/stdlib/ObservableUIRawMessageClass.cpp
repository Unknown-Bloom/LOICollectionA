#include <memory>
#include <string>

#include <ll/api/Expected.h>
#include <ll/api/io/Logger.h>
#include <ll/api/io/LoggerRegistry.h>
#include <ll/api/ui/base/Observable.h>
#include <ll/api/ui/base/UIRawMessage.h>

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"

#include "LOICollectionA/frontend/ir/VM.h"

#include "LOICollectionA/frontend/stdlib/UIRawMessageClass.h"

#include "LOICollectionA/frontend/stdlib/ObservableUIRawMessageClass.h"

using namespace LOICollection::frontend;

namespace ObservableUIRawMessageClass {
    ll::Expected<ObjectRef> makeObservableUIRawMessage(const CallbackTypeValues& args) {
        auto handle = std::make_shared<ObservableUIRawMessageHandle>();

        if (const auto* current = std::get_if<ObjectRef>(&args[0]); current && (*current)->className == "UIRawMessage") {
            handle->base = std::make_unique<ll::ui::ObservableUIRawMessage>(
                static_cast<UIRawMessageClass::UIRawMessageHandle*>((*current)->native.get())->base, ll::ui::ObservableOptions{ std::get<bool>(args[1]) }
            );
        } else {
            return ll::makeStringError("makeObservableUIRawMessage only needs one UIRawMessage parameter");
        }

        auto obj = std::make_shared<Object>();
        obj->className = "ObservableUIRawMessage";
        obj->classIndex = -1;
        obj->native = handle;

        return obj;
    }

    bool isClientWritable(const ObjectRef& self, const CallbackTypeValues&) {
        return static_cast<ObservableUIRawMessageHandle*>(self->native.get())->base->isClientWritable();
    }

    ObjectRef getData(const ObjectRef& self, const CallbackTypeValues&) {
        auto data = static_cast<ObservableUIRawMessageHandle*>(self->native.get())->base->getData();

        auto handle = std::make_shared<ObservableUIRawMessageHandle>();
        handle->base = std::make_unique<ll::ui::ObservableUIRawMessage>(
            data, ll::ui::ObservableOptions{ isClientWritable(self, {}) }
        );

        auto obj = std::make_shared<Object>();
        obj->className = "ObservableUIRawMessage";
        obj->classIndex = -1;
        obj->native = handle;

        return obj;
    }

    bool setData(const ObjectRef& self, const CallbackTypeValues& args) {
        if (const auto* current = std::get_if<ObjectRef>(&args[0])) {
            static_cast<ObservableUIRawMessageHandle*>(self->native.get())->base->setData(
                static_cast<UIRawMessageClass::UIRawMessageHandle*>((*current)->native.get())->base
            );

            return true;
        }

        return false;
    }

    ll::Expected<int> subscribe(const ObjectRef& self, const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        auto func = std::get<FunctionRefPtr>(args[0]);
        if (func->argCount != 1)
            return ll::makeStringError("Subscribe function only needs one bool parameter");

        return static_cast<ObservableUIRawMessageHandle*>(self->native.get())->base->subscribe([
            option = static_cast<ObservableUIRawMessageHandle*>(self->native.get())->base->isClientWritable(), func, placeholders
        ](const ll::ui::UIRawMessage& value) -> void {
            auto handle = std::make_shared<ObservableUIRawMessageHandle>();
            handle->base = std::make_unique<ll::ui::ObservableUIRawMessage>(
                value, ll::ui::ObservableOptions{ option }
            );

            auto obj = std::make_shared<Object>();
            obj->className = "ObservableUIRawMessage";
            obj->classIndex = -1;
            obj->native = handle;
            
            DiagnosticEngine diagnostics;

            [[maybe_unused]] auto result = ir::VM::callFunctionRef(func, { obj }, placeholders, diagnostics);

            if (diagnostics.hasErrors()) {
                ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA")
                    ->error("ObservableUIRawMessage::subscribe callback: {}", diagnostics.getErrorMessage());
            }
        });
    }

    bool unsubscribe(const ObjectRef& self, const CallbackTypeValues& args) {
        return static_cast<ObservableUIRawMessageHandle*>(self->native.get())->base->unsubscribe(std::get<int>(args[0]));
    }

    void registerClasses(const std::string&) {
        ClassCall& classes = ClassCall::getInstance();

        classes.registerClass("ObservableUIRawMessage", {});
        classes.registerConstructor("ObservableUIRawMessage", makeObservableUIRawMessage, { ParamType::OBJECT, ParamType::BOOL });
        classes.registerMethod("ObservableUIRawMessage", "isClientWritable", isClientWritable, {});
        classes.registerMethod("ObservableUIRawMessage", "getData", getData, {});
        classes.registerMethod("ObservableUIRawMessage", "setData", setData, { ParamType::OBJECT });
        classes.registerMethod("ObservableUIRawMessage", "subscribe", subscribe, { ParamType::FUNCTION });
        classes.registerMethod("ObservableUIRawMessage", "unsubscribe", unsubscribe, { ParamType::INT });
    }
}

REGISTER_CALLBACK(ObservableUIRawMessage, ObservableUIRawMessageClass::registerClasses)
