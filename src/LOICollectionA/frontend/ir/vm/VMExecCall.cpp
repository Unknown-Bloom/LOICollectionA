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

namespace LOICollection::frontend::ir::detail {
    std::string valueClassNameOf(const ValueNode::ValueType& val) {
        if (std::holds_alternative<ArrayRef>(val))
            return "Array";
        if (std::holds_alternative<std::string>(val))
            return "String";

        return {};
    }
}

namespace LOICollection::frontend::ir {

    using namespace detail;

    void VM::execMethodDispatch(ExecArgs& s) {
        const auto& instr = s.instr;
        const MirChunk& cur = s.cur;
        const MirChunk& chunk = s.chunk;
        Frame& frame = s.frame;

        switch (instr.op) {
            case MirOp::CALL_METHOD: {
                const auto& meta = chunk.methods[instr.operand];

                const ValueNode::ValueType& receiver = this->regOf(frame, instr.src1 + instr.imm);
                if (!std::holds_alternative<ObjectRef>(receiver)) {
                    this->diagnostics.addError(this->currentLoc, "Method call target is not an object");
                    break;
                }

                auto obj = std::get<ObjectRef>(receiver);
                if (!this->isDerived(chunk, obj->classIndex, meta.classIndex)) {
                    this->diagnostics.addError(this->currentLoc,
                        "Method '" + meta.name + "' does not belong to this object");
                    break;
                }

                const size_t argsBase = frame.localsBase + static_cast<size_t>(instr.src1);

                Frame callee(*chunk.methodBodies[meta.bodyIndex]);
                callee.hasThis = true;
                callee.thisObj = receiver;
                callee.returnReg = instr.dst;

                if (!this->pushFrame(std::move(callee)))
                    break;

                this->placeArgs(argsBase, meta.argCount);
            } break;
            case MirOp::CALL_METHOD_VIRTUAL: {
                const auto& meta = cur.virtualCalls[instr.operand];

                const ValueNode::ValueType& receiver = this->regOf(frame, instr.src1 + instr.imm);
                if (!std::holds_alternative<ObjectRef>(receiver)) {
                    this->diagnostics.addError(this->currentLoc, "Method call target is not an object");
                    break;
                }

                auto obj = std::get<ObjectRef>(receiver);
                if (obj->classIndex < 0 || obj->classIndex >= static_cast<int>(chunk.classes.size())) {
                    this->diagnostics.addError(this->currentLoc, "Invalid object class index");
                    break;
                }

                if (!this->isDerived(chunk, obj->classIndex, meta.classIndex)) {
                    this->diagnostics.addError(this->currentLoc, "Method call target is not an instance of the expected class");
                    break;
                }

                const auto& actualClass = chunk.classes[obj->classIndex];
                if (meta.ordinal < 0 || meta.ordinal >= static_cast<int>(actualClass.methods.size())) {
                    this->diagnostics.addError(this->currentLoc, "Invalid method ordinal");
                    break;
                }

                const auto& method = chunk.methods[actualClass.methods[meta.ordinal]];
                if (method.argCount != meta.argCount) {
                    this->diagnostics.addError(this->currentLoc,
                        "Method '" + method.name + "' expects " + std::to_string(method.argCount) +
                        " argument(s), got " + std::to_string(meta.argCount));
                    break;
                }

                const size_t argsBase = frame.localsBase + static_cast<size_t>(instr.src1);

                Frame callee(*chunk.methodBodies[method.bodyIndex]);
                callee.hasThis = true;
                callee.thisObj = receiver;
                callee.returnReg = instr.dst;

                if (!this->pushFrame(std::move(callee)))
                    break;

                this->placeArgs(argsBase, method.argCount);
            } break;
            case MirOp::CALL_METHOD_BY_NAME: {
                const auto& meta = cur.byNameCalls[instr.operand];

                const ValueNode::ValueType& receiver = this->regOf(frame, instr.src1 + instr.imm);
                if (!std::holds_alternative<ObjectRef>(receiver)) {
                    this->diagnostics.addError(this->currentLoc, "Method call target is not an object");
                    break;
                }

                auto obj = std::get<ObjectRef>(receiver);
                if (obj->classIndex < 0 || obj->classIndex >= static_cast<int>(chunk.classes.size())) {
                    this->diagnostics.addError(this->currentLoc, "Invalid object class index");
                    break;
                }

                const auto& cls = chunk.classes[obj->classIndex];

                ByNameCacheEntry& cached = this->mByNameCache[inlineCacheSlot<kInlineCacheSize>(&instr)];
                if (cached.instr != &instr || cached.classIndex != obj->classIndex) {
                    int ordinal = -1;
                    for (size_t o = 0; o < cls.methods.size(); ++o) {
                        const auto& m = chunk.methods[cls.methods[o]];
                        if (m.name == meta.methodName && m.argCount == meta.argCount) {
                            ordinal = static_cast<int>(o);
                            break;
                        }
                    }

                    cached.instr = &instr;
                    cached.classIndex = obj->classIndex;
                    cached.ordinal = ordinal;
                }

                const int ordinal = cached.ordinal;

                if (ordinal < 0) {
                    this->diagnostics.addError(this->currentLoc,
                        "Method '" + meta.methodName + "' not found on object of type '" + cls.name + "'");
                    break;
                }

                const auto& method = chunk.methods[cls.methods[ordinal]];

                const size_t argsBase = frame.localsBase + static_cast<size_t>(instr.src1);

                Frame callee(*chunk.methodBodies[method.bodyIndex]);
                callee.hasThis = true;
                callee.thisObj = receiver;
                callee.returnReg = instr.dst;

                if (!this->pushFrame(std::move(callee)))
                    break;

                this->placeArgs(argsBase, method.argCount);
            } break;
            case MirOp::CALL_SUPER_CTOR: {
                const auto& meta = cur.superCalls[instr.operand];

                const ValueNode::ValueType& receiver = this->regOf(frame, instr.src1 + instr.imm);
                if (!std::holds_alternative<ObjectRef>(receiver)) {
                    this->diagnostics.addError(this->currentLoc, "Super constructor call target is not an object");
                    break;
                }

                if (meta.constructorIndex < 0) {
                    if (meta.argCount != 0)
                        this->diagnostics.addError(this->currentLoc, "Base class has no constructor");
                    break;
                }

                const auto& ctor = chunk.methods[meta.constructorIndex];
                if (ctor.argCount != meta.argCount) {
                    this->diagnostics.addError(this->currentLoc,
                        "Base constructor expects " + std::to_string(ctor.argCount) +
                        " argument(s), got " + std::to_string(meta.argCount));
                    break;
                }

                const size_t argsBase = frame.localsBase + static_cast<size_t>(instr.src1);

                Frame callee(*chunk.methodBodies[ctor.bodyIndex]);
                callee.hasThis = true;
                callee.thisObj = receiver;
                callee.returnReg = instr.dst;

                if (!this->pushFrame(std::move(callee)))
                    break;

                this->placeArgs(argsBase, ctor.argCount);
            } break;
            default: break;
        }
    }

