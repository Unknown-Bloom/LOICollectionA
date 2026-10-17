#include <cmath>
#include <cctype>
#include <chrono>
#include <limits>
#include <memory>
#include <mutex>
#include <ranges>
#include <string>
#include <utility>
#include <vector>
#include <algorithm>

#include <ll/api/Expected.h>

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/Context.h"
#include "LOICollectionA/frontend/Unicode.h"

#include "LOICollectionA/utils/core/MathUtils.h"

#include "LOICollectionA/frontend/ir/VM.h"
#include "LOICollectionA/frontend/runtime/ScriptVMRegistry.h"

namespace LOICollection::frontend::ir::detail {
    thread_local std::shared_ptr<sandbox::SandboxBudget> tlsBudget;
    thread_local VM* tlsCurrentVM = nullptr;

    struct BudgetScope {
        std::shared_ptr<sandbox::SandboxBudget> previous;
        std::shared_ptr<sandbox::SandboxBudget> budget;

        explicit BudgetScope(std::shared_ptr<sandbox::SandboxBudget>& b) : previous(tlsBudget), budget(b) {
            if (!previous)
                budget->reset();
            else
                budget = previous;

            tlsBudget = budget;
            budget->beginWall();
        }

        ~BudgetScope() {
            budget->endWall();
            tlsBudget = std::move(previous);
        }
    };

    /* Pins tlsCurrentVM to the VM executing on this thread for the duration of
     * one execute() call, restoring the previous value on exit so nested
     * (host-driven) VM entry within a task does not clobber it. */
    struct CurrentVMScope {
        VM* previous;

        explicit CurrentVMScope(VM* vm) : previous(tlsCurrentVM) { tlsCurrentVM = vm; }
        ~CurrentVMScope() { tlsCurrentVM = previous; }
    };
}

namespace LOICollection::frontend::ir {

    using namespace detail;

    bool VM::isDerived(const MirChunk& chunk, int derivedClassIndex, int baseClassIndex) const {
        if (derivedClassIndex == baseClassIndex) return true;
        if (derivedClassIndex < 0 || derivedClassIndex >= static_cast<int>(chunk.classes.size())) return false;

        const auto& cls = chunk.classes[derivedClassIndex];

        if (baseClassIndex >= 0 && baseClassIndex < 64)
            return ((cls.ancestorMask >> baseClassIndex) & 1u) != 0;

        return std::ranges::any_of(cls.ancestorIndices, [baseClassIndex](int idx) -> bool {
            return idx == baseClassIndex;
        });
    }

    const FieldLayoutPtr& VM::classLayout(const MirChunk& chunk, int classIndex) {
        auto it = this->mCache->classLayouts.find(classIndex);
        if (it != this->mCache->classLayouts.end())
            return it->second;

        const auto& cls = chunk.classes[static_cast<size_t>(classIndex)];
        return this->mCache->classLayouts
            .emplace(classIndex, std::make_shared<const FieldLayout>(cls.fieldNames))
            .first->second;
    }

    ValueNode::ValueType& VM::regOf(Frame& frame, int index) {
        if (index >= 0 && static_cast<size_t>(index) < frame.localsSize)
            return this->localPool[frame.localsBase + static_cast<size_t>(index)];

        this->diagnostics.addError(this->currentLoc, "Register index out of range");
        return this->deadReg;
    }

    const ValueNode::ValueType& VM::regOf(const Frame& frame, int index) {
        if (index >= 0 && static_cast<size_t>(index) < frame.localsSize)
            return this->localPool[frame.localsBase + static_cast<size_t>(index)];

        this->diagnostics.addError(this->currentLoc, "Register index out of range");
        return this->deadReg;
    }

    void VM::setReg(Frame& frame, int index, ValueNode::ValueType value) {
        if (index < 0)
            return;

        this->regOf(frame, index) = std::move(value);
    }

