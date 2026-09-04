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

    int VM::resolveFieldSlot(Object& obj, const std::string& name, const Instruction& instr) {
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
        const BytecodeChunk& cur = s.cur;
        switch (instr.op) {
            case OpCode::LOAD_FIELD: {
                const auto& name = std::get<std::string>(cur.constants[instr.operand]);
                auto objValue = this->pop();

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

                this->push(*field);
            } break;
            case OpCode::STORE_FIELD: {
                const auto& name = std::get<std::string>(cur.constants[instr.operand]);
                auto objValue = this->pop();
                auto val = this->pop();

                if (!std::holds_alternative<ObjectRef>(objValue)) {
                    this->diagnostics.addError(this->currentLoc, "Cannot store field '" + name + "' on a non-object value");
                    break;
                }

                auto obj = std::get<ObjectRef>(objValue);
                obj->fieldAtOrSpill(this->resolveFieldSlot(*obj, name, instr), name) = std::move(val);
            } break;
            case OpCode::LOAD_FIELD_SLOT: {
                auto objValue = this->pop();

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

                this->push(obj->slots[static_cast<size_t>(instr.operand)]);
            } break;
            case OpCode::STORE_FIELD_SLOT: {
                auto objValue = this->pop();
                auto val = this->pop();

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

                obj->slots[static_cast<size_t>(instr.operand)] = std::move(val);
            } break;
            case OpCode::BIND_THIS: {
                if (instr.operand <= 0 || this->stack.empty())
                    break;

                const size_t depth = static_cast<size_t>(instr.operand);
                if (depth >= this->stack.size())
                    break;

                const auto* self = std::get_if<ObjectRef>(&this->stack.back());
                if (!self || !*self)
                    break;

                auto& slot = this->stack[this->stack.size() - 1 - depth];
                auto* func = std::get_if<FunctionRefPtr>(&slot);
                if (!func || !*func)
                    break;

                (*func)->hasThis = true;
                (*func)->thisObj = *self;

                for (auto& captured : (*func)->captures) {
                    if (const auto* held = std::get_if<ObjectRef>(&captured); held && *held == *self)
                        captured = std::monostate{};
                }
            } break;
            case OpCode::LOAD_LEN: {
                auto iterable = this->pop();

                if (std::holds_alternative<ArrayRef>(iterable)) {
                    this->push(static_cast<int>(std::get<ArrayRef>(iterable)->elements.size()));
                    break;
                }

                if (std::holds_alternative<std::string>(iterable)) {
                    this->push(static_cast<int>(codepointCount(std::get<std::string>(iterable))));
                    break;
                }

                this->diagnostics.addError(this->currentLoc, "Cannot take length of a non-iterable value");
            } break;
            default: break;
        }
    }

    void VM::execArray(ExecArgs& s) {
        const auto& instr = s.instr;
        switch (instr.op) {
            case OpCode::MAKE_ARRAY: {
                int count = instr.operand;
                if (count < 0 || count > static_cast<int>(this->stack.size())) {
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
                for (int i = count - 1; i >= 0; --i)
                    arr->elements[i] = this->pop();

                this->push(arr);
            } break;
            case OpCode::LOAD_INDEX: {
                auto indexValue = this->pop();
                auto targetValue = this->pop();

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

                    this->push(std::move(*character));
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

                this->push(arr->elements[index]);
            } break;
            case OpCode::STORE_INDEX: {
                auto indexValue = this->pop();
                auto targetValue = this->pop();
                auto value = this->pop();

                if (!std::holds_alternative<ArrayRef>(targetValue)) {
                    this->diagnostics.addError(this->currentLoc, "Cannot index a non-array value");
                    break;
                }
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

                if (auto* nested = std::get_if<ArrayRef>(&value); nested && nested->get() == arr.get()) {
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
        const BytecodeChunk& cur = s.cur;
        const auto& owner = s.owner;
        switch (instr.op) {
            case OpCode::MAKE_LAMBDA: {
                const auto& meta = cur.lambdas[instr.operand];

                auto func = std::make_shared<FunctionRef>();
                func->owner = owner;
                func->bodyIndex = meta.bodyIndex;
                func->argCount = meta.argCount;
                func->hasThis = frame.hasThis;
                if (frame.hasThis)
                    func->thisObj = std::get<ObjectRef>(frame.thisObj);
                const size_t captureCount =
                    std::min(static_cast<size_t>(meta.captureCount), frame.localsSize);
                func->captures.assign(
                    this->localPool.begin() + frame.localsBase,
                    this->localPool.begin() + frame.localsBase + captureCount
                );
                func->globals = this->globals;

                this->push(func);
            } break;
            case OpCode::LOAD_THIS: {
                if (!frame.hasThis) {
                    this->diagnostics.addError(this->currentLoc, "'this' is not available in the current context");
                    break;
                }

                this->push(frame.thisObj);
            } break;
            default: break;
        }
    }

    void VM::execInstanceof(ExecArgs& s) {
        const auto& instr = s.instr;
        const BytecodeChunk& cur = s.cur;
        const BytecodeChunk& chunk = s.chunk;
        switch (instr.op) {
            case OpCode::INSTANCEOF: {
                const auto& name = std::get<std::string>(cur.constants[instr.operand]);
                auto value = this->pop();

                if (!std::holds_alternative<ObjectRef>(value)) {
                    this->push(false);
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

                this->push(result);
            } break;
            default: break;
        }
    }

    void VM::execObjectCreate(ExecArgs& s) {
        const auto& instr = s.instr;
        const BytecodeChunk& chunk = s.chunk;
        const auto& placeholders = s.placeholders;
        switch (instr.op) {
            case OpCode::NEW: {
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

                if (cls.constructorIndex != -1) {
                    const auto& ctor = chunk.methods[cls.constructorIndex];

                    Frame callee(*chunk.methodBodies[ctor.bodyIndex]);
                    callee.hasThis = true;
                    callee.thisObj = obj;
                    callee.hasPending = true;
                    callee.pendingPush = obj;

                    if (!this->pushFrame(std::move(callee)))
                        break;

                    Frame& top = this->frames.back();
                    for (int i = 0; i < ctor.argCount; ++i)
                        this->localPool[top.localsBase + ctor.argCount - 1 - i] = this->pop();
                } else {
                    this->push(obj);
                }
            } break;
            case OpCode::NEW_NATIVE: {
                if (const auto violation = this->mBudget->accountObject();
                    violation != sandbox::SandboxBudget::Violation::None) {
                    this->failBudget(violation, "Object count budget exhausted");
                    break;
                }

                const auto& meta = chunk.nativeCalls[instr.operand];

                std::vector<ValueNode::ValueType> args(meta.argCount);
                for (int i = 0; i < meta.argCount; ++i)
                    args[meta.argCount - 1 - i] = this->pop();

                auto result = ClassCall::getInstance().createCached(
                    meta.className, args, placeholders,
                    this->mNativeConstructorSlots[instr.operand], this->diagnostics, this->currentLoc
                );

                if (!result.has_value()) {
                    this->diagnostics.addError(this->currentLoc,
                        "Failed to create native class '" + meta.className + "': " + result.error().message());
                    break;
                }

                this->push(result.value());
            } break;
            default: break;
        }
    }

}
