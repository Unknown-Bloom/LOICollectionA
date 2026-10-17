#include "LOICollectionA/frontend/runtime/Scheduler.h"

#include <algorithm>

namespace LOICollection::frontend::runtime {

    using namespace std::chrono_literals;

    ProcScheduler& ProcScheduler::getInstance() {
        static ProcScheduler instance;
        return instance;
    }

    ProcScheduler::Task* ProcScheduler::findTask(TaskId id) const {
        for (auto& t : tasks_)
            if (t && t->id == id)
                return t.get();
        return nullptr;
    }

    ProcScheduler::Task* ProcScheduler::findTaskByVM(ir::VM* vm) {
        if (!vm)
            return nullptr;
        for (auto& t : tasks_)
            if (t && t->vm.get() == vm)
                return t.get();
        return nullptr;
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
        const auto outcome = task.vm->runTask(task.fn, task.args, config_.sliceInstructions);
        switch (outcome) {
            case ir::VM::RunOutcome::Completed:
                task.result = task.vm->taskResult();
                task.hasError = false;
                completeTask(task);
                break;
            case ir::VM::RunOutcome::Failed:
                task.hasError = true;
                task.errorMessage = task.diag->getErrorMessage();
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
                               const std::string& scriptId, const std::string& /*ownerId*/) {
        if (!fn || !fn->owner)
            return 0;
        if (fn->bodyIndex < 0 ||
            fn->bodyIndex >= static_cast<int>(fn->owner->methodBodies.size()))
            return 0;
        if (static_cast<int>(args.size()) != fn->argCount)
            return 0;

        auto task = std::make_unique<Task>();
        task->id = nextId_++;
        task->diag = std::make_shared<DiagnosticEngine>();
        task->vm = std::make_shared<ir::VM>(*task->diag);
        task->vm->setSchedulerManaged(true);
        task->vm->setBudget(config_.taskBudget);
        task->fn = fn;
        task->args = args;
        task->scriptId = scriptId;
        task->state = TaskState::Ready;

        const TaskId id = task->id;
        tasks_.push_back(std::move(task));
        ready_.push_back(id);
        return id;
    }

    void ProcScheduler::send(TaskId to, ValueNode::ValueType msg) {
        Task* t = findTask(to);
        if (!t)
            return;
        t->mailbox.push_back(std::move(msg));
        if (t->state == TaskState::BlockedRecv) {
            t->state = TaskState::Ready;
            ready_.push_back(t->id);
        }
    }

    bool ProcScheduler::recvCurrent(ValueNode::ValueType& out) {
        ir::VM* vm = ir::VM::current();
        if (!vm || !vm->isSchedulerManaged())
            return false;
        Task* t = findTaskByVM(vm);
        if (!t)
            return false;
        if (!t->mailbox.empty()) {
            out = std::move(t->mailbox.front());
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
        size_t pumps = 0;
        while (t->state != TaskState::Done) {
            if (++pumps > kMaxPumps)
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
        if (t->hasError)
            return std::string("task failed: " + t->errorMessage);
        return t->result;
    }

    size_t ProcScheduler::pump(std::chrono::steady_clock::time_point now) {
        for (auto& pt : tasks_) {
            if (pt && pt->state == TaskState::Sleeping && pt->wakeupAt <= now) {
                pt->state = TaskState::Ready;
                ready_.push_back(pt->id);
            }
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
            pump();
        }
    }

    void ProcScheduler::reset() {
        tasks_.clear();
        ready_.clear();
        nextId_ = 1;
        clockFn = {};
    }

}