    std::vector<ValueNode::ValueType> VM::collectArgs(const Frame& frame, int base, int count) {
        std::vector<ValueNode::ValueType> args;
        if (count <= 0)
            return args;

        args.reserve(static_cast<size_t>(count));

        for (int i = 0; i < count; ++i)
            args.push_back(this->regOf(frame, base + i));

        return args;
    }

    void VM::placeArgs(std::size_t sourceBase, int count, std::size_t offset) {
        const size_t base = this->frames.back().localsBase + offset;
        for (int i = 0; i < count; ++i) {
            const size_t index = static_cast<size_t>(i);
            this->localPool[base + index] = std::move(this->localPool[sourceBase + index]);
        }
    }

    ValueNode::ValueType VM::run(
        const std::shared_ptr<const MirChunk>& chunk,
        const Context& ctx
    ) {
        if (!chunk) {
            this->diagnostics.addError({ 0, 0, 0 }, "Cannot run a null bytecode chunk");
            return ValueNode::ValueType{};
        }

        this->frames.clear();
        this->localPool.clear();
        this->globals->clear();
        this->mReport = sandbox::SandboxReport{};

        for (const auto& cls : chunk->classes) {
            for (size_t i = 0; i < cls.staticFieldNames.size(); ++i) {
                (*this->globals)[cls.name + "::" + cls.staticFieldNames[i]] =
                    cls.staticHasDefault[i]
                        ? VM::cloneValue(cls.staticDefaults[i])
                        : ValueNode::ValueType{};
            }
        }

        this->frames.emplace_back(*chunk);
        this->localPool.resize(chunk->slotCount);

        CallbackTypePlaces placeholders = ctx.params;
        if (!ctx.scriptId.empty())
            placeholders[Context::kScriptIdKey] = ctx.scriptId;

        const auto startedAt = std::chrono::steady_clock::now();
        ValueNode::ValueType result = this->execute(chunk, placeholders);

        this->mReport.executedInstructions = this->mBudget->executedInstructions;
        this->mReport.nativeCallCount = this->mBudget->nativeCallCount;
        this->mReport.collectedObjects = this->mCollector.collect();
        this->mReport.objectCount = this->mBudget->objectCount;
        this->mReport.allocatedBytes = this->mBudget->allocatedBytes;
        this->mReport.wallTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt
        );
        this->mReport.hasErrors = this->diagnostics.hasErrors();
        if (this->mReport.hasErrors)
            this->mReport.errorMessage = this->diagnostics.getErrorMessage();

