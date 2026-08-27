#include <string>
#include <utility>
#include <variant>
#include <algorithm>
#include <vector>

#include <ll/api/Expected.h>

#include "LOICollectionA/frontend/Callback.h"

#include "LOICollectionA/frontend/stdlib/MapClass.h"

using namespace LOICollection::frontend;

namespace MapClass {
    struct MapHandle : NativeHandle {
        std::vector<std::pair<TypedValue, TypedValue>> entries;
    };

    namespace {
        MapHandle& handleOf(const ObjectRef& self) {
            return *static_cast<MapHandle*>(self->native.get());
        }

        bool keyEquals(const TypedValue& left, const TypedValue& right) {
            if (auto li = std::get_if<int>(&left)) {
                if (auto ri = std::get_if<int>(&right)) return *li == *ri;
                if (auto rf = std::get_if<float>(&right)) return *li == *rf;
                return false;
            }
            if (auto lf = std::get_if<float>(&left)) {
                if (auto ri = std::get_if<int>(&right)) return *lf == *ri;
                if (auto rf = std::get_if<float>(&right)) return *lf == *rf;
                return false;
            }
            if (auto ls = std::get_if<std::string>(&left)) {
                if (auto rs = std::get_if<std::string>(&right)) return *ls == *rs;
                return false;
            }
            if (auto lb = std::get_if<bool>(&left)) {
                if (auto rb = std::get_if<bool>(&right)) return *lb == *rb;
                return false;
            }

            return false;
        }

        void syncFields(const ObjectRef& self) {
            auto& handle = handleOf(self);

            auto keys = std::make_shared<ArrayValue>();
            keys->elements.reserve(handle.entries.size());
            for (const auto& [key, value] : handle.entries)
                keys->elements.push_back(key);

            self->fields["keys"] = keys;
        }
    }

    ll::Expected<ObjectRef> makeMap(const CallbackTypeValues&) {
        auto handle = std::make_shared<MapHandle>();

        auto obj = std::make_shared<Object>();
        obj->className = "Map";
        obj->classIndex = -1;
        obj->native = handle;

        syncFields(obj);

        return obj;
    }

    ll::Expected<TypedValue> set(const ObjectRef& self, const CallbackTypeValues& args) {
        auto& handle = handleOf(self);

        for (auto& [key, value] : handle.entries) {
            if (keyEquals(key, args[0])) {
                value = args[1];
                return args[1];
            }
        }

        handle.entries.emplace_back(args[0], args[1]);
        syncFields(self);

        return args[1];
    }

    ll::Expected<TypedValue> get(const ObjectRef& self, const CallbackTypeValues& args) {
        auto& handle = handleOf(self);

        for (const auto& [key, value] : handle.entries) {
            if (keyEquals(key, args[0]))
                return value;
        }

        return TypedValue{ std::monostate{} };
    }

    ll::Expected<TypedValue> has(const ObjectRef& self, const CallbackTypeValues& args) {
        auto& handle = handleOf(self);

        return std::ranges::any_of(handle.entries,
            [&args](const auto& entry) -> bool {
                return keyEquals(entry.first, args[0]);
            }
        );
    }

    ll::Expected<TypedValue> length(const ObjectRef& self, const CallbackTypeValues&) {
        return static_cast<int>(handleOf(self).entries.size());
    }

    ll::Expected<TypedValue> remove(const ObjectRef& self, const CallbackTypeValues& args) {
        auto& handle = handleOf(self);

        auto it = std::ranges::find_if(handle.entries,
            [&args](const auto& entry) -> bool {
                return keyEquals(entry.first, args[0]);
            }
        );
        if (it == handle.entries.end())
            return false;

        handle.entries.erase(it);
        syncFields(self);

        return true;
    }

    void registerClasses(const std::string&) {
        ClassCall& classes = ClassCall::getInstance();

        classes.registerClass("Map", { "keys" });
        classes.registerConstructor("Map", makeMap, {});
        classes.registerMethod("Map", "length", length, {});

        for (ParamType key : { ParamType::INT, ParamType::FLOAT, ParamType::STRING, ParamType::BOOL }) {
            classes.registerMethod("Map", "get", get, { key });
            classes.registerMethod("Map", "has", has, { key });
            classes.registerMethod("Map", "remove", remove, { key });

            for (ParamType value : { ParamType::INT, ParamType::FLOAT, ParamType::STRING, ParamType::BOOL,
                                     ParamType::OBJECT, ParamType::FUNCTION, ParamType::ARRAY })
                classes.registerMethod("Map", "set", set, { key, value });
        }
    }
}

REGISTER_CALLBACK(Map, MapClass::registerClasses)
