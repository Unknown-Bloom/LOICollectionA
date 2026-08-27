#include <any>
#include <string>

#include <ll/api/Expected.h>

#include <mc/world/actor/player/Player.h>

#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/Context.h"

#include "LOICollectionA/frontend/builtin/mc/server/CommandBuiltin.h"

#include "LOICollectionA/frontend/sandbox/ScriptPermission.h"

#include "LOICollectionA/utils/mc-server/CommandUtils.h"

using namespace LOICollection::frontend;

namespace CommandBuiltin {
    ll::Expected<TypedValue> runCmd(const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        const std::string command = std::get<std::string>(args[0]);
        const std::string scriptId = Context::scriptIdOf(placeholders);

        const auto playerIt = placeholders.find(0);
        if (playerIt == placeholders.end())
            return ll::makeStringError("mc::runCmd requires a player context");

        std::reference_wrapper<Player> player = std::any_cast<std::reference_wrapper<Player>>(playerIt->second);

        const auto permission = ServiceProvider::getInstance().getService<sandbox::ScriptPermissionService>();
        if (!permission || !permission->gate().isCommandAllowed(scriptId, command))
            return ll::makeStringError(
                "Permission denied: mc::runCmd '" + command + "' is not allowed for script '" + scriptId +
                "'. Add it to permission.json under scripts." + scriptId + ".commands.templates"
            );

        CommandUtils::executeCommand(player.get(), command);

        return TypedValue{};
    }

    void registerFunctions(const std::string& namespaces) {
        FunctionCall::getInstance().registerFunction(namespaces, "runCmd", runCmd, { ParamType::STRING });
    }
}

REGISTER_CALLBACK(mc, CommandBuiltin::registerFunctions)
