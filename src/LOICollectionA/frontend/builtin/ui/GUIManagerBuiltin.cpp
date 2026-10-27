#pragma push_macro("MessageBox")
#undef MessageBox

#include <any>
#include <memory>
#include <string>

#include <ll/api/Expected.h>

#include "LOICollectionA/include/form/GUIManager.h"

#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/Context.h"

#include "LOICollectionA/frontend/builtin/ui/GUIManagerBuiltin.h"

#include "LOICollectionA/frontend/sandbox/ScriptPermission.h"

using namespace LOICollection::form;
using namespace LOICollection::frontend;

namespace GUIManagerBuiltin::detail {
    std::string scriptIdOf(const CallbackTypePlaces& placeholders) {
        return Context::scriptIdOf(placeholders);
    }

    std::shared_ptr<sandbox::ScriptPermissionService> permissionService() {
        return ServiceProvider::getInstance().getService<sandbox::ScriptPermissionService>();
    }
}

namespace GUIManagerBuiltin {
    using namespace detail;

    ll::Expected<TypedValue> value(const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        const std::string id = std::get<std::string>(args[0]);
        const std::string scriptId = scriptIdOf(placeholders);

        const auto permission = permissionService();
        if (!permission || !permission->gate().isGuiValueAllowed(scriptId, id))
            return ll::makeStringError(
                "Permission denied: GUIManager::value '" + id + "' is not allowed for script '" + scriptId +
                "'. Add it to permission.json under scripts." + scriptId + ".gui.values"
            );

        const auto player = Context::playerOf(placeholders);
        if (!player)
            return ll::makeStringError("GUIManager::value requires a player context");

        return GUIManager::getInstance().getValue(id, player->get());
    }

    ll::Expected<TypedValue> request(const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        const std::string id = std::get<std::string>(args[0]);
        const std::string scriptId = scriptIdOf(placeholders);

        const auto permission = permissionService();
        if (!permission || !permission->gate().isGuiRequestAllowed(scriptId, id))
            return ll::makeStringError(
                "Permission denied: GUIManager::request '" + id + "' is not allowed for script '" + scriptId +
                "'. Add it to permission.json under scripts." + scriptId + ".gui.requests"
            );

        const auto player = Context::playerOf(placeholders);
        if (!player)
            return ll::makeStringError("GUIManager::request requires a player context");

        return GUIManager::getInstance().getRequest(id, std::get<ArrayRef>(args[1]), player->get());
    }

    ll::Expected<TypedValue> callback(const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        const std::string id = std::get<std::string>(args[0]);
        const std::string scriptId = scriptIdOf(placeholders);

        const auto permission = permissionService();
        if (!permission || !permission->gate().isGuiCallbackAllowed(scriptId, id))
            return ll::makeStringError(
                "Permission denied: GUIManager::callback '" + id + "' is not allowed for script '" + scriptId +
                "'. Add it to permission.json under scripts." + scriptId + ".gui.callbacks"
            );

        const auto player = Context::playerOf(placeholders);
        if (!player)
            return ll::makeStringError("GUIManager::callback requires a player context");

        auto result = GUIManager::getInstance().getCallback(
            id,
            std::get<ArrayRef>(args[1]),
            player->get()
        );

        if (!result.has_value())
            return ll::Unexpected(result.error());

        return TypedValue{};
    }

    ll::Expected<TypedValue> open(const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        const std::string scriptId = scriptIdOf(placeholders);
        const std::string targetScript = std::get<std::string>(args[0]);
        const std::string formId = std::get<std::string>(args[1]);

        const auto permission = permissionService();
        if (!permission || !permission->gate().isGuiNavigationAllowed(scriptId))
            return ll::makeStringError(
                "Permission denied: GUIManager::open is not allowed for script '" + scriptId +
                "'. Set scripts." + scriptId + ".enabled to true in permission.json"
            );

        if (!permission->gate().isGuiNavigationTargetAllowed(scriptId, targetScript, formId))
            return ll::makeStringError(
                "Permission denied: GUIManager::open target '" + targetScript + "/" + formId +
                "' is not allowed for script '" + scriptId +
                "'. Add '" + targetScript + "' to permission.json under scripts." + scriptId + ".gui.navigations"
            );

        const auto player = Context::playerOf(placeholders);
        if (!player)
            return ll::makeStringError("GUIManager::open requires a player context");

        auto result = GUIManager::getInstance().open(
            targetScript,
            formId,
            static_cast<GUIManagerType>(std::get<int>(args[2])),
            player->get(),
            args.size() >= 4 ? std::get<ArrayRef>(args[3]) : ArrayRef{}
        );

        if (!result.has_value())
            return ll::Unexpected(result.error());

        return TypedValue{};
    }

    ll::Expected<TypedValue> switchTo(const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        const std::string scriptId = scriptIdOf(placeholders);
        const auto permission = permissionService();
        if (!permission || !permission->gate().isGuiNavigationAllowed(scriptId))
            return ll::makeStringError(
                "Permission denied: GUIManager::switchTo is not allowed for script '" + scriptId +
                "'. Set scripts." + scriptId + ".enabled to true in permission.json"
            );

        const auto player = Context::playerOf(placeholders);
        if (!player)
            return ll::makeStringError("GUIManager::switchTo requires a player context");

        ll::Expected<void> result;

        switch (std::get<int>(args[1])) {
            case static_cast<int>(GUIManagerType::CustomForm): {
                result = GUIManager::getInstance().switchToCustomForm(
                    std::get<std::string>(args[0]), player->get(), scriptId
                );

                break;
            }
            case static_cast<int>(GUIManagerType::MessageBox): {
                result = GUIManager::getInstance().switchToMessageBox(
                    std::get<std::string>(args[0]), player->get(), scriptId
                );

                break;
            }
            case static_cast<int>(GUIManagerType::PaginatedForm): {
                result = GUIManager::getInstance().switchToPaginatedForm(
                    std::get<std::string>(args[0]), player->get(), scriptId
                );

                break;
            }
            case static_cast<int>(GUIManagerType::ScriptForm): {
                result = GUIManager::getInstance().switchToScriptForm(
                    std::get<std::string>(args[0]), player->get(), scriptId
                );

                break;
            }
            default: result = ll::makeStringError("switchTo: Unknown form type");
        }

        if (!result.has_value())
            return ll::Unexpected(result.error());

        return TypedValue{};
    }

    void registerFunctions(const std::string& namespaces) {
        FunctionCall& functions = FunctionCall::getInstance();

        functions.registerFunction(namespaces, "value", value, { ParamType::STRING });
        functions.registerFunction(namespaces, "request", request, { ParamType::STRING, ParamType::ARRAY });
        functions.registerFunction(namespaces, "callback", callback, { ParamType::STRING, ParamType::ARRAY });
        functions.registerFunction(namespaces, "open", open, { ParamType::STRING, ParamType::STRING, ParamType::INT });
        functions.registerFunction(namespaces, "open", open, { ParamType::STRING, ParamType::STRING, ParamType::INT, ParamType::ARRAY });
        functions.registerFunction(namespaces, "switchTo", switchTo, { ParamType::STRING, ParamType::INT });
    }
}

REGISTER_CALLBACK(GUIManager, GUIManagerBuiltin::registerFunctions)

#pragma pop_macro("MessageBox")
