#include <string>
#include <string_view>
#include <variant>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/Context.h"

#include "LOICollectionA/frontend/storage/ScriptStorage.h"

#include "LOICollectionA/frontend/builtin/storage/StorageBuiltin.h"

#include "LOICollectionA/frontend/sandbox/ScriptPermission.h"

using namespace LOICollection::frontend;

namespace ScriptDataBuiltin::detail {
    const char* capabilityName(sandbox::Capability capability) {
        switch (capability) {
            case sandbox::Capability::StorageRead: return "StorageRead";
            case sandbox::Capability::StorageWrite: return "StorageWrite";
            case sandbox::Capability::PlayerDataRead: return "PlayerDataRead";
            case sandbox::Capability::PlayerDataWrite: return "PlayerDataWrite";
            default: return "Unknown";
        }
    }

    bool authorize(
        const CallbackTypePlaces& placeholders, bool player, sandbox::Capability capability, std::string_view label,
        std::string& scriptId, std::string& ownerId, std::string& error
    ) {
        scriptId = Context::scriptIdOf(placeholders);
        if (scriptId.empty()) {
            error = std::string(label) + " requires a script context";
            return false;
        }

        if (player) {
            ownerId = Context::ownerIdOf(placeholders);
            if (ownerId.empty()) {
                error = std::string(label) + " requires a player context";
                return false;
            }
        }

        const auto permission = ServiceProvider::getInstance().getService<sandbox::ScriptPermissionService>();
        if (!permission || !permission->gate().hasCapability(scriptId, capability)) {
            error = "Permission denied: " + std::string(label) + " is not allowed for script '" + scriptId +
                "'. Grant " + capabilityName(capability) + " to its trust level in permission.json.";
            return false;
        }

        return true;
    }

    std::shared_ptr<storage::ScriptStorage> service(std::string& error) {
        auto result = ServiceProvider::getInstance().getService<storage::ScriptStorage>();
        if (!result)
            error = "Script storage service is not available";

        return result;
    }

    ll::Expected<TypedValue> get(
        const CallbackTypeValues& args, const CallbackTypePlaces& placeholders, bool player, std::string_view label
    ) {
        std::string scriptId;
        std::string ownerId;
        std::string error;
        if (!authorize(placeholders, player, player ? sandbox::Capability::PlayerDataRead : sandbox::Capability::StorageRead, label, scriptId, ownerId, error))
            return ll::makeStringError(error);

        auto storage = service(error);
        if (!storage)
            return ll::makeStringError(error);

        const std::string& key = std::get<std::string>(args[0]);
        return player ? storage->playerGet(ownerId, scriptId, key) : storage->get(scriptId, key);
    }

    ll::Expected<TypedValue> set(
        const CallbackTypeValues& args, const CallbackTypePlaces& placeholders, bool player, std::string_view label
    ) {
        std::string scriptId;
        std::string ownerId;
        std::string error;
        if (!authorize(placeholders, player, player ? sandbox::Capability::PlayerDataWrite : sandbox::Capability::StorageWrite, label, scriptId, ownerId, error))
            return ll::makeStringError(error);

        auto storage = service(error);
        if (!storage)
            return ll::makeStringError(error);

        const std::string& key = std::get<std::string>(args[0]);
        return player ? storage->playerSet(ownerId, scriptId, key, args[1]) : storage->set(scriptId, key, args[1]);
    }

    ll::Expected<TypedValue> has(
        const CallbackTypeValues& args, const CallbackTypePlaces& placeholders, bool player, std::string_view label
    ) {
        std::string scriptId;
        std::string ownerId;
        std::string error;
        if (!authorize(placeholders, player, player ? sandbox::Capability::PlayerDataRead : sandbox::Capability::StorageRead, label, scriptId, ownerId, error))
            return ll::makeStringError(error);

        auto storage = service(error);
        if (!storage)
            return ll::makeStringError(error);

        const std::string& key = std::get<std::string>(args[0]);
        return player ? storage->playerHas(ownerId, scriptId, key) : storage->has(scriptId, key);
    }

    ll::Expected<TypedValue> remove(
        const CallbackTypeValues& args, const CallbackTypePlaces& placeholders, bool player, std::string_view label
    ) {
        std::string scriptId;
        std::string ownerId;
        std::string error;
        if (!authorize(placeholders, player, player ? sandbox::Capability::PlayerDataWrite : sandbox::Capability::StorageWrite, label, scriptId, ownerId, error))
            return ll::makeStringError(error);

        auto storage = service(error);
        if (!storage)
            return ll::makeStringError(error);

        const std::string& key = std::get<std::string>(args[0]);
        return player ? storage->playerRemove(ownerId, scriptId, key) : storage->remove(scriptId, key);
    }

