#include <cmath>
#include <cctype>
#include <chrono>
#include <limits>
#include <ranges>
#include <string>
#include <vector>
#include <algorithm>

#include <ll/api/Expected.h>

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/Unicode.h"

#include "LOICollectionA/utils/core/MathUtils.h"

#include "LOICollectionA/frontend/ir/VM.h"

namespace LOICollection::frontend::ir {

    namespace {
        thread_local std::shared_ptr<sandbox::SandboxBudget> tlsBudget;

        struct BudgetScope {
            std::shared_ptr<sandbox::SandboxBudget> previous;

            explicit BudgetScope(std::shared_ptr<sandbox::SandboxBudget>& budget) : previous(tlsBudget) {
                if (!previous)
                    budget->reset();
                else
                    budget = previous;

                tlsBudget = budget;
            }

            ~BudgetScope() { tlsBudget = std::move(previous); }
        };
    }

    bool VM::isDerived(const BytecodeChunk& chunk, int derivedClassIndex, int baseClassIndex) const {
        if (derivedClassIndex == baseClassIndex) return true;
        if (derivedClassIndex < 0 || derivedClassIndex >= static_cast<int>(chunk.classes.size())) return false;

        const auto& cls = chunk.classes[derivedClassIndex];

        return std::ranges::any_of(cls.ancestorIndices, [baseClassIndex](int idx) -> bool {
            return idx == baseClassIndex;
        });
    }

    const FieldLayoutPtr& VM::classLayout(const BytecodeChunk& chunk, int classIndex) {
        auto it = this->mClassLayouts.find(classIndex);
        if (it != this->mClassLayouts.end())
            return it->second;

        const auto& cls = chunk.classes[static_cast<size_t>(classIndex)];
        return this->mClassLayouts
            .emplace(classIndex, std::make_shared<const FieldLayout>(cls.fieldNames))
            .first->second;
    }

