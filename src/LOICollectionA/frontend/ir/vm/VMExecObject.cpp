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

    int VM::resolveFieldSlot(Object& obj, const std::string& name, const MirInstr& instr) {
        if (!obj.layout)
            obj.adoptLayout();

        FieldCacheSlot& cached = this->mFieldSlots[&instr];

        if (cached.name != name || cached.layout != obj.layout.get()) {
            cached.name = name;
            cached.layout = obj.layout.get();
            cached.slot = obj.slotOf(name);
        }

        return cached.slot;
    }

    void VM::execFieldAccess(ExecArgs& s) {
        const auto& instr = s.instr;
        const MirChunk& cur = s.cur;
        Frame& frame = s.frame;
        switch (instr.op) {
            case MirOp::LOAD_FIELD: {
                const auto& name = std::get<std::string>(cur.constants[instr.operand]);
                const ValueNode::ValueType& objValue = this->regOf(frame, instr.src1);

                if (std::holds_alternative<ArrayRef>(objValue)) {
                    this->diagnostics.addError(this->currentLoc, "Array has no field: " + name);
                    break;
                }

                if (std::holds_alternative<std::string>(objValue)) {
                    this->diagnostics.addError(this->currentLoc, "String has no field: " + name);
                    break;
                }

                if (!std::holds_alternative<ObjectRef>(objValue)) {
                    this->diagnostics.addError(this->currentLoc, "Cannot load field '" + name + "' from a non-object value");
                    break;
                }

                auto obj = std::get<ObjectRef>(objValue);
                const auto* field = obj->fieldAt(this->resolveFieldSlot(*obj, name, instr), name);
                if (!field) {
                    this->diagnostics.addError(this->currentLoc, "Object has no field: " + name);
                    break;
                }

                this->setReg(frame, instr.dst, *field);
            } break;
            case MirOp::STORE_FIELD: {
                const auto& name = std::get<std::string>(cur.constants[instr.operand]);
                const ValueNode::ValueType& objValue = this->regOf(frame, instr.src1);

                if (!std::holds_alternative<ObjectRef>(objValue)) {
                    this->diagnostics.addError(this->currentLoc, "Cannot store field '" + name + "' on a non-object value");
                    break;
                }

                auto obj = std::get<ObjectRef>(objValue);
                obj->fieldAtOrSpill(this->resolveFieldSlot(*obj, name, instr), name) =
                    this->regOf(frame, instr.src2);
            } break;
            case MirOp::LOAD_FIELD_SLOT: {
                const ValueNode::ValueType& objValue = this->regOf(frame, instr.src1);

                if (!std::holds_alternative<ObjectRef>(objValue)) {
                    this->diagnostics.addError(this->currentLoc, "Cannot load a field from a non-object value");
                    break;
                }

                auto obj = std::get<ObjectRef>(objValue);
                if (instr.operand >= 0 && static_cast<size_t>(instr.operand) >= obj->slots.size())
                    obj->adoptLayout();

                if (instr.operand < 0 || static_cast<size_t>(instr.operand) >= obj->slots.size()) {
                    this->diagnostics.addError(this->currentLoc, "Field slot index out of range");
                    break;
                }

                this->setReg(frame, instr.dst, obj->slots[static_cast<size_t>(instr.operand)]);
            } break;
            case MirOp::STORE_FIELD_SLOT: {
                const ValueNode::ValueType& objValue = this->regOf(frame, instr.src1);

                if (!std::holds_alternative<ObjectRef>(objValue)) {
                    this->diagnostics.addError(this->currentLoc, "Cannot store a field on a non-object value");
                    break;
                }

                auto obj = std::get<ObjectRef>(objValue);
                if (instr.operand >= 0 && static_cast<size_t>(instr.operand) >= obj->slots.size())
                    obj->adoptLayout();

                if (instr.operand < 0 || static_cast<size_t>(instr.operand) >= obj->slots.size()) {
                    this->diagnostics.addError(this->currentLoc, "Field slot index out of range");
                    break;
                }

                obj->slots[static_cast<size_t>(instr.operand)] = this->regOf(frame, instr.src2);
            } break;
            case MirOp::BIND_THIS: {
                const ValueNode::ValueType& selfValue = this->regOf(frame, instr.src2);
                const ValueNode::ValueType& slot = this->regOf(frame, instr.src1);

                const auto* self = std::get_if<ObjectRef>(&selfValue);
                if (!self || !*self)
                    break;

                const auto* func = std::get_if<FunctionRefPtr>(&slot);
                if (!func || !*func)
                    break;

                (*func)->hasThis = true;
                (*func)->thisObj = *self;

                for (auto& captured : (*func)->captures) {
                    if (const auto* held = std::get_if<ObjectRef>(&captured); held && *held == *self)
                        captured = std::monostate{};
                }
            } break;
            case MirOp::LOAD_LEN: {
                const ValueNode::ValueType& iterable = this->regOf(frame, instr.src1);

                if (std::holds_alternative<ArrayRef>(iterable)) {
                    this->setReg(frame, instr.dst,
                        static_cast<int>(std::get<ArrayRef>(iterable)->elements.size()));
                    break;
                }

                if (std::holds_alternative<std::string>(iterable)) {
                    this->setReg(frame, instr.dst,
                        static_cast<int>(codepointCount(std::get<std::string>(iterable))));
                    break;
                }

                this->diagnostics.addError(this->currentLoc, "Cannot take length of a non-iterable value");
            } break;
            default: break;
        }
    }

    void VM::execArray(ExecArgs& s) {
        const auto& instr = s.instr;
        Frame& frame = s.frame;
        switch (instr.op) {
            case MirOp::MAKE_ARRAY: {
                const int count = instr.operand;
                if (count < 0) {
                    this->diagnostics.addError(this->currentLoc, "Invalid array literal size");
                    break;
                }

                if (const auto violation = this->mBudget->accountArray(static_cast<std::size_t>(count));
                    violation != sandbox::SandboxBudget::Violation::None) {
                    this->failBudget(violation, "Array size budget exhausted");
                    break;
                }

                auto arr = std::make_shared<ArrayValue>();
                arr->elements.resize(count);
                for (int i = 0; i < count; ++i)
                    arr->elements[i] = this->regOf(frame, instr.src1 + i);

                this->setReg(frame, instr.dst, arr);
            } break;
            case MirOp::LOAD_INDEX: {
                const ValueNode::ValueType& indexValue = this->regOf(frame, instr.src2);
                const ValueNode::ValueType& targetValue = this->regOf(frame, instr.src1);

                if (!std::holds_alternative<int>(indexValue)) {
                    this->diagnostics.addError(this->currentLoc, "Index must be an int");
                    break;
                }

                int index = std::get<int>(indexValue);

                if (const auto* text = std::get_if<std::string>(&targetValue)) {
                    if (index < 0) {
                        this->diagnostics.addError(this->currentLoc, "String index out of range");
                        break;
                    }

                    auto character = codepointAt(*text, static_cast<size_t>(index));
                    if (!character) {
                        this->diagnostics.addError(this->currentLoc, "String index out of range");
                        break;
                    }

                    this->setReg(frame, instr.dst, std::move(*character));
                    break;
                }

                if (!std::holds_alternative<ArrayRef>(targetValue)) {
                    this->diagnostics.addError(this->currentLoc, "Cannot index a non-array value");
                    break;
                }

                auto arr = std::get<ArrayRef>(targetValue);
                if (index < 0 || index >= static_cast<int>(arr->elements.size())) {
                    this->diagnostics.addError(this->currentLoc, "Array index out of range");
                    break;
                }

                this->setReg(frame, instr.dst, arr->elements[index]);
            } break;
            case MirOp::STORE_INDEX: {
                const ValueNode::ValueType& targetValue = this->regOf(frame, instr.src1);

                if (!std::holds_alternative<ArrayRef>(targetValue)) {
                    this->diagnostics.addError(this->currentLoc, "Cannot index a non-array value");
                    break;
                }

                const ValueNode::ValueType& indexValue = this->regOf(frame, instr.src2);
                if (!std::holds_alternative<int>(indexValue)) {
                    this->diagnostics.addError(this->currentLoc, "Array index must be an int");
                    break;
                }

                auto arr = std::get<ArrayRef>(targetValue);
                int index = std::get<int>(indexValue);
                if (index < 0 || index > static_cast<int>(arr->elements.size())) {
                    this->diagnostics.addError(this->currentLoc, "Array index out of range");
                    break;
                }

                const ValueNode::ValueType& value = this->regOf(frame, instr.src3);
                if (const auto* nested = std::get_if<ArrayRef>(&value); nested && nested->get() == arr.get()) {
                    this->diagnostics.addError(this->currentLoc,
                        "Circular reference: an array cannot contain itself");
                    break;
                }

                if (index == static_cast<int>(arr->elements.size())) {
                    if (static_cast<std::size_t>(index + 1) > this->mBudget->maxArrayElements) {
                        this->failBudget(sandbox::SandboxBudget::Violation::ArrayElementLimit, "Array size budget exhausted");
                        break;
                    }

                    arr->elements.push_back(value);
                } else {
                    arr->elements[index] = value;
                }
            } break;
            default: break;
        }
    }

    void VM::execClosure(ExecArgs& s) {
        const auto& instr = s.instr;
        Frame& frame = s.frame;
        const MirChunk& cur = s.cur;
        const auto& owner = s.owner;
        switch (instr.op) {
            case MirOp::MAKE_LAMBDA: {
                const auto& meta = cur.lambdas[instr.operand];

                auto func = std::make_shared<FunctionRef>();
                func->owner = owner;
                func->bodyIndex = meta.bodyIndex;
                func->argCount = meta.argCount;
                func->hasThis = frame.hasThis;
                if (frame.hasThis)
                    func->thisObj = std::get<ObjectRef>(frame.thisObj);

                const size_t captureCount =
                    std::min(static_cast<size_t>(instr.imm), frame.localsSize);
                func->captures.assign(
                    this->localPool.begin() + frame.localsBase,
                    this->localPool.begin() + frame.localsBase + captureCount
                );
                func->globals = this->globals;

                this->setReg(frame, instr.dst, func);
            } break;
            case MirOp::LOAD_THIS: {
                if (!frame.hasThis) {
                    this->diagnostics.addError(this->currentLoc, "'this' is not available in the current context");
                    break;
                }

                this->setReg(frame, instr.dst, frame.thisObj);
            } break;
            default: break;
        }
    }

    void VM::execInstanceof(ExecArgs& s) {
        const auto& instr = s.instr;
        const MirChunk& cur = s.cur;
        const MirChunk& chunk = s.chunk;
        Frame& frame = s.frame;
        switch (instr.op) {
            case MirOp::INSTANCEOF: {
                const auto& name = std::get<std::string>(cur.constants[instr.operand]);
                const ValueNode::ValueType& value = this->regOf(frame, instr.src1);

                if (!std::holds_alternative<ObjectRef>(value)) {
                    this->setReg(frame, instr.dst, false);
                    break;
                }

                auto obj = std::get<ObjectRef>(value);
                bool result = (obj->className == name);

                if (!result && obj->classIndex >= 0) {
                    int targetIdx = -1;
                    for (size_t i = 0; i < chunk.classes.size(); ++i) {
                        if (chunk.classes[i].name == name) {
                            targetIdx = static_cast<int>(i);
                            break;
                        }
                    }

                    if (targetIdx >= 0)
                        result = this->isDerived(chunk, obj->classIndex, targetIdx);
                }

                this->setReg(frame, instr.dst, result);
            } break;
            default: break;
        }
    }

    void VM::execObjectCreate(ExecArgs& s) {
        const auto& instr = s.instr;
        const MirChunk& chunk = s.chunk;
        Frame& frame = s.frame;
        switch (instr.op) {
            case MirOp::NEW: {
                if (const auto violation = this->mBudget->accountObject();
                    violation != sandbox::SandboxBudget::Violation::None) {
                    this->failBudget(violation, "Object count budget exhausted");
                    break;
                }

                const auto& cls = chunk.classes[instr.operand];

                auto obj = std::make_shared<Object>();
                obj->className = cls.name;
                obj->classIndex = instr.operand;
                obj->layout = this->classLayout(chunk, instr.operand);
                obj->resize(cls.fieldNames.size());

                for (size_t i = 0; i < cls.fieldNames.size(); ++i) {
                    if (cls.hasDefault[i])
                        obj->slots[i] = VM::cloneValue(cls.defaults[i]);
                }

                this->setReg(frame, instr.dst, obj);

                if (cls.constructorIndex != -1) {
                    const auto& ctor = chunk.methods[cls.constructorIndex];

                    // Collect before pushing: `pushFrame` may reallocate `frames`
                    // and invalidate every reference into it, including `frame`.
                    auto args = this->collectArgs(frame, instr.src1, ctor.argCount);

                    Frame callee(*chunk.methodBodies[ctor.bodyIndex]);
                    callee.hasThis = true;
                    callee.thisObj = obj;
                    callee.hasPending = true;
                    callee.pendingPush = obj;
                    callee.returnReg = instr.dst;

                    if (!this->pushFrame(std::move(callee)))
                        break;

                    const size_t base = this->frames.back().localsBase;
                    for (size_t i = 0; i < args.size(); ++i)
                        this->localPool[base + i] = std::move(args[i]);
                }
            } break;
            case MirOp::NEW_NATIVE: {
                if (const auto violation = this->mBudget->accountObject();
                    violation != sandbox::SandboxBudget::Violation::None) {
                    this->failBudget(violation, "Object count budget exhausted");
                    break;
                }

                const auto& meta = chunk.nativeCalls[instr.operand];

                auto result = ClassCall::getInstance().createCached(
                    meta.className, this->collectArgs(frame, instr.src1, meta.argCount), s.placeholders,
                    this->mNativeConstructorSlots[instr.operand], this->diagnostics, this->currentLoc
                );

                if (!result.has_value()) {
                    this->diagnostics.addError(this->currentLoc,
                        "Failed to create native class '" + meta.className + "': " + result.error().message());
                    break;
                }

                this->setReg(frame, instr.dst, result.value());
            } break;
            default: break;
        }
    }

}