        return result;
    }

    ValueNode::ValueType VM::execute(
        const std::shared_ptr<const MirChunk>& owner,
        const CallbackTypePlaces& placeholders
    ) {
        if (!owner) {
            this->diagnostics.addError({ 0, 0, 0 }, "Cannot execute a null bytecode chunk");
            mOutcome = RunOutcome::Failed;
            return ValueNode::ValueType{};
        }

        if (this->mCacheChunk != owner.get()) {
            this->mCacheChunk = owner.get();
            this->mCache = ChunkCacheRegistry::getInstance().forChunk(owner.get());
        }

        if (this->frames.capacity() < this->mBudget->maxFrames)
            this->frames.reserve(this->mBudget->maxFrames);

        BudgetScope budgetScope(this->mBudget);
        CurrentVMScope vmScope(this);

        if (this->mSchedulerManaged)
            return this->executeLoop<true>(owner, placeholders);

        return this->executeLoop<false>(owner, placeholders);
    }

    template <bool Managed>
    ValueNode::ValueType VM::executeLoop(
        const std::shared_ptr<const MirChunk>& owner,
        const CallbackTypePlaces& placeholders
    ) {
        const MirChunk& chunk = *owner;

        while (true) {
            if (this->diagnostics.hasErrors()) {
                mOutcome = RunOutcome::Failed;
                return std::string("");
            }

            if (const auto violation = this->mBudget->tickInstruction();
                violation != sandbox::SandboxBudget::Violation::None) {
                this->failBudget(violation, violation == sandbox::SandboxBudget::Violation::WallTimeLimit
                    ? "Execution timeout"
                    : "Execution budget exhausted");
                mOutcome = RunOutcome::Failed;
                return ValueNode::ValueType{};
            }

            if ((this->mBudget->executedInstructions & 1023u) == 0
                && (this->mCollector.collecting()
                    || this->mBudget->objectCount >= this->mBudget->maxObjectCount / 2))
                this->mCollector.step(1024);

            Frame& frame = this->frames.back();
            const MirChunk& cur = frame.chunk.get();

            if (frame.ip >= cur.code.size()) {
                this->diagnostics.addError(this->currentLoc, "Invalid instruction pointer");
                return ValueNode::ValueType{};
            }

            const auto& instr = cur.code[frame.ip++];
            this->currentLoc = instr.loc;
            ExecArgs s{ owner, chunk, cur, frame, instr, placeholders };
            switch (instr.op) {
                case MirOp::LOAD_CONST: this->execLoadConst(s); break;
                case MirOp::MOVE: this->execMove(s); break;
                case MirOp::UNWRAP: case MirOp::TYPE_OF: case MirOp::HAS_VALUE: case MirOp::IS_NONE: this->execOptional(s); break;
                case MirOp::LOAD_SLOT: case MirOp::STORE_SLOT: this->execLocalSlot(s); break;
                case MirOp::LOAD_VAR: case MirOp::STORE_VAR: this->execVariable(s); break;
                case MirOp::LOAD_FIELD: case MirOp::STORE_FIELD: case MirOp::LOAD_FIELD_SLOT: case MirOp::STORE_FIELD_SLOT: case MirOp::BIND_THIS: case MirOp::LOAD_LEN: this->execFieldAccess(s); break;
                case MirOp::MAKE_ARRAY: case MirOp::LOAD_INDEX: case MirOp::STORE_INDEX: this->execArray(s); break;
                case MirOp::MAKE_LAMBDA: case MirOp::LOAD_THIS: this->execClosure(s); break;
                case MirOp::INSTANCEOF: this->execInstanceof(s); break;
                case MirOp::NEW: case MirOp::NEW_NATIVE: this->execObjectCreate(s); break;
                case MirOp::CALL_METHOD: case MirOp::CALL_METHOD_VIRTUAL: case MirOp::CALL_METHOD_BY_NAME: case MirOp::CALL_SUPER_CTOR: this->execMethodDispatch(s); break;
                case MirOp::CALL_NATIVE_METHOD: this->execNativeCall(s); break;
                case MirOp::CALL_FUNC: case MirOp::CALL_LAMBDA: this->execFunctionCall(s); break;
                case MirOp::ADD: case MirOp::SUB: case MirOp::MUL: case MirOp::DIV: case MirOp::MOD: case MirOp::POW:
                    this->execArithmetic(s); break;
                case MirOp::CMP_EQ: case MirOp::CMP_NE: case MirOp::CMP_GT: case MirOp::CMP_LT: case MirOp::CMP_GE: case MirOp::CMP_LE:
                    this->execComparison(s); break;
                case MirOp::LOGIC_AND: case MirOp::LOGIC_OR: case MirOp::NEG: case MirOp::NOT: this->execLogic(s); break;
                case MirOp::CALL: case MirOp::CALL_MACRO: this->execHostCall(s); break;
                case MirOp::JMP_IF_FALSE: case MirOp::JMP_IF_TRUE: case MirOp::JMP: case MirOp::JMP_IF_CMP_FALSE: this->execBranch(s); break;
                case MirOp::RETURN: {
                    auto result = instr.src1 >= 0
                        ? this->regOf(frame, instr.src1)
                        : ValueNode::ValueType(std::string(""));

                    Frame finished = std::move(this->frames.back());
                    this->frames.pop_back();
                    this->localPool.resize(finished.localsBase);

                    if (this->frames.empty()) {
                        mOutcome = RunOutcome::Completed;
                        return result;
                    }

                    if (finished.hasPending)
                        result = std::move(finished.pendingPush);

                    if (finished.returnReg >= 0)
                        this->regOf(this->frames.back(), finished.returnReg) = std::move(result);

                    break;
                }
                case MirOp::HALT:
                    mOutcome = RunOutcome::Completed;
                    return this->regOf(this->frames.back(), 0);
                case MirOp::PANIC: {
                    std::string message = "panic";
                    if (instr.src1 >= 0) {
                        const ValueNode::ValueType& value = this->regOf(frame, instr.src1);
                        const std::string rendered = VM::valueToString(value);
                        if (!rendered.empty())
                            message += ": " + rendered;
                    }

                    this->diagnostics.addError(instr.loc, message);
                    mOutcome = RunOutcome::Failed;
                    return std::string("");
                }
                default:
                    this->diagnostics.addError(this->currentLoc, "Unknown opcode");
                    break;
            }

            if constexpr (Managed) {
                if (mStopRequest != StopKind::None || --mSliceRemaining == 0) {
                    switch (mStopRequest) {
                        case StopKind::Yield:
                            mOutcome = RunOutcome::Yielded;
                            mBlockInfo.kind = BlockKind::None;
                            break;
                        case StopKind::Sleep:
                            mOutcome = RunOutcome::Blocked;
                            mBlockInfo.kind = BlockKind::Sleep;
                            mBlockInfo.wakeupAt = mWakeupAt;
                            break;
                        case StopKind::Recv:
                            mOutcome = RunOutcome::Blocked;
                            mBlockInfo.kind = BlockKind::Recv;
                            this->frames.back().ip -= 1;  // replay the recv call on resume
                            break;
                        case StopKind::Join:
                            mOutcome = RunOutcome::Blocked;
                            mBlockInfo.kind = BlockKind::Join;
                            mBlockInfo.joinTarget = mJoinTarget;
                            this->frames.back().ip -= 1;  // replay the join call on resume
                            break;
                        case StopKind::None:
                        case StopKind::Slice:
                            mOutcome = RunOutcome::SliceExpired;
                            mBlockInfo.kind = BlockKind::None;
                            break;
                    }
                    return ValueNode::ValueType{};
                }
            }
        }
    }

    void VM::reset() {
        this->frames.clear();
        this->localPool.clear();
        this->mReport = sandbox::SandboxReport{};
        this->currentLoc = SourceLocation{};
    }

    void VM::failBudget(sandbox::SandboxBudget::Violation violation, const std::string& message) {
        this->mReport.violation = violation;
        this->diagnostics.addError(this->currentLoc, message);
    }

    void VM::execLoadConst(ExecArgs& s) {
        const auto& instr = s.instr;
        const MirChunk& cur = s.cur;
        Frame& frame = s.frame;

        const auto& value = cur.constants[instr.operand];

        if (const auto* text = std::get_if<std::string>(&value)) {
            if (const auto violation = this->mBudget->accountString(text->size());
                violation != sandbox::SandboxBudget::Violation::None) {
                this->failBudget(violation, "String size budget exhausted");
                return;
            }

            this->setReg(frame, instr.dst, *text);
            return;
        }

        ValueNode::ValueType copy = VM::cloneValue(value);

        if (cur.hasRefStores) {
            const auto* array = std::get_if<ArrayRef>(&copy);
            if (array && std::any_of((*array)->elements.begin(), (*array)->elements.end(), CycleCollector::holdsReference))
                this->mCollector.track(*array);
        }

        this->setReg(frame, instr.dst, std::move(copy));
    }

    void VM::execMove(ExecArgs& s) {
        const auto& instr = s.instr;

        if (instr.dst == instr.src1)
            return;

        auto value = this->regOf(s.frame, instr.src1);
        this->setReg(s.frame, instr.dst, std::move(value));
    }

    void VM::execBranch(ExecArgs& s) {
        const auto& instr = s.instr;
        Frame& frame = s.frame;
        switch (instr.op) {
            case MirOp::JMP_IF_FALSE: {
                if (!VM::valueToBool(this->regOf(frame, instr.src1)))
                    frame.ip += instr.operand;
            } break;
            case MirOp::JMP_IF_TRUE: {
                if (VM::valueToBool(this->regOf(frame, instr.src1)))
                    frame.ip += instr.operand;
            } break;
            case MirOp::JMP: {
                frame.ip += instr.operand;
            } break;
            case MirOp::JMP_IF_CMP_FALSE: {
                if (!VM::applyComparison(
                        this->regOf(frame, instr.src1), this->regOf(frame, instr.src2),
                        static_cast<MirOp>(instr.imm), this->diagnostics, this->currentLoc))
                    frame.ip += instr.operand;
            } break;
            default: break;
        }
    }

    ValueNode::ValueType VM::callFunctionRef(
        const FunctionRefPtr& func,
        const CallbackTypeValues& args,
        const CallbackTypePlaces& placeholders,
        DiagnosticEngine& diagnostics,
        const sandbox::SandboxBudget* budget
    ) {
        if (!func) {
            diagnostics.addError({ 0, 0, 0 }, "Cannot call a null function reference");
            return ValueNode::ValueType{};
        }

        if (!func->owner) {
            diagnostics.addError({ 0, 0, 0 }, "Function reference has no owning bytecode chunk");
            return ValueNode::ValueType{};
        }

        if (func->bodyIndex < 0 ||
            func->bodyIndex >= static_cast<int>(func->owner->methodBodies.size())) {
            diagnostics.addError({ 0, 0, 0 }, "Invalid function body index");
            return ValueNode::ValueType{};
        }

        if (static_cast<int>(args.size()) != func->argCount) {
            diagnostics.addError({ 0, 0, 0 },
                "Function expects " + std::to_string(func->argCount) +
                " argument(s), got " + std::to_string(args.size()));
            return ValueNode::ValueType{};
        }

        static thread_local size_t nativeCallDepth = 0;
        if (nativeCallDepth >= 64) {
            diagnostics.addError({ 0, 0, 0 }, "Nested native script call limit exceeded");
            return ValueNode::ValueType{};
        }

        ++nativeCallDepth;
        struct CallDepthGuard {
            size_t& depth;
            ~CallDepthGuard() { --depth; }
        } depthGuard{ nativeCallDepth };

        auto globals = func->globals.lock();
        if (!globals) {
            diagnostics.addError({ 0, 0, 0 },
                "Script context expired: the callback was invoked after its script instance was released");
            return ValueNode::ValueType{};
        }

        const std::string scriptId = Context::scriptIdOf(placeholders);
        const std::string ownerId = Context::ownerIdOf(placeholders);

        std::shared_ptr<runtime::ScriptVM> pooled;
        std::unique_lock<std::mutex> pooledGuard;
        std::unique_ptr<VM> standalone;

        VM* target = nullptr;
        if (!scriptId.empty()) {
            pooled = runtime::ScriptVMRegistry::getInstance().tryGetOrCreate(scriptId, ownerId);
            pooledGuard = pooled->lock();
            pooled->diagnostics().clear();
            pooled->vm().reset();
            pooled->vm().setGlobals(std::move(globals));

            if (budget)
                pooled->vm().setBudget(*budget);

            target = &pooled->vm();
        } else {
            standalone = std::make_unique<VM>(diagnostics, std::move(globals));

            if (budget)
                standalone->setBudget(*budget);

            target = standalone.get();
        }

        VM& vm = *target;

        struct FrameLimitGuard {
            std::shared_ptr<sandbox::SandboxBudget> budget;
            ~FrameLimitGuard() { if (budget) --budget->activeFrames; }
        } frameGuard;

        if (tlsBudget) {
            if (tlsBudget->activeFrames >= tlsBudget->maxFrames) {
                diagnostics.addError({}, "Call stack depth limit exceeded");
                return ValueNode::ValueType{};
            }

            ++tlsBudget->activeFrames;
            frameGuard.budget = tlsBudget;
        }

        vm.frames.clear();

        Frame callee(*func->owner->methodBodies[func->bodyIndex]);
        callee.hasThis = func->hasThis;
        callee.returnReg = -1;
        if (func->hasThis) {
            auto self = func->thisObj.lock();
            if (!self) {
                diagnostics.addError({}, "Method receiver has been released");
                return std::monostate{};
            }

            callee.thisObj = self;
        }

        const size_t paramBase = func->captures.size();
        if (paramBase + static_cast<size_t>(func->argCount) > callee.localsSize) {
            diagnostics.addError({}, "Lambda frame is too small for its parameters");
            return std::monostate{};
        }

        vm.localPool.resize(callee.localsSize);
        std::copy_n(func->captures.begin(), paramBase, vm.localPool.begin());

        for (int i = 0; i < func->argCount; ++i)
            vm.localPool[paramBase + i] = args[i];

        vm.frames.push_back(std::move(callee));

        auto result = vm.execute(func->owner, placeholders);
        if (pooled && pooled->diagnostics().hasErrors())
            diagnostics.addError({}, pooled->diagnostics().getErrorMessage());

        return result;
    }

    void VM::setupRootFrame(const FunctionRefPtr& func, const CallbackTypeValues& args) {
        this->frames.clear();
        this->localPool.clear();

        Frame callee(*func->owner->methodBodies[func->bodyIndex]);
        callee.hasThis = func->hasThis;
        callee.returnReg = -1;

        if (func->hasThis) {
            auto self = func->thisObj.lock();
            if (!self) {
                this->diagnostics.addError({}, "Method receiver has been released");
                return;
            }
            callee.thisObj = self;
        }

        const size_t paramBase = func->captures.size();
        if (paramBase + static_cast<size_t>(func->argCount) > callee.localsSize) {
            this->diagnostics.addError({}, "Lambda frame is too small for its parameters");
            return;
        }

        this->localPool.resize(callee.localsSize);
        std::copy_n(func->captures.begin(), paramBase, this->localPool.begin());
        const size_t n = std::min<size_t>(args.size(), static_cast<size_t>(func->argCount));
        for (size_t i = 0; i < n; ++i)
            this->localPool[paramBase + i] = args[i];

        this->frames.push_back(std::move(callee));
    }

    VM::RunOutcome VM::runTask(const FunctionRefPtr& func, const CallbackTypeValues& args, size_t sliceInstructions) {
        if (!func || !func->owner) {
            mOutcome = RunOutcome::Failed;
            return mOutcome;
        }

        if (func->bodyIndex < 0 ||
            func->bodyIndex >= static_cast<int>(func->owner->methodBodies.size())) {
            mOutcome = RunOutcome::Failed;
            return mOutcome;
        }

        if (this->frames.empty()) {
            this->globals->clear();
            for (const auto& cls : func->owner->classes) {
                for (size_t i = 0; i < cls.staticFieldNames.size(); ++i) {
                    (*this->globals)[cls.name + "::" + cls.staticFieldNames[i]] =
                        cls.staticHasDefault[i]
                            ? VM::cloneValue(cls.staticDefaults[i])
                            : ValueNode::ValueType{};
                }
            }

            this->setupRootFrame(func, args);
            if (this->diagnostics.hasErrors()) {
                mOutcome = RunOutcome::Failed;
                return mOutcome;
            }
        }

        mSchedulerManaged = true;
        mSliceRemaining = sliceInstructions;
        mStopRequest = StopKind::None;
        mBlockInfo = TaskEvent{};
        mOutcome = RunOutcome::Completed;

        auto prevTlsBudget = tlsBudget;
        tlsBudget = this->mBudget;
        const auto result = this->execute(func->owner, CallbackTypePlaces{});
        tlsBudget = prevTlsBudget;

        mTaskResult = result;

        if (mOutcome == RunOutcome::Completed || mOutcome == RunOutcome::Failed
            || this->mBudget->objectCount >= this->mBudget->maxObjectCount / 4)
            (void)this->mCollector.collect();

        return mOutcome;
    }

}
