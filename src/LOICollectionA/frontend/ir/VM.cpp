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

    bool VM::isDerived(const MirChunk& chunk, int derivedClassIndex, int baseClassIndex) const {
        if (derivedClassIndex == baseClassIndex) return true;
        if (derivedClassIndex < 0 || derivedClassIndex >= static_cast<int>(chunk.classes.size())) return false;

        const auto& cls = chunk.classes[derivedClassIndex];

        return std::ranges::any_of(cls.ancestorIndices, [baseClassIndex](int idx) -> bool {
            return idx == baseClassIndex;
        });
    }

    const FieldLayoutPtr& VM::classLayout(const MirChunk& chunk, int classIndex) {
        auto it = this->mClassLayouts.find(classIndex);
        if (it != this->mClassLayouts.end())
            return it->second;

        const auto& cls = chunk.classes[static_cast<size_t>(classIndex)];
        return this->mClassLayouts
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

    void VM::placeArgs(std::vector<ValueNode::ValueType>&& args, size_t offset) {
        const size_t base = this->frames.back().localsBase + offset;
        for (size_t i = 0; i < args.size(); ++i)
            this->localPool[base + i] = std::move(args[i]);
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
            return ValueNode::ValueType{};
        }

        const MirChunk& chunk = *owner;

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
                case MirOp::JMP_IF_FALSE: case MirOp::JMP_IF_TRUE: case MirOp::JMP: this->execBranch(s); break;
                case MirOp::RETURN: {
                    auto result = instr.src1 >= 0
                        ? this->regOf(frame, instr.src1)
                        : ValueNode::ValueType(std::string(""));

                    Frame finished = std::move(this->frames.back());
                    this->frames.pop_back();
                    this->localPool.resize(finished.localsBase);

                    if (this->frames.empty())
                        return result;

                    if (finished.hasPending)
                        result = std::move(finished.pendingPush);

                    if (finished.returnReg >= 0)
                        this->regOf(this->frames.back(), finished.returnReg) = std::move(result);

                    break;
                }
                case MirOp::HALT:
                    return this->regOf(this->frames.back(), 0);
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

        this->setReg(frame, instr.dst, VM::cloneValue(value));
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
        return vm.execute(func->owner, placeholders);
    }

}
