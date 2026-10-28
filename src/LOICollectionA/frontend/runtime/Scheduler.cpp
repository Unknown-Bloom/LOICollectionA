#include "LOICollectionA/frontend/runtime/Scheduler.h"

#include <algorithm>

#include "LOICollectionA/frontend/runtime/ScriptHeap.h"

namespace LOICollection::frontend::runtime {

    using namespace std::chrono_literals;

    ProcScheduler& ProcScheduler::getInstance() {
        static ProcScheduler instance;
        return instance;
    }

    ProcScheduler::Task* ProcScheduler::findTask(TaskId id) const {
        auto it = index_.find(id);
        return it == index_.end() ? nullptr : it->second;
    }

    ProcScheduler::Task* ProcScheduler::findTaskByVM(ir::VM* vm) {
        if (!vm)
            return nullptr;
        for (auto& t : tasks_)
            if (t && t->vm.get() == vm)
                return t.get();
        return nullptr;
    }

    void ProcScheduler::reapDone() {
        std::erase_if(tasks_, [this](const std::unique_ptr<Task>& task) {
            if (!task || task->state != TaskState::Done)
                return false;

            index_.erase(task->id);
            return true;
        });
    }

    void ProcScheduler::completeTask(Task& task) {
        task.state = TaskState::Done;
        for (TaskId jid : task.joiners) {
            if (Task* j = findTask(jid)) {
                if (j->state == TaskState::BlockedJoin) {
                    j->state = TaskState::Ready;
                    ready_.push_back(jid);
                }
            }
        }
        task.joiners.clear();
    }

    void ProcScheduler::runSlice(Task& task) {
        const auto outcome = task.vm->runTask(task.fn, task.args, config_.sliceInstructions, task.placeholders);
        switch (outcome) {
            case ir::VM::RunOutcome::Completed: {
                auto result = task.vm->taskResult();
                const std::size_t bytes = retainedBytes(result);

                if (bytes && task.budgetOwner
                    && task.budgetOwner->accountBytes(bytes) != sandbox::SandboxBudget::Violation::None) {
                    task.budgetOwner->releaseBytes(bytes);
                    task.hasError = true;
                    task.errorMessage = "Task result exceeds the memory budget";
                    completeTask(task);
                    break;
                }

                task.result = std::move(result);
                task.resultStringBytes = bytes;
                task.hasError = false;
                completeTask(task);
                break;
            }
            case ir::VM::RunOutcome::Failed:
                task.hasError = true;
                task.errorMessage = task.diag->getErrorMessage();
                completeTask(task);
                break;
            case ir::VM::RunOutcome::Canceled:
                task.cancelled = true;
                completeTask(task);
                break;
            case ir::VM::RunOutcome::Yielded:
            case ir::VM::RunOutcome::SliceExpired:
                task.state = TaskState::Ready;
                ready_.push_back(task.id);
                break;
            case ir::VM::RunOutcome::Blocked: {
                const ir::VM::TaskEvent& ev = task.vm->blockInfo();
                if (ev.kind == ir::VM::BlockKind::Sleep) {
                    task.state = TaskState::Sleeping;
                    task.wakeupAt = ev.wakeupAt;
                    sleepers_.emplace(ev.wakeupAt, task.id);
                } else if (ev.kind == ir::VM::BlockKind::Recv) {
                    task.state = TaskState::BlockedRecv;
                } else if (ev.kind == ir::VM::BlockKind::Join) {
                    task.state = TaskState::BlockedJoin;
                    if (Task* target = findTask(ev.joinTarget)) {
                        target->joiners.push_back(task.id);
                        if (target->state == TaskState::Done) {
                            task.state = TaskState::Ready;
                            ready_.push_back(task.id);
                        }
                    } else {
                        task.result = std::string("joined task does not exist");
                        task.hasError = false;
                        completeTask(task);
                    }
                }
                break;
            }
        }
    }

    TaskId ProcScheduler::spawn(const FunctionRefPtr& fn, const CallbackTypeValues& args,
                               const std::string& scriptId, const std::string& ownerId) {
        if (!fn || !fn->owner)
            return 0;
        if (fn->bodyIndex < 0 ||
            fn->bodyIndex >= static_cast<int>(fn->owner->methodBodies.size()))
            return 0;
        if (static_cast<int>(args.size()) != fn->argCount)
            return 0;

        auto globals = fn->globals.lock();
        if (!globals)
            return 0;

        if (tasks_.size() >= config_.maxTasks) {
            reapDone();
            if (tasks_.size() >= config_.maxTasks)
                return 0;
        }

        auto budget = ir::VM::current() ? ir::VM::current()->budgetHandle() : nullptr;

        std::size_t argBytes = 0;
        for (const auto& arg : args)
            argBytes += retainedBytes(arg);

        if (budget && argBytes
            && budget->accountBytes(argBytes) != sandbox::SandboxBudget::Violation::None) {
            budget->releaseBytes(argBytes);
            return 0;
        }

        auto task = std::make_unique<Task>();
        task->id = nextId_++;
        task->diag = std::make_shared<DiagnosticEngine>();
        task->vm = std::make_shared<ir::VM>(*task->diag, std::move(globals));
        task->vm->setSchedulerManaged(true);
        task->vm->setCancelToken(task->token);
        task->vm->setBudget(config_.taskBudget);
        if (!scriptId.empty())
            task->vm->setHeap(ScriptHeapRegistry::getInstance().forScript(scriptId));
        task->fn = fn;
        task->args = args;
        task->scriptId = scriptId;
        task->ownerId = ownerId;
        task->placeholders = Context::withOwnerId(Context::withScriptId({}, scriptId), ownerId);
        task->budgetOwner = budget ? budget : task->vm->budgetHandle();
        task->budgetStringBytes = budget ? argBytes : 0;
        task->state = TaskState::Ready;

        const TaskId id = task->id;
        index_.emplace(id, task.get());
        tasks_.push_back(std::move(task));
        ready_.push_back(id);
        return id;
    }