    ValueNode::ValueType VM::run(
        const std::shared_ptr<const BytecodeChunk>& chunk,
        const Context& ctx
    ) {
        if (!chunk) {
            this->diagnostics.addError({ 0, 0, 0 }, "Cannot run a null bytecode chunk");
            return ValueNode::ValueType{};
        }

        this->stack.clear();
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
        const std::shared_ptr<const BytecodeChunk>& owner,
        const CallbackTypePlaces& placeholders
    ) {
        if (!owner) {
            this->diagnostics.addError({ 0, 0, 0 }, "Cannot execute a null bytecode chunk");
            return ValueNode::ValueType{};
        }

        const BytecodeChunk& chunk = *owner;

        BudgetScope budgetScope(this->mBudget);


        while (true) {
            if (this->diagnostics.hasErrors())
                return std::string("");

            if (const auto violation = this->mBudget->tickInstruction();
                violation != sandbox::SandboxBudget::Violation::None) {
                this->failBudget(violation, violation == sandbox::SandboxBudget::Violation::WallTimeLimit
                    ? "Execution timeout"
                    : "Execution budget exhausted");
                return ValueNode::ValueType{};
            }

            Frame& frame = this->frames.back();
            const BytecodeChunk& cur = frame.chunk.get();

            if (frame.ip >= cur.code.size()) {
                this->diagnostics.addError(this->currentLoc, "Invalid instruction pointer");
                return ValueNode::ValueType{};
            }

            const auto& instr = cur.code[frame.ip++];
            this->currentLoc = instr.loc;
            ExecArgs s{ owner, chunk, cur, frame, instr, placeholders };
            switch (instr.op) {
                case OpCode::PUSH_INT: case OpCode::PUSH_FLOAT: case OpCode::PUSH_BOOL: case OpCode::PUSH_NONE: case OpCode::PUSH_STR: this->execPushConst(s); break;
                case OpCode::POP: case OpCode::DUP: case OpCode::DUP2: case OpCode::ROT3: case OpCode::SWAP2: this->execStackManip(s); break;
                case OpCode::IS_NONE: case OpCode::UNWRAP: case OpCode::TYPE_OF: case OpCode::HAS_VALUE: case OpCode::DUP_IS_NONE: this->execOptional(s); break;
                case OpCode::LOAD_SLOT: case OpCode::STORE_SLOT: case OpCode::DUP_STORE_SLOT: this->execLocalSlot(s); break;
                case OpCode::LOAD_VAR: case OpCode::STORE_VAR: case OpCode::DUP_STORE: this->execVariable(s); break;
                case OpCode::LOAD_FIELD: case OpCode::STORE_FIELD: case OpCode::LOAD_FIELD_SLOT: case OpCode::STORE_FIELD_SLOT: case OpCode::BIND_THIS: case OpCode::LOAD_LEN: this->execFieldAccess(s); break;
                case OpCode::MAKE_ARRAY: case OpCode::LOAD_INDEX: case OpCode::STORE_INDEX: this->execArray(s); break;
                case OpCode::MAKE_LAMBDA: case OpCode::LOAD_THIS: this->execClosure(s); break;
                case OpCode::INSTANCEOF: this->execInstanceof(s); break;
                case OpCode::NEW: case OpCode::NEW_NATIVE: this->execObjectCreate(s); break;
                case OpCode::CALL_METHOD: case OpCode::CALL_METHOD_VIRTUAL: case OpCode::CALL_METHOD_BY_NAME: case OpCode::CALL_SUPER_CTOR: this->execMethodDispatch(s); break;
                case OpCode::CALL_NATIVE_METHOD: this->execNativeCall(s); break;
                case OpCode::CALL_FUNC: case OpCode::CALL_LAMBDA: this->execFunctionCall(s); break;
                case OpCode::ADD: case OpCode::SUB: case OpCode::MUL: case OpCode::DIV: case OpCode::MOD: case OpCode::POW:
                case OpCode::ADD_I: case OpCode::SUB_I: case OpCode::MUL_I: case OpCode::MOD_I:
                    this->execArithmetic(s); break;
                case OpCode::CMP_EQ: case OpCode::CMP_NE: case OpCode::CMP_GT: case OpCode::CMP_LT: case OpCode::CMP_GE: case OpCode::CMP_LE:
                case OpCode::CMP_EQ_I: case OpCode::CMP_NE_I: case OpCode::CMP_GT_I: case OpCode::CMP_LT_I: case OpCode::CMP_GE_I: case OpCode::CMP_LE_I:
                    this->execComparison(s); break;
                case OpCode::LOGIC_AND: case OpCode::LOGIC_OR: case OpCode::NEG: case OpCode::NOT: case OpCode::NEG_I: this->execLogic(s); break;
                case OpCode::CALL: case OpCode::CALL_MACRO: this->execHostCall(s); break;
                case OpCode::JMP_IF_FALSE: case OpCode::JMP_IF_TRUE: case OpCode::JMP: this->execBranch(s); break;
                case OpCode::RETURN: {
                    auto result = this->pop();

                    Frame finished = std::move(this->frames.back());
                    this->frames.pop_back();
                    this->localPool.resize(finished.localsBase);

                    if (this->frames.empty())
                        return result;

                    if (finished.hasPending)
                        this->push(finished.pendingPush);
                    else
                        this->push(result);

                    break;
            }
                case OpCode::HALT: {
                    if (this->stack.empty()) {
                        return std::string("");
                    }

                    return this->stack.back();
            }
                default:
                    this->diagnostics.addError(this->currentLoc, "Unknown opcode");
                    break;
            }
        }
    }

    void VM::failBudget(sandbox::SandboxBudget::Violation violation, const std::string& message) {
        this->mReport.violation = violation;
        this->diagnostics.addError(this->currentLoc, message);
    }

    void VM::execBranch(ExecArgs& s) {
        const auto& instr = s.instr;
        Frame& frame = s.frame;
        switch (instr.op) {
            case OpCode::JMP_IF_FALSE: {
                auto cond = this->pop();
                if (!VM::valueToBool(cond))
                    frame.ip += instr.operand;
            } break;
            case OpCode::JMP_IF_TRUE: {
                auto cond = this->pop();
                if (VM::valueToBool(cond))
                    frame.ip += instr.operand;
            } break;
            case OpCode::JMP: {
                frame.ip += instr.operand;
            } break;
            default: break;
        }
    }

    ValueNode::ValueType VM::callFunctionRef(
        const FunctionRefPtr& func,
        const CallbackTypeValues& args,
        const CallbackTypePlaces& placeholders,
        DiagnosticEngine& diagnostics
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

        auto snapshot = func->globals.lock();
        VM vm(diagnostics, snapshot
            ? std::make_shared<GlobalsTable>(*snapshot)
            : std::make_shared<GlobalsTable>());

        vm.stack.clear();
        vm.frames.clear();

        Frame callee(*func->owner->methodBodies[func->bodyIndex]);
        callee.hasThis = func->hasThis;
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
        return vm.execute(func->owner, placeholders);
    }

}
