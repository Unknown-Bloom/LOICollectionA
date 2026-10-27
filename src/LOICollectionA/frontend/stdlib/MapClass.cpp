#include <string>
#include <utility>
#include <variant>
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <vector>

#include <ll/api/Expected.h>

#include "LOICollectionA/frontend/Callback.h"

#include "LOICollectionA/frontend/stdlib/MapClass.h"

using namespace LOICollection::frontend;

namespace MapClass::detail {
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

    struct KeyHash {
        std::size_t operator()(const TypedValue& key) const {
            if (auto value = std::get_if<int>(&key))
                return std::hash<float>{}(static_cast<float>(*value));

            if (auto value = std::get_if<float>(&key))
                return std::hash<float>{}(*value == 0.0f ? 0.0f : *value);

            if (auto value = std::get_if<std::string>(&key))
                return std::hash<std::string>{}(*value);

            if (auto value = std::get_if<bool>(&key))
                return std::hash<bool>{}(*value);

            return 0;
        }
    };

    struct KeyEqual {
        bool operator()(const TypedValue& left, const TypedValue& right) const {
            return keyEquals(left, right);
        }
    };

    struct MapHandle : NativeHandle {
        std::vector<std::pair<TypedValue, TypedValue>> entries;
        std::unordered_map<TypedValue, std::size_t, KeyHash, KeyEqual> positions;
        ArrayRef keys;
    };

    MapHandle& handleOf(const ObjectRef& self) {
        return *static_cast<MapHandle*>(self->native.get());
    }

    void bindKeys(const ObjectRef& self) {
        auto& handle = handleOf(self);

        handle.keys = std::make_shared<ArrayValue>();
        self->assign("keys", handle.keys);
    }

    void rebuildKeys(const ObjectRef& self) {
        auto& handle = handleOf(self);

        if (!handle.keys)
            handle.keys = std::make_shared<ArrayValue>();

        handle.keys->elements.clear();
        handle.keys->elements.reserve(handle.entries.size());
        for (const auto& entry : handle.entries)
            handle.keys->elements.push_back(entry.first);

        self->assign("keys", handle.keys);
    }
}

namespace MapClass {
    using namespace detail;

    ll::Expected<ObjectRef> makeMap(const CallbackTypeValues&) {
        auto handle = std::make_shared<MapHandle>();

        auto obj = std::make_shared<Object>();
        obj->className = "Map";
        obj->classIndex = -1;
        obj->native = handle;

        bindKeys(obj);

        return obj;
    }

    ll::Expected<TypedValue> set(const ObjectRef& self, const CallbackTypeValues& args) {
        auto& handle = handleOf(self);

        if (const auto it = handle.positions.find(args[0]); it != handle.positions.end()) {
            handle.entries[it->second].second = args[1];
            return args[1];
        }

        if (!handle.keys || handle.keys->elements.size() != handle.entries.size())
            rebuildKeys(self);

        const std::size_t index = handle.entries.size();
        handle.entries.emplace_back(args[0], args[1]);
        handle.positions.emplace(args[0], index);
        handle.keys->elements.push_back(args[0]);

        return args[1];
    }

    ll::Expected<TypedValue> get(const ObjectRef& self, const CallbackTypeValues& args) {
        auto& handle = handleOf(self);

        if (const auto it = handle.positions.find(args[0]); it != handle.positions.end())
            return handle.entries[it->second].second;

        return TypedValue{ std::monostate{} };
    }

    ll::Expected<TypedValue> has(const ObjectRef& self, const CallbackTypeValues& args) {
        return handleOf(self).positions.contains(args[0]);
    }

    ll::Expected<TypedValue> length(const ObjectRef& self, const CallbackTypeValues&) {
        return static_cast<int>(handleOf(self).entries.size());
    }

    ll::Expected<TypedValue> element(const ObjectRef& self, const CallbackTypeValues& args) {
        auto& entries = handleOf(self).entries;

        int index = std::get<int>(args[0]);
        if (index < 0 || static_cast<size_t>(index) >= entries.size())
            return ll::makeStringError("Map element index out of range");

        return entries[static_cast<size_t>(index)].first;
    }

    ll::Expected<TypedValue> remove(const ObjectRef& self, const CallbackTypeValues& args) {
        auto& handle = handleOf(self);

        const auto it = handle.positions.find(args[0]);
        if (it == handle.positions.end())
            return false;

        const std::size_t index = it->second;
        handle.positions.erase(it);
        handle.entries.erase(handle.entries.begin() + static_cast<std::ptrdiff_t>(index));

        for (std::size_t i = index; i < handle.entries.size(); ++i)
            handle.positions.insert_or_assign(handle.entries[i].first, i);

        rebuildKeys(self);

        return true;
    }

    void registerClasses(const std::string&) {
        ClassCall& classes = ClassCall::getInstance();

        classes.registerClass("Map", { "keys" });
        classes.registerConstructor("Map", makeMap, {});
        classes.registerMethod("Map", "length", length, {});
        classes.registerMethod("Map", "element", element, { ParamType::INT });

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