    ll::Expected<TypedValue> keys(
        const CallbackTypeValues&, const CallbackTypePlaces& placeholders, bool player, std::string_view label
    ) {
        std::string scriptId;
        std::string ownerId;
        std::string error;
        if (!authorize(placeholders, player, player ? sandbox::Capability::PlayerDataRead : sandbox::Capability::StorageRead, label, scriptId, ownerId, error))
            return ll::makeStringError(error);

        auto storage = service(error);
        if (!storage)
            return ll::makeStringError(error);

        return player ? storage->playerKeys(ownerId, scriptId) : storage->keys(scriptId);
    }

    ll::Expected<TypedValue> clear(
        const CallbackTypeValues&, const CallbackTypePlaces& placeholders, bool player, std::string_view label
    ) {
        std::string scriptId;
        std::string ownerId;
        std::string error;
        if (!authorize(placeholders, player, player ? sandbox::Capability::PlayerDataWrite : sandbox::Capability::StorageWrite, label, scriptId, ownerId, error))
            return ll::makeStringError(error);

        auto storage = service(error);
        if (!storage)
            return ll::makeStringError(error);

        if (player) {
            if (auto cleared = storage->playerClear(ownerId, scriptId); !cleared)
                return ll::Unexpected(cleared.error());
        } else {
            if (auto cleared = storage->clear(scriptId); !cleared)
                return ll::Unexpected(cleared.error());
        }

        return TypedValue{ std::monostate{} };
    }
}

namespace StorageBuiltin {
    ll::Expected<TypedValue> get(const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        return ScriptDataBuiltin::detail::get(args, placeholders, false, "storage::get");
    }

    ll::Expected<TypedValue> set(const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        return ScriptDataBuiltin::detail::set(args, placeholders, false, "storage::set");
    }

    ll::Expected<TypedValue> has(const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        return ScriptDataBuiltin::detail::has(args, placeholders, false, "storage::has");
    }

    ll::Expected<TypedValue> remove(const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        return ScriptDataBuiltin::detail::remove(args, placeholders, false, "storage::remove");
    }

    ll::Expected<TypedValue> keys(const CallbackTypeValues&, const CallbackTypePlaces& placeholders) {
        return ScriptDataBuiltin::detail::keys({}, placeholders, false, "storage::keys");
    }

    ll::Expected<TypedValue> clear(const CallbackTypeValues&, const CallbackTypePlaces& placeholders) {
        return ScriptDataBuiltin::detail::clear({}, placeholders, false, "storage::clear");
    }

    void registerFunctions(const std::string& namespaces) {
        FunctionCall& functions = FunctionCall::getInstance();

        functions.registerFunction(namespaces, "get", get, { ParamType::STRING });
        for (ParamType type : { ParamType::INT, ParamType::FLOAT, ParamType::STRING, ParamType::BOOL, ParamType::OBJECT, ParamType::ARRAY })
            functions.registerFunction(namespaces, "set", set, { ParamType::STRING, type });
        functions.registerFunction(namespaces, "has", has, { ParamType::STRING });
        functions.registerFunction(namespaces, "remove", remove, { ParamType::STRING });
        functions.registerFunction(namespaces, "keys", keys, {});
        functions.registerFunction(namespaces, "clear", clear, {});
    }
}

namespace PlayerDataBuiltin {
    ll::Expected<TypedValue> get(const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        return ScriptDataBuiltin::detail::get(args, placeholders, true, "playerdata::get");
    }

    ll::Expected<TypedValue> set(const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        return ScriptDataBuiltin::detail::set(args, placeholders, true, "playerdata::set");
    }

    ll::Expected<TypedValue> has(const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        return ScriptDataBuiltin::detail::has(args, placeholders, true, "playerdata::has");
    }

    ll::Expected<TypedValue> remove(const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        return ScriptDataBuiltin::detail::remove(args, placeholders, true, "playerdata::remove");
    }

    ll::Expected<TypedValue> keys(const CallbackTypeValues&, const CallbackTypePlaces& placeholders) {
        return ScriptDataBuiltin::detail::keys({}, placeholders, true, "playerdata::keys");
    }

    ll::Expected<TypedValue> clear(const CallbackTypeValues&, const CallbackTypePlaces& placeholders) {
        return ScriptDataBuiltin::detail::clear({}, placeholders, true, "playerdata::clear");
    }

    void registerFunctions(const std::string& namespaces) {
        FunctionCall& functions = FunctionCall::getInstance();

        functions.registerFunction(namespaces, "get", get, { ParamType::STRING });
        for (ParamType type : { ParamType::INT, ParamType::FLOAT, ParamType::STRING, ParamType::BOOL, ParamType::OBJECT, ParamType::ARRAY })
            functions.registerFunction(namespaces, "set", set, { ParamType::STRING, type });
        functions.registerFunction(namespaces, "has", has, { ParamType::STRING });
        functions.registerFunction(namespaces, "remove", remove, { ParamType::STRING });
        functions.registerFunction(namespaces, "keys", keys, {});
        functions.registerFunction(namespaces, "clear", clear, {});
    }
}

REGISTER_CALLBACK(storage, StorageBuiltin::registerFunctions)
REGISTER_CALLBACK(playerdata, PlayerDataBuiltin::registerFunctions)