    void VM::execNativeCall(ExecArgs& s) {
        const auto& instr = s.instr;
        const MirChunk& chunk = s.chunk;
        Frame& frame = s.frame;

        switch (instr.op) {
            case MirOp::CALL_NATIVE_METHOD: {
                if (const auto violation = this->mBudget->accountNativeCall();
                    violation != sandbox::SandboxBudget::Violation::None) {
                    this->failBudget(violation, "Native call budget exhausted");
                    break;
                }

                const auto& meta = chunk.nativeCalls[instr.operand];
                auto args = this->collectArgs(frame, instr.src1, meta.argCount);

                if (meta.isStatic) {
                    auto result = ClassCall::getInstance().callStaticMethodCached(
                        meta.className, meta.name, args, s.placeholders,
                        this->mCache->nativeStaticMethodSlots[instr.operand], this->diagnostics, this->currentLoc
                    );

                    if (!result.has_value()) {
                        this->diagnostics.addError(this->currentLoc,
                            "Native static method call failed '" + meta.className + "::" + meta.name +
                            "': " + result.error().message());
                        break;
                    }

                    this->setReg(frame, instr.dst, result.value());
                    break;
                }

                auto receiver = this->regOf(frame, instr.src1 + instr.imm);
                if (!std::holds_alternative<ObjectRef>(receiver)) {
                    std::string valueClassName = valueClassNameOf(receiver);
                    if (valueClassName.empty()) {
                        this->diagnostics.addError(this->currentLoc, "Native method call target is not an object");
                        break;
                    }

                    auto result = ClassCall::getInstance().callValueMethodCached(
                        valueClassName, meta.name, receiver, args,
                        this->mCache->nativeValueMethodSlots[instr.operand], this->diagnostics, this->currentLoc
                    );

                    if (!result.has_value()) {
                        this->diagnostics.addError(this->currentLoc,
                            "Native value method call failed '" + valueClassName + "::" + meta.name +
                            "': " + result.error().message());
                        break;
                    }

                    this->setReg(frame, instr.dst, result.value());
                    break;
                }

                auto obj = std::get<ObjectRef>(receiver);

                auto result = ClassCall::getInstance().callMethodCached(
                    obj->className, meta.name, args, obj, s.placeholders,
                    this->mCache->nativeMethodSlots[instr.operand], this->diagnostics, this->currentLoc
                );

                if (!result.has_value()) {
                    this->diagnostics.addError(this->currentLoc,
                        "Native method call failed '" + meta.className + "::" + meta.name +
                        "': " + result.error().message());
                    break;
                }

                this->setReg(frame, instr.dst, result.value());
            } break;
            default: break;
        }
    }

