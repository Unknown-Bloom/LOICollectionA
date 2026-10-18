#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "LOICollectionA/frontend/AST.h"
#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/CancelToken.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/ir/VM.h"

namespace LOICollection::frontend::runtime {

    using TaskId = uint64_t;

    struct ProcSchedulerConfig {
        size_t sliceInstructions = 100'000;
        size_t joinMaxPumps = 100'000'000;
        size_t drainGuard = 100'000'000;
        sandbox::SandboxBudget taskBudget;
    };

    enum class TaskState {
        Ready,
        Sleeping,
        BlockedRecv,
        BlockedJoin,
        Done,
    };

    class ProcScheduler {
    public:
        static ProcScheduler& getInstance();

        TaskId spawn(const FunctionRefPtr& fn, const CallbackTypeValues& args,
                     const std::string& scriptId = "", const std::string& ownerId = "");

        void send(TaskId to, ValueNode::ValueType msg);

        bool cancel(TaskId id);

        bool isCancelled(TaskId id) const {
            const Task* t = findTask(id);
            return t != nullptr && t->cancelled;
        }

        bool recvCurrent(ValueNode::ValueType& out);

        ValueNode::ValueType joinFromHost(TaskId target);

        size_t pump(std::chrono::steady_clock::time_point now);
        size_t pump();

        void drain();

        void setConfig(const ProcSchedulerConfig& cfg) { config_ = cfg; }
        const ProcSchedulerConfig& config() const { return config_; }

        size_t taskCount() const { return tasks_.size(); }
        size_t readyCount() const { return ready_.size(); }
        bool hasTask(TaskId id) const { return findTask(id) != nullptr; }
        bool anyTaskPending() const {
            for (auto& t : tasks_)
                if (t && t->state != TaskState::Done)
                    return true;
            return false;
        }
        bool isDone(TaskId id) const {
            const Task* t = findTask(id);
            return t != nullptr && t->state == TaskState::Done;
        }

        void reset();

        std::function<std::chrono::steady_clock::time_point()> clockFn;

        std::chrono::steady_clock::time_point now() const {
            if (clockFn)
                return clockFn();
            return std::chrono::steady_clock::now();
        }

    private:
        struct Task {
            TaskId id = 0;
            TaskState state = TaskState::Ready;
            std::shared_ptr<DiagnosticEngine> diag;
            std::shared_ptr<ir::VM> vm;
            FunctionRefPtr fn;
            CallbackTypeValues args;
            std::chrono::steady_clock::time_point wakeupAt{};
            std::deque<ValueNode::ValueType> mailbox;
            std::vector<TaskId> joiners;
            ValueNode::ValueType result;
            bool hasError = false;
            bool cancelled = false;
            CancelToken token;
            std::string errorMessage;
            std::string scriptId;
        };

        Task* findTask(TaskId id) const;
        Task* findTaskByVM(ir::VM* vm);
        void completeTask(Task& task);
        void runSlice(Task& task);

        using Sleeper = std::pair<std::chrono::steady_clock::time_point, TaskId>;

        std::vector<std::unique_ptr<Task>> tasks_;
        std::unordered_map<TaskId, Task*> index_;
        std::priority_queue<Sleeper, std::vector<Sleeper>, std::greater<>> sleepers_;
        std::deque<TaskId> ready_;
        TaskId nextId_ = 1;
        ProcSchedulerConfig config_;
    };

}
