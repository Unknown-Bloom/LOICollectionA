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
        std::string valueClassNameOf(const ValueNode::ValueType& val) {
            if (std::holds_alternative<ArrayRef>(val))
                return "Array";
            if (std::holds_alternative<std::string>(val))
                return "String";

            return {};
        }
    }

    void VM::execMethodDispatch(ExecArgs& s) {
        const auto& instr = s.instr;
        const BytecodeChunk& cur = s.cur;
        const BytecodeChunk& chunk = s.chunk;
        switch (instr.op) {
            case OpCode::CALL_METHOD: {
                    const auto& meta = chunk.methods[instr.operand];

                    auto receiver = this->pop();
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

                    Frame callee(*chunk.methodBodies[meta.bodyIndex]);
                    callee.hasThis = true;
                    callee.thisObj = receiver;

                    if (!this->pushFrame(std::move(callee)))
                        break;

                    Frame& top = this->frames.back();
                    for (int i = 0; i < meta.argCount; ++i)
                        this->localPool[top.localsBase + meta.argCount - 1 - i] = this->pop();
            } break;
            case OpCode::CALL_METHOD_VIRTUAL: {
                    const auto& meta = cur.virtualCalls[instr.operand];

                    auto receiver = this->pop();
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

                    Frame callee(*chunk.methodBodies[method.bodyIndex]);
                    callee.hasThis = true;
                    callee.thisObj = receiver;

                    if (!this->pushFrame(std::move(callee)))
                        break;

                    Frame& top = this->frames.back();
                    for (int i = 0; i < method.argCount; ++i)
                        this->localPool[top.localsBase + method.argCount - 1 - i] = this->pop();
            } break;
            case OpCode::CALL_METHOD_BY_NAME: {
                    const auto& meta = cur.byNameCalls[instr.operand];

                    auto receiver = this->pop();
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
                    int ordinal = -1;
                    for (size_t o = 0; o < cls.methods.size(); ++o) {
                        const auto& m = chunk.methods[cls.methods[o]];
                        if (m.name == meta.methodName && m.argCount == meta.argCount) {
                            ordinal = static_cast<int>(o);
                            break;
                        }
                    }

                    if (ordinal < 0) {
                        this->diagnostics.addError(this->currentLoc,
                            "Method '" + meta.methodName + "' not found on object of type '" + cls.name + "'");
                        break;
                    }

                    const auto& method = chunk.methods[cls.methods[ordinal]];

                    Frame callee(*chunk.methodBodies[method.bodyIndex]);
                    callee.hasThis = true;
                    callee.thisObj = receiver;

                    if (!this->pushFrame(std::move(callee)))
                        break;

                    Frame& top = this->frames.back();
                    for (int i = 0; i < method.argCount; ++i)
                        this->localPool[top.localsBase + method.argCount - 1 - i] = this->pop();
            } break;
            case OpCode::CALL_SUPER_CTOR: {
                    const auto& meta = cur.superCalls[instr.operand];

                    auto receiver = this->pop();
                    if (!std::holds_alternative<ObjectRef>(receiver)) {
                        this->diagnostics.addError(this->currentLoc, "Super constructor call target is not an object");
                        break;
                    }

                    if (meta.constructorIndex < 0) {
                        if (meta.argCount != 0) {
                            this->diagnostics.addError(this->currentLoc, "Base class has no constructor");
                        } else {
                            this->push(std::string(""));
                        }
                        break;
                    }

                    const auto& ctor = chunk.methods[meta.constructorIndex];
                    if (ctor.argCount != meta.argCount) {
                        this->diagnostics.addError(this->currentLoc,
                            "Base constructor expects " + std::to_string(ctor.argCount) +
                            " argument(s), got " + std::to_string(meta.argCount));
                        break;
                    }

                    Frame callee(*chunk.methodBodies[ctor.bodyIndex]);
                    callee.hasThis = true;
                    callee.thisObj = receiver;

                    if (!this->pushFrame(std::move(callee)))
                        break;

                    Frame& top = this->frames.back();
                    for (int i = 0; i < ctor.argCount; ++i)
                        this->localPool[top.localsBase + ctor.argCount - 1 - i] = this->pop();
            } break;
            default: break;
        }
    }

    void VM::execNativeCall(ExecArgs& s) {
        const auto& instr = s.instr;
        const BytecodeChunk& chunk = s.chunk;
        const auto& placeholders = s.placeholders;
        switch (instr.op) {
            case OpCode::CALL_NATIVE_METHOD: {
                    if (const auto violation = this->mBudget->accountNativeCall();
                        violation != sandbox::SandboxBudget::Violation::None) {
                        this->failBudget(violation, "Native call budget exhausted");
                        break;
                    }

                    const auto& meta = chunk.nativeCalls[instr.operand];

                    if (meta.isStatic) {
                        std::vector<ValueNode::ValueType> args(meta.argCount);
                        for (int i = 0; i < meta.argCount; ++i)
                            args[meta.argCount - 1 - i] = this->pop();

                        auto result = ClassCall::getInstance().callStaticMethodCached(
                            meta.className, meta.name, args, placeholders,
                            this->mNativeStaticMethodSlots[instr.operand], this->diagnostics, this->currentLoc
                        );

                        if (!result.has_value()) {
                            this->diagnostics.addError(this->currentLoc,
                                "Native static method call failed '" + meta.className + "::" + meta.name +
                                "': " + result.error().message());
                            break;
                        }

                        this->push(result.value());
                        break;
                    }

                    auto receiver = this->pop();
                    if (!std::holds_alternative<ObjectRef>(receiver)) {
                        std::string valueClassName = valueClassNameOf(receiver);
                        if (valueClassName.empty()) {
                            this->diagnostics.addError(this->currentLoc, "Native method call target is not an object");
                            break;
                        }

                        std::vector<ValueNode::ValueType> args(meta.argCount);
                        for (int i = 0; i < meta.argCount; ++i)
                            args[meta.argCount - 1 - i] = this->pop();

                        auto result = ClassCall::getInstance().callValueMethodCached(
                            valueClassName, meta.name, receiver, args,
                            this->mNativeValueMethodSlots[instr.operand], this->diagnostics, this->currentLoc
                        );

                        if (!result.has_value()) {
                            this->diagnostics.addError(this->currentLoc,
                                "Native value method call failed '" + valueClassName + "::" + meta.name +
                                "': " + result.error().message());
                            break;
                        }

                        this->push(result.value());
                        break;
                    }

                    auto obj = std::get<ObjectRef>(receiver);

                    std::vector<ValueNode::ValueType> args(meta.argCount);
                    for (int i = 0; i < meta.argCount; ++i)
                        args[meta.argCount - 1 - i] = this->pop();

                    auto result = ClassCall::getInstance().callMethodCached(
                        obj->className, meta.name, args, obj, placeholders,
                        this->mNativeMethodSlots[instr.operand], this->diagnostics, this->currentLoc
                    );

                    if (!result.has_value()) {
                        this->diagnostics.addError(this->currentLoc,
                            "Native method call failed '" + meta.className + "::" + meta.name +
                            "': " + result.error().message());
                        break;
                    }

                    this->push(result.value());
            } break;
            default: break;
        }
    }

    void VM::execFunctionCall(ExecArgs& s) {
        const auto& instr = s.instr;
        const BytecodeChunk& chunk = s.chunk;
        switch (instr.op) {
            case OpCode::CALL_FUNC: {
                    const auto& meta = chunk.methods[instr.operand];

                    Frame callee(*chunk.methodBodies[meta.bodyIndex]);
                    callee.hasThis = false;

                    if (!this->pushFrame(std::move(callee)))
                        break;

                    Frame& top = this->frames.back();
                    for (int i = 0; i < meta.argCount; ++i)
                        this->localPool[top.localsBase + meta.argCount - 1 - i] = this->pop();
            } break;
            case OpCode::CALL_LAMBDA: {
                    auto funcValue = this->pop();
                    if (!std::holds_alternative<FunctionRefPtr>(funcValue)) {
                        this->diagnostics.addError(this->currentLoc, "Attempted to call a non-function value");
                        break;
                    }

                    auto func = std::get<FunctionRefPtr>(funcValue);
                    if (instr.operand != func->argCount) {
                        this->diagnostics.addError(this->currentLoc,
                            "Function expects " + std::to_string(func->argCount) +
                            " argument(s), got " + std::to_string(instr.operand));
                        break;
                    }

                    if (!func->owner ||
                        func->bodyIndex < 0 ||
                        func->bodyIndex >= static_cast<int>(func->owner->methodBodies.size())) {
                        this->diagnostics.addError(this->currentLoc, "Invalid function body index");
                        break;
                    }

                    Frame callee(*func->owner->methodBodies[func->bodyIndex]);
                    callee.hasThis = func->hasThis;
                    if (func->hasThis) {
                        auto self = func->thisObj.lock();
                        if (!self) {
                            this->diagnostics.addError(this->currentLoc, "Method receiver has been released");
                            break;
                        }

                        callee.thisObj = self;
                    }

                    const size_t paramBase = func->captures.size();
                    if (paramBase + static_cast<size_t>(func->argCount) > callee.localsSize) {
                        this->diagnostics.addError(this->currentLoc, "Lambda frame is too small for its parameters");
                        break;
                    }

                    if (!this->pushFrame(std::move(callee)))
                        break;

                    Frame& top = this->frames.back();
                    std::copy_n(func->captures.begin(), paramBase, this->localPool.begin() + top.localsBase);

                    for (int i = 0; i < func->argCount; ++i)
                        this->localPool[top.localsBase + paramBase + func->argCount - 1 - i] = this->pop();
            } break;
            default: break;
        }
    }

    void VM::execHostCall(ExecArgs& s) {
        const auto& instr = s.instr;
        const BytecodeChunk& cur = s.cur;
        const auto& placeholders = s.placeholders;
        switch (instr.op) {
            case OpCode::CALL: {
                    if (const auto violation = this->mBudget->accountNativeCall();
                        violation != sandbox::SandboxBudget::Violation::None) {
                        this->failBudget(violation, "Native call budget exhausted");
                        break;
                    }

                    const auto& meta = cur.functions[instr.operand];

                    CallbackTypeValues args;
                    args.reserve(meta.argCount);
                    for (int i = 0; i < meta.argCount; ++i)
                        args.push_back(this->pop());

                    std::ranges::reverse(args);

                    auto ns = meta.name.substr(0, meta.name.find("::"));
                    auto func = meta.name.substr(meta.name.find("::") + 2);
                    auto result = FunctionCall::getInstance().callFunctionCached(
                        ns, func, args, placeholders,
                        this->mFunctionCallSlots[instr.operand], this->diagnostics, this->currentLoc
                    );

                    if (!result.has_value()) {
                        this->diagnostics.addError(this->currentLoc, result.error().message());
                        this->push(ValueNode::ValueType{});
                        break;
                    }

                    this->push(result.value());
            } break;
            case OpCode::CALL_MACRO: {
                    if (const auto violation = this->mBudget->accountNativeCall();
                        violation != sandbox::SandboxBudget::Violation::None) {
                        this->failBudget(violation, "Native call budget exhausted");
                        break;
                    }

                    const auto& meta = cur.macros[instr.operand];

                    CallbackTypeValues args;
                    args.reserve(meta.argCount);
                    for (int i = 0; i < meta.argCount; ++i)
                        args.push_back(this->pop());

                    std::ranges::reverse(args);
                    
                    auto result = MacroCall::getInstance().callMacro(
                        meta.name, args, placeholders, this->diagnostics, this->currentLoc
                    );

                    if (!result.has_value()) {
                        this->diagnostics.addError(this->currentLoc, result.error().message());
                        this->push(ValueNode::ValueType{});
                        break;
                    }

                    this->push(result.value());
            } break;
            default: break;
        }
    }

}
