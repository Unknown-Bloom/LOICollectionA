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

#include "LOICollectionA/frontend/stdlib/ObservableStringClass.h"

using namespace LOICollection::frontend;

namespace ObservableStringClass {
    namespace {
        std::optional<std::string> stringOperand(const TypedValue& value) {
            if (const auto* text = std::get_if<std::string>(&value))
                return *text;
            if (const auto* obj = std::get_if<ObjectRef>(&value)) {
                if ((*obj)->className == "ObservableString" && (*obj)->native)
                    return static_cast<ObservableStringHandle*>((*obj)->native.get())->base->getData();
            }

            return std::nullopt;
        }
    }
    ll::Expected<ObjectRef> makeObservableString(const CallbackTypeValues& args) {
        auto handle = std::make_shared<ObservableStringHandle>();
        handle->base = std::make_unique<ll::ui::ObservableString>(
            std::get<std::string>(args[0]), ll::ui::ObservableOptions{ std::get<bool>(args[1]) }
        );

        auto obj = std::make_shared<Object>();
        obj->className = "ObservableString";
        obj->classIndex = -1;
        obj->native = handle;

        return obj;
    }

    bool isClientWritable(const ObjectRef& self, const CallbackTypeValues&) {
        return static_cast<ObservableStringHandle*>(self->native.get())->base->isClientWritable();
    }

    std::string getData(const ObjectRef& self, const CallbackTypeValues&) {
        return static_cast<ObservableStringHandle*>(self->native.get())->base->getData();
    }

    bool setData(const ObjectRef& self, const CallbackTypeValues& args) {
        static_cast<ObservableStringHandle*>(self->native.get())->base->setData(std::get<std::string>(args[0]));

        return true;
    }

    ll::Expected<int> subscribe(const ObjectRef& self, const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        auto func = std::get<FunctionRefPtr>(args[0]);
        if (func->argCount != 1)
            return ll::makeStringError("Subscribe function only needs one string parameter");

        return static_cast<ObservableStringHandle*>(self->native.get())->base->subscribe([func, placeholders](const std::string& value) -> void {
            DiagnosticEngine diagnostics;

            [[maybe_unused]] auto result = ir::VM::callFunctionRef(func, { value }, placeholders, diagnostics);

            if (diagnostics.hasErrors()) {
                ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA")
                    ->error("ObservableString::subscribe callback: {}", diagnostics.getErrorMessage());
            }
        });
    }

    bool unsubscribe(const ObjectRef& self, const CallbackTypeValues& args) {
        return static_cast<ObservableStringHandle*>(self->native.get())->base->unsubscribe(std::get<int>(args[0]));
    }

    void registerClasses(const std::string&) {
        ClassCall& classes = ClassCall::getInstance();

        classes.registerClass("ObservableString", {});
        classes.registerConstructor("ObservableString", makeObservableString, { ParamType::STRING, ParamType::BOOL });
        classes.registerMethod("ObservableString", "isClientWritable", isClientWritable, {});
        classes.registerMethod("ObservableString", "getData", getData, {});
        classes.registerMethod("ObservableString", "setData", setData, { ParamType::STRING });
        classes.registerMethod("ObservableString", "subscribe", subscribe, { ParamType::FUNCTION });
        classes.registerMethod("ObservableString", "unsubscribe", unsubscribe, { ParamType::INT });

        // "obs + x" mirrors "getData() + x"; "obs == x" compares the stored text.
        classes.registerOperator("ObservableString", "+", [](const TypedValue& left, const TypedValue& right) -> ll::Expected<TypedValue> {
            auto l = stringOperand(left);
            if (!l)
                return ll::makeStringError("ObservableString operators require string operands");

            /* Unwrap an ObservableString right side to its data; other types
             * still stringify so "obs + number" keeps concatenating. */
            if (auto r = stringOperand(right))
                return *l + *r;
            return *l + ir::VM::valueToString(right);
        });
        for (const std::string& op : { "==", "!=", ">", "<", ">=", "<=" })
            classes.registerOperator("ObservableString", op, [op](const TypedValue& left, const TypedValue& right) -> ll::Expected<TypedValue> {
                auto l = stringOperand(left);
                auto r = stringOperand(right);
                if (!l || !r)
                    return ll::makeStringError("ObservableString operators require string operands");

                auto cmp = *l <=> *r;
                if (op == "==") return cmp == 0;
                if (op == "!=") return cmp != 0;
                if (op == ">") return cmp > 0;
                if (op == "<") return cmp < 0;
                if (op == ">=") return cmp >= 0;

                return cmp <= 0;
            });
    }
}

REGISTER_CALLBACK(ObservableString, ObservableStringClass::registerClasses)
