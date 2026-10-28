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

        const FieldLayout* layout = obj.layout.get();
        FieldCacheEntry& cached = this->mFieldCache[inlineCacheSlot<kInlineCacheSize>(&instr)];

        if (cached.instr != &instr || cached.layout != layout) {
            cached.instr = &instr;
            cached.layout = layout;
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
                int slot = this->resolveFieldSlot(*obj, name, instr);

                const auto* existing = obj->fieldAt(slot, name);
                if (!adjustRetainedBytes(
                        obj->budgetOwner, obj->budgetStringBytes,
                        existing ? retainedBytes(*existing) : 0,
                        retainedBytes(this->regOf(frame, instr.src2))
                    )) {
                    this->failBudget(sandbox::SandboxBudget::Violation::TotalByteLimit, "Field value exceeds the memory budget");
                    break;
                }

                ValueNode::ValueType previous;
                const bool collectBarrier = this->heap().collecting();
                if (collectBarrier && slot >= 0)
                    previous = obj->slots[static_cast<size_t>(slot)];

                if (slot < 0) {
                    obj->addField(name, this->regOf(frame, instr.src2));
                } else {
                    obj->slots[static_cast<size_t>(slot)] = this->regOf(frame, instr.src2);
                }

                if ((instr.imm & static_cast<int>(MirFlag::ScalarStore)) == 0
                    && CycleCollector::holdsReference(this->regOf(frame, instr.src2))) {
                    this->heap().noteReferenceStore();
                    this->heap().track(obj);

                    if (collectBarrier)
                        this->heap().barrier(slot >= 0 ? &previous : nullptr, this->regOf(frame, instr.src2));
                }
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

                const bool collectBarrier = this->heap().collecting();
                ValueNode::ValueType previous;
                if (collectBarrier)
                    previous = obj->slots[static_cast<size_t>(instr.operand)];

                if (!adjustRetainedBytes(
                        obj->budgetOwner, obj->budgetStringBytes,
                        retainedBytes(obj->slots[static_cast<size_t>(instr.operand)]),
                        retainedBytes(this->regOf(frame, instr.src2))
                    )) {
                    this->failBudget(sandbox::SandboxBudget::Violation::TotalByteLimit, "Field value exceeds the memory budget");
                    break;
                }

                obj->slots[static_cast<size_t>(instr.operand)] = this->regOf(frame, instr.src2);

                if ((instr.imm & static_cast<int>(MirFlag::ScalarStore)) == 0
                    && CycleCollector::holdsReference(obj->slots[static_cast<size_t>(instr.operand)])) {
                    this->heap().noteReferenceStore();
                    this->heap().track(obj);

                    if (collectBarrier)
                        this->heap().barrier(&previous, obj->slots[static_cast<size_t>(instr.operand)]);
                }
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

                if (this->heap().collecting())
                    this->heap().barrier(nullptr, selfValue);

                for (auto& captured : (*func)->captures) {
                    if (const auto* held = std::get_if<ObjectRef>(&captured); held && *held == *self)
                        captured = std::monostate{};
                }
            } break;
            case MirOp::LOAD_LEN: {
                const ValueNode::ValueType& iterable = this->regOf(frame, instr.src1);

                if (std::holds_alternative<ArrayRef>(iterable)) {
                    const auto& array = std::get<ArrayRef>(iterable);
                    if (!array) {
                        this->diagnostics.addError(this->currentLoc, "Cannot take length of a null array");
                        break;
                    }

                    this->setReg(frame, instr.dst, static_cast<int>(array->elements.size()));
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

                const std::size_t arrayBytes = static_cast<std::size_t>(count) * 16;
                auto arr = std::make_shared<ArrayValue>();
                arr->budgetOwner = this->mBudget;
                arr->budgetBytes = arrayBytes;
                arr->elements.resize(count);

                bool holdsReferences = false;
                for (int i = 0; i < count; ++i) {
                    arr->elements[i] = this->regOf(frame, instr.src1 + i);
                    holdsReferences = holdsReferences || CycleCollector::holdsReference(arr->elements[i]);
                }

                std::size_t stringBytes = 0;
                for (const auto& element : arr->elements)
                    stringBytes += retainedBytes(element);

                if (!adjustRetainedBytes(arr->budgetOwner, arr->budgetStringBytes, 0, stringBytes)) {
                    this->failBudget(sandbox::SandboxBudget::Violation::TotalByteLimit, "Array literal exceeds the memory budget");
                    break;
                }

                if (holdsReferences)
                    this->heap().track(arr);

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
                if (!arr) {
                    this->diagnostics.addError(this->currentLoc, "Cannot index a null array");
                    break;
                }

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
                if (!arr) {
                    this->diagnostics.addError(this->currentLoc, "Cannot index a null array");
                    break;
                }

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

                const bool collectBarrier = this->heap().collecting();
                ValueNode::ValueType previous;
                if (collectBarrier && index < static_cast<int>(arr->elements.size()))
                    previous = arr->elements[static_cast<size_t>(index)];

                const std::size_t previousString = index < static_cast<int>(arr->elements.size())
                    ? retainedBytes(arr->elements[static_cast<size_t>(index)])
                    : 0;

                if (!adjustRetainedBytes(arr->budgetOwner, arr->budgetStringBytes, previousString, retainedBytes(value))) {
                    this->failBudget(sandbox::SandboxBudget::Violation::TotalByteLimit, "Array element exceeds the memory budget");
                    break;
                }

                if (index == static_cast<int>(arr->elements.size())) {
                    if (static_cast<std::size_t>(index + 1) > this->mBudget->maxArrayElements) {
                        this->failBudget(sandbox::SandboxBudget::Violation::ArrayElementLimit, "Array size budget exhausted");
                        break;
                    }

                    if (const auto violation = this->mBudget->accountArray(1);
                        violation != sandbox::SandboxBudget::Violation::None) {
                        this->failBudget(violation, "Array size budget exhausted");
                        break;
                    }

                    if (arr->budgetOwner.get() == this->mBudget.get())
                        arr->budgetBytes += 16;
                    arr->elements.push_back(value);
                } else {
                    arr->elements[index] = value;
                }

                if ((instr.imm & static_cast<int>(MirFlag::ScalarStore)) == 0
                    && CycleCollector::holdsReference(value)) {
                    this->heap().noteReferenceStore();
                    this->heap().track(arr);

                    if (collectBarrier)
                        this->heap().barrier(index < static_cast<int>(arr->elements.size()) ? &previous : nullptr, value);
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

                func->captures.reserve(meta.captures.size());
                func->captureSlots.reserve(meta.captures.size());
                for (const auto& capture : meta.captures) {
                    if (capture.sourceSlot >= 0) {
                        const size_t index =
                            frame.localsBase + static_cast<size_t>(capture.sourceSlot);

                        func->captures.push_back(index < this->localPool.size()
                            ? this->localPool[index]
                            : ValueNode::ValueType{});
                    } else {
                        const auto it = this->globals->find(capture.name);
                        func->captures.push_back(
                            it != this->globals->end() ? it->second : ValueNode::ValueType{});
                    }

                    func->captureSlots.push_back(capture.slot);
                }

                func->globals = this->globals;

                func->budgetOwner = this->mBudget;
                std::size_t captureBytes = 0;
                for (const auto& capture : func->captures)
                    captureBytes += retainedBytes(capture);

                if (!adjustRetainedBytes(func->budgetOwner, func->budgetStringBytes, 0, captureBytes)) {
                    this->failBudget(sandbox::SandboxBudget::Violation::TotalByteLimit, "Lambda captures exceed the memory budget");
                    break;
                }

                if (!meta.captures.empty())
                    this->heap().track(func);

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
                auto violation = this->mBudget->accountObject();
                if (violation != sandbox::SandboxBudget::Violation::None) {
                    if (this->heap().collect() == 0) {
                        this->failBudget(violation, "Object count budget exhausted");
                        break;
                    }

                    violation = this->mBudget->accountObject();
                    if (violation != sandbox::SandboxBudget::Violation::None) {
                        this->failBudget(violation, "Object count budget exhausted");
                        break;
                    }
                }

                const auto& cls = chunk.classes[instr.operand];

                auto obj = std::make_shared<Object>();
                obj->budgetOwner = this->mBudget;
                obj->className = cls.name;
                obj->classIndex = instr.operand;
                obj->layout = this->classLayout(chunk, instr.operand);
                obj->resize(cls.fieldNames.size());

                std::size_t defaultStringBytes = 0;
                for (size_t i = 0; i < cls.fieldNames.size(); ++i)
                    if (cls.hasDefault[i])
                        defaultStringBytes += retainedBytes(cls.defaults[i]);

                if (!adjustRetainedBytes(obj->budgetOwner, obj->budgetStringBytes, 0, defaultStringBytes)) {
                    this->failBudget(sandbox::SandboxBudget::Violation::TotalByteLimit, "Field defaults exceed the memory budget");
                    break;
                }

                for (size_t i = 0; i < cls.fieldNames.size(); ++i) {
                    if (!cls.hasDefault[i])
                        continue;

                    obj->slots[i] = VM::cloneValue(cls.defaults[i]);

                    auto* array = std::get_if<ArrayRef>(&obj->slots[i]);
                    if (array && *array && !this->adoptArray(**array)) {
                        this->failBudget(sandbox::SandboxBudget::Violation::TotalByteLimit, "Field defaults exceed the memory budget");
                        break;
                    }

                    if (array && std::any_of((*array)->elements.begin(), (*array)->elements.end(), CycleCollector::holdsReference))
                        this->heap().track(*array);
                }

                if (this->diagnostics.hasErrors())
                    break;

                this->setReg(frame, instr.dst, obj);

                if (cls.constructorIndex != -1) {
                    const auto& ctor = chunk.methods[cls.constructorIndex];
                    const size_t argsBase = frame.localsBase + static_cast<size_t>(instr.src1);

                    Frame callee(*chunk.methodBodies[ctor.bodyIndex]);
                    callee.hasThis = true;
                    callee.thisObj = obj;
                    callee.hasPending = true;
                    callee.pendingPush = obj;
                    callee.returnReg = instr.dst;

                    if (!this->pushFrame(std::move(callee)))
                        break;

                    this->placeArgs(argsBase, ctor.argCount);
                }
            } break;
            case MirOp::NEW_NATIVE: {
                const auto& meta = chunk.nativeCalls[instr.operand];

                auto result = ClassCall::getInstance().createCached(
                    meta.className, this->collectArgs(frame, instr.src1, meta.argCount), s.placeholders,
                    this->mCache->nativeConstructorSlots[instr.operand], this->diagnostics, this->currentLoc
                );

                if (!result.has_value()) {
                    this->diagnostics.addError(this->currentLoc,
                        "Failed to create native class '" + meta.className + "': " + result.error().message());
                    break;
                }

                this->storeNativeResult(frame, instr.dst, result.value());
            } break;
            default: break;
        }
    }

    void VM::execEnum(ExecArgs& s) {
        const auto& instr = s.instr;
        const MirChunk& chunk = s.chunk;
        Frame& frame = s.frame;

        switch (instr.op) {
            case MirOp::MAKE_ENUM: {
                if (instr.operand < 0 || static_cast<size_t>(instr.operand) >= chunk.enums.size()) {
                    this->diagnostics.addError(this->currentLoc, "Unknown enum id");
                    break;
                }

                const auto& meta = chunk.enums[static_cast<size_t>(instr.operand)];
                const auto tag = static_cast<std::uint32_t>(instr.imm);

                if (tag >= meta.variantNames.size()) {
                    this->diagnostics.addError(this->currentLoc, "Unknown enum variant");
                    break;
                }

                int arity = tag < meta.variantArity.size()
                    ? static_cast<int>(meta.variantArity[tag])
                    : 0;

                
                
                if (arity > 3)
                    arity = 3;

                EnumValue value{};
                value.enumId = static_cast<std::uint32_t>(instr.operand);
                value.tag = tag;

                if (arity > 0) {
                    const int sources[3] = { instr.src1, instr.src2, instr.src3 };
                    auto payload = std::make_shared<EnumPayload>();
                    payload->items.reserve(static_cast<size_t>(arity));

                    for (int i = 0; i < arity; ++i) {
                        if (sources[i] < 0) {
                            this->diagnostics.addError(this->currentLoc, "Missing enum payload");
                            break;
                        }

                        payload->items.push_back(this->regOf(frame, sources[i]));
                    }

                    value.payload = std::move(payload);
                }

                this->setReg(frame, instr.dst, std::move(value));
            } break;
            case MirOp::TAG_OF: {
                const auto& value = this->regOf(frame, instr.src1);
                int tag = -1;

                if (const auto* ev = std::get_if<EnumValue>(&value))
                    tag = static_cast<int>(ev->tag);

                this->setReg(frame, instr.dst, tag);
            } break;
            case MirOp::LOAD_ENUM_PAYLOAD: {
                const auto& value = this->regOf(frame, instr.src1);
                const auto* ev = std::get_if<EnumValue>(&value);

                if (!ev || !ev->payload || instr.operand < 0 ||
                    static_cast<size_t>(instr.operand) >= ev->payload->items.size()) {
                    this->setReg(frame, instr.dst, std::monostate{});
                    break;
                }

                this->setReg(frame, instr.dst,
                    ev->payload->items[static_cast<size_t>(instr.operand)]);
            } break;
            default: break;
        }
    }

}
