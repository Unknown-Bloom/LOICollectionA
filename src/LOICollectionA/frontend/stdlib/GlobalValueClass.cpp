#include <string>
#include <variant>

#include <ll/api/Expected.h>

#include "LOICollectionA/frontend/Callback.h"

#include "LOICollectionA/frontend/stdlib/GlobalValueClass.h"

using namespace LOICollection::frontend;

namespace GlobalValueClass {
    ll::Expected<ObjectRef> makeGlobalValue(const CallbackTypeValues&) {
        auto handle = std::make_shared<GlobalValueHandle>();
        handle->value = std::monostate{};

        auto obj = std::make_shared<Object>();
        obj->className = "GlobalValue";
        obj->classIndex = -1;
        obj->native = handle;

        return obj;
    }

    void registerClasses(const std::string&) {
        ClassCall& classes = ClassCall::getInstance();

        classes.registerClass("GlobalValue", { "value" });
        classes.registerField("GlobalValue", "value", std::monostate{});
        classes.registerConstructor("GlobalValue", makeGlobalValue, {});
    }
}

REGISTER_CALLBACK(GlobalValue, GlobalValueClass::registerClasses)