    bool ProcScheduler::send(TaskId to, ValueNode::ValueType msg) {
        Task* t = findTask(to);
        if (!t || t->state == TaskState::Done || t->mailbox.size() >= config_.maxMailboxMessages)
            return false;

        const std::size_t bytes = retainedBytes(msg);
        auto owner = ir::VM::current() ? ir::VM::current()->budgetHandle() : nullptr;
        if (bytes && owner
            && owner->accountBytes(bytes) != sandbox::SandboxBudget::Violation::None) {
            owner->releaseBytes(bytes);
            return false;
        }

        t->mailbox.push_back({ std::move(msg), owner, bytes });
        if (t->state == TaskState::BlockedRecv) {
            t->state = TaskState::Ready;
            ready_.push_back(t->id);
        }

        return true;
    }

    bool ProcScheduler::cancel(TaskId id) {
        Task* t = findTask(id);
        if (!t || t->state == TaskState::Done)
            return false;

        t->cancelled = true;
        t->token.cancel();

        if (t->state != TaskState::Ready)
            completeTask(*t);

        return true;
    }

    void ProcScheduler::cancelScript(const std::string& scriptId) {
        cancelMatching(scriptId, {});
    }

    void ProcScheduler::cancelOwner(const std::string& ownerId) {
        cancelMatching({}, ownerId);
    }

    void ProcScheduler::cancelMatching(const std::string& scriptId, const std::string& ownerId) {
        if (scriptId.empty() && ownerId.empty())
            return;

        for (auto& task : tasks_) {
            if (!task || task->state == TaskState::Done)
                continue;
            if (!((!scriptId.empty() && task->scriptId == scriptId)
                || (!ownerId.empty() && task->ownerId == ownerId)))
                continue;

            task->cancelled = true;
            task->token.cancel();
            completeTask(*task);
        }

        reapDone();
    }

    bool ProcScheduler::recvCurrent(ValueNode::ValueType& out) {
        ir::VM* vm = ir::VM::current();
        if (!vm || !vm->isSchedulerManaged())
            return false;
        Task* t = findTaskByVM(vm);
        if (!t)
            return false;
        if (!t->mailbox.empty()) {
            auto& entry = t->mailbox.front();
            out = std::move(entry.value);
            if (entry.owner)
                entry.owner->releaseBytes(entry.bytes);
            t->mailbox.pop_front();
            return true;
        }
        vm->requestRecv();
        return false;
    }

    ValueNode::ValueType ProcScheduler::joinFromHost(TaskId target) {
        Task* t = findTask(target);
        if (!t)
            return std::string("joined task does not exist");

        const size_t kMaxPumps = config_.joinMaxPumps;
        const auto deadline = now() + config_.joinTimeout;
        size_t pumps = 0;
        while (t->state != TaskState::Done) {
            if (++pumps > kMaxPumps || now() > deadline)
                break;
            const size_t ran = pump();
            if (ran == 0) {
                if (t->state == TaskState::Sleeping)
                    continue;
                bool recoverable = false;
                for (auto& o : tasks_)
                    if (o && o.get() != t && o->state == TaskState::Sleeping)
                        recoverable = true;
                if (recoverable)
                    continue;
                break;
            }
        }

        if (t->state != TaskState::Done)
            return std::string("join deadlocked: target is blocked with no runnable sender");
        if (t->cancelled)
            return std::string("task cancelled");
        if (t->hasError)
            return std::string("task failed: " + t->errorMessage);
        return t->result;
    }

    size_t ProcScheduler::pump(std::chrono::steady_clock::time_point now) {
        while (!sleepers_.empty() && sleepers_.top().first <= now) {
            const Sleeper sleeper = sleepers_.top();
            sleepers_.pop();

            Task* task = findTask(sleeper.second);
            if (!task || task->state != TaskState::Sleeping || task->wakeupAt != sleeper.first)
                continue;

            task->state = TaskState::Ready;
            ready_.push_back(task->id);
        }

        size_t ran = 0;
        size_t budget = ready_.size();
        while (budget-- > 0 && !ready_.empty()) {
            const TaskId id = ready_.front();
            ready_.pop_front();
            Task* t = findTask(id);
            if (!t || t->state != TaskState::Ready)
                continue;
            runSlice(*t);
            ++ran;
        }
        return ran;
    }

    size_t ProcScheduler::pump() {
        return pump(now());
    }

    void ProcScheduler::drain() {
        size_t guard = 0;
        while (guard++ < config_.drainGuard) {
            bool any = false;
            for (auto& t : tasks_) {
                if (t && t->state != TaskState::Done) {
                    any = true;
                    break;
                }
            }
            if (!any)
                break;
            if (pump() == 0)
                break;
        }
    }

    void ProcScheduler::reset() {
        tasks_.clear();
        index_.clear();
        sleepers_ = {};
        ready_.clear();
        nextId_ = 1;
        clockFn = {};
    }

}
