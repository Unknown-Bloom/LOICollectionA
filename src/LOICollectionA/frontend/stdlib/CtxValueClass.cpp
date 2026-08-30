#include <string>
#include <variant>

#include <ll/api/Expected.h>

#include "LOICollectionA/frontend/Callback.h"

#include "LOICollectionA/frontend/stdlib/CtxValueClass.h"

using namespace LOICollection::frontend;

namespace CtxValueClass {
    ll::Expected<ObjectRef> makeCtxValue(const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        int index = std::get<int>(args[0]);

        auto ctxIt = placeholders.find(1);
        if (ctxIt == placeholders.end())
            return ll::makeStringError("CtxValue: no ctx parameter imported");

        auto ctxPtr = std::any_cast<ArrayRef>(&ctxIt->second);
        if (!ctxPtr || !*ctxPtr)
            return ll::makeStringError("CtxValue: invalid ctx parameter");

        if (index < 0 || index >= static_cast<int>((*ctxPtr)->elements.size()))
            return ll::makeStringError("CtxValue: ctx index out of range");

        auto handle = std::make_shared<CtxValueHandle>();
        handle->value = (*ctxPtr)->elements[index];

        auto obj = std::make_shared<Object>();
        obj->className = "CtxValue";
        obj->classIndex = -1;
        obj->native = handle;
        obj->assign("value", handle->value);

        return obj;
    }

    void registerClasses(const std::string&) {
        ClassCall& classes = ClassCall::getInstance();

        classes.registerClass("CtxValue", { "value" });
        classes.registerField("CtxValue", "value", std::monostate{});
        classes.registerConstructor("CtxValue", makeCtxValue, { ParamType::INT });
    }
}

REGISTER_CALLBACK(CtxValue, CtxValueClass::registerClasses)
