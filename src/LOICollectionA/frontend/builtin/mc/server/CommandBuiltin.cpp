#include <any>
#include <string>

#include <ll/api/Expected.h>

#include <mc/world/actor/player/Player.h>

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/Context.h"

#include "LOICollectionA/frontend/builtin/mc/server/CommandBuiltin.h"

#include "LOICollectionA/frontend/sandbox/ScriptPermission.h"

#include "LOICollectionA/utils/mc-server/CommandUtils.h"

using namespace LOICollection::frontend;

namespace CommandBuiltin {
    namespace {
        std::string scriptIdOf(const CallbackTypePlaces& placeholders) {
            const auto it = placeholders.find(Context::kScriptIdKey);
            return it == placeholders.end() ? std::string{} : std::any_cast<std::string>(it->second);
        }
    }

    ll::Expected<TypedValue> runCmd(const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        const std::string command = std::get<std::string>(args[0]);
        const std::string scriptId = scriptIdOf(placeholders);

        std::reference_wrapper<Player> player = std::any_cast<std::reference_wrapper<Player>>(placeholders.at(0));

        if (!sandbox::permissionGate().isCommandAllowed(scriptId, command, std::string(player.get().mName)))
            return ll::makeStringError("Permission denied: mc::runCmd is not allowed for script '" + scriptId + "'");

        // The host executes the command with the highest server-side permission
        // level (unchanged); the gate above only decides whether this script may
        // issue the command, and which commands it may run.
        CommandUtils::executeCommand(player.get(), command);

        return TypedValue{};
    }

    void registerFunctions(const std::string& namespaces) {
        FunctionCall::getInstance().registerFunction(namespaces, "runCmd", runCmd, { ParamType::STRING });
    }
}

REGISTER_CALLBACK(mc, CommandBuiltin::registerFunctions)