    void VM::execFunctionCall(ExecArgs& s) {
        const auto& instr = s.instr;
        const MirChunk& chunk = s.chunk;
        Frame& frame = s.frame;

        switch (instr.op) {
            case MirOp::CALL_FUNC: {
                const auto& meta = chunk.methods[instr.operand];

                const size_t argsBase = frame.localsBase + static_cast<size_t>(instr.src1);

                Frame callee(*chunk.methodBodies[meta.bodyIndex]);
                callee.hasThis = false;
                callee.returnReg = instr.dst;

                if (!this->pushFrame(std::move(callee)))
                    break;

                this->placeArgs(argsBase, meta.argCount);
            } break;
            case MirOp::CALL_LAMBDA: {
                const ValueNode::ValueType& funcValue = this->regOf(frame, instr.src2);
                if (!std::holds_alternative<FunctionRefPtr>(funcValue)) {
                    this->diagnostics.addError(this->currentLoc, "Attempted to call a non-function value");
                    break;
                }

                auto func = std::get<FunctionRefPtr>(funcValue);
                if (instr.imm != func->argCount) {
                    this->diagnostics.addError(this->currentLoc,
                        "Function expects " + std::to_string(func->argCount) +
                        " argument(s), got " + std::to_string(instr.imm));
                    break;
                }

                if (!func->owner ||
                    func->bodyIndex < 0 ||
                    func->bodyIndex >= static_cast<int>(func->owner->methodBodies.size())) {
                    this->diagnostics.addError(this->currentLoc, "Invalid function body index");
                    break;
                }

                const size_t paramBase = func->captures.size();
                if (paramBase + static_cast<size_t>(func->argCount) >
                    static_cast<size_t>(func->owner->methodBodies[func->bodyIndex]->slotCount)) {
                    this->diagnostics.addError(this->currentLoc, "Lambda frame is too small for its parameters");
                    break;
                }

                const size_t argsBase = frame.localsBase + static_cast<size_t>(instr.src1);

                Frame callee(*func->owner->methodBodies[func->bodyIndex]);
                callee.hasThis = func->hasThis;
                callee.returnReg = instr.dst;

                if (func->hasThis) {
                    auto self = func->thisObj.lock();
                    if (!self) {
                        this->diagnostics.addError(this->currentLoc, "Method receiver has been released");
                        break;
                    }

                    callee.thisObj = self;
                }

                if (!this->pushFrame(std::move(callee)))
                    break;

                const size_t base = this->frames.back().localsBase;
                std::copy_n(func->captures.begin(), paramBase, this->localPool.begin() + base);
                this->placeArgs(argsBase, func->argCount, paramBase);
            } break;
            default: break;
        }
    }

    void VM::execHostCall(ExecArgs& s) {
        const auto& instr = s.instr;
        const MirChunk& cur = s.cur;
        Frame& frame = s.frame;

        switch (instr.op) {
            case MirOp::CALL: {
                if (const auto violation = this->mBudget->accountNativeCall();
                    violation != sandbox::SandboxBudget::Violation::None) {
                    this->failBudget(violation, "Native call budget exhausted");
                    break;
                }

                const auto& meta = cur.functions[instr.operand];

                CallbackTypeValues args;
                args.reserve(static_cast<size_t>(instr.imm));
                for (int i = 0; i < instr.imm; ++i)
                    args.push_back(this->regOf(frame, instr.src1 + i));

                HostCallCacheEntry& cached = this->mHostCallCache[inlineCacheSlot<kHostCallCacheSize>(&instr)];
                if (cached.instr != &instr) {
                    const size_t pos = meta.name.find("::");
                    cached.namespaces = meta.name.substr(0, pos);
                    cached.function = meta.name.substr(pos + 2);
                    cached.instr = &instr;
                }

                auto result = FunctionCall::getInstance().callFunctionCached(
                    cached.namespaces, cached.function, args, s.placeholders,
                    this->mCache->functionCallSlots[instr.operand], this->diagnostics, this->currentLoc
                );

                if (!result.has_value()) {
                    this->diagnostics.addError(this->currentLoc, result.error().message());
                    this->setReg(frame, instr.dst, ValueNode::ValueType{});
                    break;
                }

                this->setReg(frame, instr.dst, result.value());
            } break;
            case MirOp::CALL_MACRO: {
                if (const auto violation = this->mBudget->accountNativeCall();
                    violation != sandbox::SandboxBudget::Violation::None) {
                    this->failBudget(violation, "Native call budget exhausted");
                    break;
                }

                const auto& meta = cur.macros[instr.operand];

                CallbackTypeValues args;
                args.reserve(static_cast<size_t>(instr.imm));
                for (int i = 0; i < instr.imm; ++i)
                    args.push_back(this->regOf(frame, instr.src1 + i));

                auto result = MacroCall::getInstance().callMacro(
                    meta.name, args, s.placeholders, this->diagnostics, this->currentLoc
                );

                if (!result.has_value()) {
                    this->diagnostics.addError(this->currentLoc, result.error().message());
                    this->setReg(frame, instr.dst, ValueNode::ValueType{});
                    break;
                }

                this->setReg(frame, instr.dst, result.value());
            } break;
            default: break;
        }
    }

}
