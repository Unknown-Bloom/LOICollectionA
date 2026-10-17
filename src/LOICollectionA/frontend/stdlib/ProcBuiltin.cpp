#include <chrono>
#include <variant>

#include <ll/api/Expected.h>

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/ir/VM.h"
#include "LOICollectionA/frontend/runtime/Scheduler.h"

using namespace LOICollection::frontend;
using namespace LOICollection::frontend::runtime;

namespace ProcBuiltin {

    static long long asLong(const ValueNode::ValueType& v) {
        if (std::holds_alternative<int>(v)) return std::get<int>(v);
        if (std::holds_alternative<float>(v)) return static_cast<long long>(std::get<float>(v));
        return 0;
    }

    ll::Expected<TypedValue> spawn(const CallbackTypeValues& args) {
        if (args.empty() || !std::holds_alternative<FunctionRefPtr>(args[0]))
            return ll::makeStringError("Proc::spawn requires a function as its first argument");

        const auto& fn = std::get<FunctionRefPtr>(args[0]);
        CallbackTypeValues spawnArgs(args.begin() + 1, args.end());
        if (static_cast<int>(spawnArgs.size()) != fn->argCount)
            return ll::makeStringError("Proc::spawn argument count does not match the function");

        const TaskId id = ProcScheduler::getInstance().spawn(fn, spawnArgs);
        if (!id)
            return ll::makeStringError("Proc::spawn failed to create the task");

        return static_cast<int>(id);
    }

    ll::Expected<TypedValue> yield(const CallbackTypeValues&) {
        ir::VM* vm = ir::VM::current();
        if (!vm || !vm->isSchedulerManaged())
            return ll::makeStringError("Proc::yield may only be called inside a spawned task");
        vm->requestYield();
        return std::monostate{};
    }

    ll::Expected<TypedValue> sleep(const CallbackTypeValues& args) {
        ir::VM* vm = ir::VM::current();
        if (!vm || !vm->isSchedulerManaged())
            return ll::makeStringError("Proc::sleep may only be called inside a spawned task");

        const long long ms = args.empty() ? 0 : asLong(args[0]);
        vm->requestSleep(ProcScheduler::getInstance().now() + std::chrono::milliseconds(ms));
        return std::monostate{};
    }

    ll::Expected<TypedValue> send(const CallbackTypeValues& args) {
        if (args.size() < 2)
            return ll::makeStringError("Proc::send requires (taskId, message)");
        ProcScheduler::getInstance().send(static_cast<TaskId>(asLong(args[0])), args[1]);
        return std::monostate{};
    }

    ll::Expected<TypedValue> recv(const CallbackTypeValues&) {
        ir::VM* vm = ir::VM::current();
        if (!vm || !vm->isSchedulerManaged())
            return ll::makeStringError("Proc::recv may only be called inside a spawned task");

        ValueNode::ValueType out;
        if (ProcScheduler::getInstance().recvCurrent(out))
            return out;
        return std::monostate{};
    }

    ll::Expected<TypedValue> join(const CallbackTypeValues& args) {
        if (args.empty())
            return ll::makeStringError("Proc::join requires a task id");

        const TaskId target = static_cast<TaskId>(asLong(args[0]));
        ir::VM* vm = ir::VM::current();
        if (vm && vm->isSchedulerManaged()) {
            vm->requestJoin(target);
            return std::monostate{};
        }
        return ProcScheduler::getInstance().joinFromHost(target);
    }

    void registerClasses(const std::string&) {
        ClassCall& classes = ClassCall::getInstance();
        classes.registerClass("Proc", {});

        classes.registerStaticMethod("Proc", "spawn", spawn, { ParamType::FUNCTION });
        classes.registerStaticMethod("Proc", "yield", yield, {});
        classes.registerStaticMethod("Proc", "sleep", sleep, { ParamType::INT });
        classes.registerStaticMethod("Proc", "join", join, { ParamType::INT });

        for (const ParamType t : { ParamType::INT, ParamType::FLOAT, ParamType::STRING,
                                   ParamType::BOOL, ParamType::OBJECT, ParamType::FUNCTION, ParamType::ARRAY })
            classes.registerStaticMethod("Proc", "send", send, { ParamType::INT, t });

        classes.registerStaticMethod("Proc", "recv", recv, {});
    }
}

REGISTER_CALLBACK(Proc, ProcBuiltin::registerClasses)
