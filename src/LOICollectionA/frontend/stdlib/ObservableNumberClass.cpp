#include <cmath>
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

#include "LOICollectionA/frontend/stdlib/ObservableNumberClass.h"

using namespace LOICollection::frontend;

namespace ObservableNumberClass {
    namespace {
        std::optional<float> numberOperand(const TypedValue& value) {
            if (const auto* num = std::get_if<float>(&value))
                return *num;
            if (const auto* num = std::get_if<int>(&value))
                return static_cast<float>(*num);
            if (const auto* obj = std::get_if<ObjectRef>(&value)) {
                if ((*obj)->className == "ObservableNumber" && (*obj)->native)
                    return static_cast<ObservableNumberHandle*>((*obj)->native.get())->base->getData();
            }

            return std::nullopt;
        }

        ll::Expected<TypedValue> numberOperator(const TypedValue& left, const TypedValue& right, const std::string& op) {
            auto l = numberOperand(left);
            auto r = numberOperand(right);
            if (!l || !r)
                return ll::makeStringError("ObservableNumber operators require number operands");

            if (op == "+") return *l + *r;
            if (op == "-") return *l - *r;
            if (op == "*") return *l * *r;
            if (op == "/") return *l / *r;
            if (op == "%") return std::fmod(*l, *r);
            if (op == "^") return static_cast<float>(std::pow(*l, *r));

            auto cmp = *l <=> *r;
            if (op == "==") return cmp == 0;
            if (op == "!=") return cmp != 0;
            if (op == ">") return cmp > 0;
            if (op == "<") return cmp < 0;
            if (op == ">=") return cmp >= 0;
            if (op == "<=") return cmp <= 0;

            return ll::makeStringError("Unsupported operator for ObservableNumber: " + op);
        }
    }
    ll::Expected<ObjectRef> makeObservableNumber(const CallbackTypeValues& args) {
        auto handle = std::make_shared<ObservableNumberHandle>();
        handle->base = std::make_unique<ll::ui::ObservableNumber>(
            std::get<float>(args[0]), ll::ui::ObservableOptions{ std::get<bool>(args[1]) }
        );

        auto obj = std::make_shared<Object>();
        obj->className = "ObservableNumber";
        obj->classIndex = -1;
        obj->native = handle;

        return obj;
    }

    ll::Expected<ObjectRef> makeObservableNumberFromInt(const CallbackTypeValues& args) {
        auto handle = std::make_shared<ObservableNumberHandle>();
        handle->base = std::make_unique<ll::ui::ObservableNumber>(
            static_cast<float>(std::get<int>(args[0])), ll::ui::ObservableOptions{ std::get<bool>(args[1]) }
        );

        auto obj = std::make_shared<Object>();
        obj->className = "ObservableNumber";
        obj->classIndex = -1;
        obj->native = handle;

        return obj;
    }

    bool isClientWritable(const ObjectRef& self, const CallbackTypeValues&) {
        return static_cast<ObservableNumberHandle*>(self->native.get())->base->isClientWritable();
    }

    float getData(const ObjectRef& self, const CallbackTypeValues&) {
        return static_cast<float>(static_cast<ObservableNumberHandle*>(self->native.get())->base->getData());
    }

    bool setData(const ObjectRef& self, const CallbackTypeValues& args) {
        if (const auto* current = std::get_if<float>(&args[0])) {
            static_cast<ObservableNumberHandle*>(self->native.get())->base->setData(*current);
            
            return true;
        }

        static_cast<ObservableNumberHandle*>(self->native.get())->base->setData(std::get<int>(args[0]));

        return true;
    }

    ll::Expected<int> subscribe(const ObjectRef& self, const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        auto func = std::get<FunctionRefPtr>(args[0]);
        if (func->argCount != 1)
            return ll::makeStringError("Subscribe function only needs one float parameter");

        return static_cast<ObservableNumberHandle*>(self->native.get())->base->subscribe([func, placeholders](const double& value) -> void {
            DiagnosticEngine diagnostics;

            [[maybe_unused]] auto result = ir::VM::callFunctionRef(func, { static_cast<float>(value) }, placeholders, diagnostics);

            if (diagnostics.hasErrors()) {
                ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA")
                    ->error("ObservableNumber::subscribe callback: {}", diagnostics.getErrorMessage());
            }
        });
    }

    bool unsubscribe(const ObjectRef& self, const CallbackTypeValues& args) {
        return static_cast<ObservableNumberHandle*>(self->native.get())->base->unsubscribe(std::get<int>(args[0]));
    }

    void registerClasses(const std::string&) {
        ClassCall& classes = ClassCall::getInstance();

        classes.registerClass("ObservableNumber", {});
        classes.registerConstructor("ObservableNumber", makeObservableNumber, { ParamType::FLOAT, ParamType::BOOL });
        classes.registerConstructor("ObservableNumber", makeObservableNumberFromInt, { ParamType::INT, ParamType::BOOL });
        classes.registerMethod("ObservableNumber", "isClientWritable", isClientWritable, {});
        classes.registerMethod("ObservableNumber", "getData", getData, {});
        classes.registerMethod("ObservableNumber", "setData", setData, { ParamType::INT });
        classes.registerMethod("ObservableNumber", "setData", setData, { ParamType::FLOAT });
        classes.registerMethod("ObservableNumber", "subscribe", subscribe, { ParamType::FUNCTION });
        classes.registerMethod("ObservableNumber", "unsubscribe", unsubscribe, { ParamType::INT });

        for (const std::string& op : { "+", "-", "*", "/", "%", "^", "==", "!=", ">", "<", ">=", "<=" })
            classes.registerOperator("ObservableNumber", op, [op](const TypedValue& left, const TypedValue& right) {
                return numberOperator(left, right, op);
            });
    }
}

REGISTER_CALLBACK(ObservableNumber, ObservableNumberClass::registerClasses)
