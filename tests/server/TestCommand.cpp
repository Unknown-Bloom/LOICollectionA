#include <gtest/gtest.h>

#include <chrono>

#include <ll/api/memory/Hook.h>

#include <ll/api/coro/CoroTask.h>
#include <ll/api/thread/ServerThreadExecutor.h>

#include <ll/api/command/Command.h>
#include <ll/api/command/CommandHandle.h>
#include <ll/api/command/CommandRegistrar.h>

#include <mc/scripting/ServerScriptManager.h>

#include <mc/server/commands/CommandOrigin.h>
#include <mc/server/commands/CommandOutput.h>

#include "server/TestSimulatedPlayer.h"

LL_AUTO_TYPE_INSTANCE_HOOK(
    registerBuiltinCommandsHook,
    ll::memory::HookPriority::Normal,
    ServerScriptManager,
    &ServerScriptManager::$onServerThreadStarted,
    EventResult,
    ServerInstance& ins
) {
    auto result = origin(ins);

    testing::InitGoogleTest();

    auto& cmd = ll::command::CommandRegistrar::getServerInstance().getOrCreateCommand("test", "LOICollectionA -> test command");
    cmd.overload().text("all").execute([](CommandOrigin const&, CommandOutput& output) -> void {
        ll::coro::keepThis([output]() -> ll::coro::CoroTask<> {
            TestSimulatedPlayer sp("test_player");
            if (!sp.create())
                co_return;
            
            co_await std::chrono::seconds(1);

            static_cast<void>(RUN_ALL_TESTS());

            sp.destroy();
        }).launch(ll::thread::ServerThreadExecutor::getDefault());
    });

    return result;
}
