#include <memory>
#include <optional>
#include <string>

#include <ll/api/Expected.h>
#include <ll/api/io/Logger.h>
#include <ll/api/io/LoggerRegistry.h>
#include <ll/api/ui/base/Observable.h>

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"

#include "LOICollectionA/frontend/ir/VM.h"

#include "LOICollectionA/frontend/stdlib/ObservableBooleanClass.h"

using namespace LOICollection::frontend;

namespace ObservableBooleanClass {
    namespace {
        std::optional<bool> boolOperand(const TypedValue& value) {
            if (const auto* flag = std::get_if<bool>(&value))
                return *flag;
            if (const auto* obj = std::get_if<ObjectRef>(&value)) {
                if ((*obj)->className == "ObservableBoolean" && (*obj)->native)
                    return static_cast<ObservableBooleanHandle*>((*obj)->native.get())->base->getData();
            }

            return std::nullopt;
        }
    }
    ll::Expected<ObjectRef> makeObservableBoolean(const CallbackTypeValues& args) {
        auto handle = std::make_shared<ObservableBooleanHandle>();
        handle->base = std::make_unique<ll::ui::ObservableBoolean>(
            std::get<bool>(args[0]), ll::ui::ObservableOptions{ std::get<bool>(args[1]) }
        );

        auto obj = std::make_shared<Object>();
        obj->className = "ObservableBoolean";
        obj->classIndex = -1;
        obj->native = handle;

        return obj;
    }

    bool isClientWritable(const ObjectRef& self, const CallbackTypeValues&) {
        return static_cast<ObservableBooleanHandle*>(self->native.get())->base->isClientWritable();
    }

    bool getData(const ObjectRef& self, const CallbackTypeValues&) {
        return static_cast<ObservableBooleanHandle*>(self->native.get())->base->getData();
    }

    bool setData(const ObjectRef& self, const CallbackTypeValues& args) {
        static_cast<ObservableBooleanHandle*>(self->native.get())->base->setData(std::get<bool>(args[0]));

        return true;
    }

    ll::Expected<int> subscribe(const ObjectRef& self, const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        auto func = std::get<FunctionRefPtr>(args[0]);
        if (func->argCount != 1)
            return ll::makeStringError("Subscribe function only needs one bool parameter");

        auto* handle = static_cast<ObservableBooleanHandle*>(self->native.get());

        auto id = handle->base->subscribe([func, placeholders](const bool& value) -> void {
            DiagnosticEngine diagnostics;

            [[maybe_unused]] auto result = ir::VM::callFunctionRef(func, { value }, placeholders, diagnostics);

            if (diagnostics.hasErrors()) {
                ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA")
                    ->error("ObservableBoolean::subscribe callback: {}", diagnostics.getErrorMessage());
            }
        });

        handle->subscriptions.push_back(id);
        return id;
    }

    bool unsubscribe(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* handle = static_cast<ObservableBooleanHandle*>(self->native.get());
        auto id = static_cast<ObservableBooleanHandle::SubscriptionId>(std::get<int>(args[0]));

        std::erase(handle->subscriptions, id);

        return handle->base->unsubscribe(id);
    }

    void registerClasses(const std::string&) {
        ClassCall& classes = ClassCall::getInstance();

        classes.registerClass("ObservableBoolean", {});
        classes.registerConstructor("ObservableBoolean", makeObservableBoolean, { ParamType::BOOL, ParamType::BOOL });
        classes.registerMethod("ObservableBoolean", "isClientWritable", isClientWritable, {});
        classes.registerMethod("ObservableBoolean", "getData", getData, {});
        classes.registerMethod("ObservableBoolean", "setData", setData, { ParamType::BOOL });
        classes.registerMethod("ObservableBoolean", "subscribe", subscribe, { ParamType::FUNCTION });
        classes.registerMethod("ObservableBoolean", "unsubscribe", unsubscribe, { ParamType::INT });

        
        for (const std::string& op : { "==", "!=" })
            classes.registerOperator("ObservableBoolean", op, [op](const TypedValue& left, const TypedValue& right) -> ll::Expected<TypedValue> {
                auto l = boolOperand(left);
                auto r = boolOperand(right);
                if (!l || !r)
                    return ll::makeStringError("ObservableBoolean operators require bool operands");

                return (op == "==") == (*l == *r);
            });
    }
}

REGISTER_CALLBACK(ObservableBoolean, ObservableBooleanClass::registerClasses)
