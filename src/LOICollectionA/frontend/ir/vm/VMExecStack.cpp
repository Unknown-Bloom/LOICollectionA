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
        bool splitStaticMemberName(const std::string& name, std::string& className, std::string& fieldName) {
            auto pos = name.find("::");
            if (pos == std::string::npos || pos == 0 || pos + 2 >= name.size())
                return false;

            className = name.substr(0, pos);
            fieldName = name.substr(pos + 2);
            return !className.empty() && !fieldName.empty();
        }
    }

    void VM::push(const ValueNode::ValueType& v) {
        this->stack.push_back(v);
    }

    void VM::push(ValueNode::ValueType&& v) {
        this->stack.push_back(std::move(v));
    }

    ValueNode::ValueType VM::pop() {
        if (this->stack.empty()) {
            this->diagnostics.addError(this->currentLoc, "Stack underflow");
            return ValueNode::ValueType{};
        }

        ValueNode::ValueType v = std::move(this->stack.back());
        this->stack.pop_back();

        return v;
    }

    void VM::storeVariable(
        const BytecodeChunk& chunk,
        Frame& frame,
        const std::string& name,
        const ValueNode::ValueType& val
    ) {
        if (frame.hasThis) {
            auto obj = std::get<ObjectRef>(frame.thisObj);

            if (obj->classIndex >= 0) {
                const auto& cls = chunk.classes[obj->classIndex];
                bool isField = std::ranges::find(cls.fieldNames, name) != cls.fieldNames.end();

                if (isField) {
                    obj->assign(name, val);
                    return;
                }
            }

            if (auto* existing = obj->find(name)) {
                *existing = val;
                return;
            }
        }

        std::string className;
        std::string fieldName;
        if (splitStaticMemberName(name, className, fieldName) &&
            std::ranges::none_of(chunk.classes, [&className](const auto& cls) {
                return cls.name == className;
            }) &&
            ClassCall::getInstance().isRegistered(className) &&
            ClassCall::getInstance().hasStaticField(className, fieldName)) {
            ClassCall::getInstance().setStaticField(className, fieldName, val);
            return;
        }

        (*this->globals)[name] = val;
    }

    bool VM::pushFrame(Frame&& frame) {
        if (this->frames.size() >= this->mBudget->maxFrames) {
            this->diagnostics.addError(this->currentLoc, "Call stack depth limit exceeded");
            return false;
        }

        frame.localsBase = this->localPool.size();
        this->localPool.resize(this->localPool.size() + frame.localsSize);
        this->frames.push_back(std::move(frame));
        return true;
    }

    void VM::execPushConst(ExecArgs& s) {
        const auto& instr = s.instr;
        const BytecodeChunk& cur = s.cur;
        switch (instr.op) {
            case OpCode::PUSH_INT: {
                    this->push(VM::cloneValue(cur.constants[instr.operand]));
            } break;
            case OpCode::PUSH_FLOAT: {
                    this->push(VM::cloneValue(cur.constants[instr.operand]));
            } break;
            case OpCode::PUSH_BOOL: {
                    this->push(VM::cloneValue(cur.constants[instr.operand]));
            } break;
            case OpCode::PUSH_NONE: {
                    this->push(VM::cloneValue(cur.constants[instr.operand]));
            } break;
            case OpCode::PUSH_STR: {
                    const auto& value = std::get<std::string>(cur.constants[instr.operand]);
                    if (const auto violation = this->mBudget->accountString(value.size());
                        violation != sandbox::SandboxBudget::Violation::None) {
                        this->failBudget(violation, "String size budget exhausted");
                        break;
                    }

                    this->push(value);
            } break;
            default: break;
        }
    }

    void VM::execStackManip(ExecArgs& s) {
        const auto& instr = s.instr;
        switch (instr.op) {
            case OpCode::POP: {
                    this->pop();
            } break;
            case OpCode::DUP: {
                    if (this->stack.empty()) {
                        this->diagnostics.addError(this->currentLoc, "Stack underflow during DUP");
                        break;
                    }

                    this->stack.push_back(this->stack.back());
            } break;
            case OpCode::DUP2: {
                    if (this->stack.size() < 2) {
                        this->diagnostics.addError(this->currentLoc, "Stack underflow during DUP2");
                        break;
                    }

                    auto second = this->stack[this->stack.size() - 2];
                    this->stack.push_back(second);
                    this->stack.push_back(this->stack[this->stack.size() - 2]);
            } break;
            case OpCode::ROT3: {
                    if (this->stack.size() < 3) {
                        this->diagnostics.addError(this->currentLoc, "Stack underflow during ROT3");
                        break;
                    }

                    auto bottom = std::move(this->stack[this->stack.size() - 3]);
                    this->stack.erase(this->stack.end() - 3);
                    this->stack.push_back(std::move(bottom));
            } break;
            case OpCode::SWAP2: {
                    if (this->stack.size() < 4) {
                        this->diagnostics.addError(this->currentLoc, "Stack underflow during SWAP2");
                        break;
                    }

                    std::swap(this->stack[this->stack.size() - 4], this->stack[this->stack.size() - 2]);
                    std::swap(this->stack[this->stack.size() - 3], this->stack[this->stack.size() - 1]);
            } break;
            default: break;
        }
    }

    void VM::execOptional(ExecArgs& s) {
        const auto& instr = s.instr;
        switch (instr.op) {
            case OpCode::IS_NONE: {
                    auto value = this->pop();
                    this->push(std::holds_alternative<std::monostate>(value));
            } break;
            case OpCode::UNWRAP: {
                    auto value = this->pop();
                    if (std::holds_alternative<std::monostate>(value)) {
                        this->diagnostics.addError(this->currentLoc, "Optional value is empty");
                        break;
                    }

                    this->push(value);
            } break;
            case OpCode::TYPE_OF: {
                    auto value = this->pop();
                    this->push(VM::typeNameOf(value));
            } break;
            case OpCode::HAS_VALUE: {
                    auto value = this->pop();
                    this->push(!std::holds_alternative<std::monostate>(value));
            } break;
            case OpCode::DUP_IS_NONE: {
                    if (this->stack.empty()) {
                        this->diagnostics.addError(this->currentLoc, "Stack underflow during DUP_IS_NONE");
                        break;
                    }

                    this->push(ValueNode::ValueType{
                        std::holds_alternative<std::monostate>(this->stack.back())
                    });
            } break;
            default: break;
        }
    }

    void VM::execLocalSlot(ExecArgs& s) {
        const auto& instr = s.instr;
        Frame& frame = s.frame;
        switch (instr.op) {
            case OpCode::LOAD_SLOT: {
                    if (instr.operand < 0 || static_cast<size_t>(instr.operand) >= frame.localsSize) {
                        this->diagnostics.addError(this->currentLoc, "Slot index out of range");
                        break;
                    }

                    this->push(this->localPool[frame.localsBase + instr.operand]);
            } break;
            case OpCode::STORE_SLOT: {
                    if (instr.operand < 0 || static_cast<size_t>(instr.operand) >= frame.localsSize) {
                        this->diagnostics.addError(this->currentLoc, "Slot index out of range");
                        break;
                    }

                    this->localPool[frame.localsBase + instr.operand] = this->pop();
            } break;
            case OpCode::DUP_STORE_SLOT: {
                    if (this->stack.empty()) {
                        this->diagnostics.addError(this->currentLoc, "Stack underflow during DUP_STORE_SLOT");
                        break;
                    }

                    if (instr.operand < 0 || static_cast<size_t>(instr.operand) >= frame.localsSize) {
                        this->diagnostics.addError(this->currentLoc, "Slot index out of range");
                        break;
                    }

                    this->localPool[frame.localsBase + instr.operand] = this->stack.back();
            } break;
            default: break;
        }
    }

    void VM::execVariable(ExecArgs& s) {
        const auto& instr = s.instr;
        Frame& frame = s.frame;
        const BytecodeChunk& cur = s.cur;
        const BytecodeChunk& chunk = s.chunk;
        switch (instr.op) {
            case OpCode::LOAD_VAR: {
                    const auto& name = std::get<std::string>(cur.constants[instr.operand]);

                    if (frame.hasThis) {
                        auto obj = std::get<ObjectRef>(frame.thisObj);

                        if (obj->classIndex >= 0) {
                            const auto& cls = chunk.classes[obj->classIndex];
                            bool isField = std::ranges::find(cls.fieldNames, name) != cls.fieldNames.end();

                            if (isField) {
                                const auto* field = obj->find(name);
                                if (!field) {
                                    this->diagnostics.addError(this->currentLoc, "Object has no field: " + name);
                                    break;
                                }

                                this->push(*field);
                                break;
                            }
                        }

                        if (const auto* existing = obj->find(name)) {
                            this->push(*existing);
                            break;
                        }
                    }

                    std::string className;
                    std::string fieldName;
                    if (splitStaticMemberName(name, className, fieldName) &&
                        std::ranges::none_of(chunk.classes, [&className](const auto& cls) {
                            return cls.name == className;
                        }) &&
                        ClassCall::getInstance().isRegistered(className) &&
                        ClassCall::getInstance().hasStaticField(className, fieldName)) {
                        auto result = ClassCall::getInstance().getStaticField(className, fieldName);
                        if (!result.has_value()) {
                            this->diagnostics.addError(this->currentLoc,
                                "Failed to load native static field '" + name + "': " + result.error().message());
                            break;
                        }

                        this->push(result.value());
                        break;
                    }

                    auto globalIt = this->globals->find(name);
                    if (globalIt == this->globals->end()) {
                        this->diagnostics.addError(this->currentLoc, "Undefined variable: " + name);
                        break;
                    }

                    this->push(globalIt->second);
            } break;
            case OpCode::STORE_VAR: {
                    const auto& name = std::get<std::string>(cur.constants[instr.operand]);

                    auto val = this->pop();

                    this->storeVariable(chunk, frame, name, val);
            } break;
            case OpCode::DUP_STORE: {
                    if (this->stack.empty()) {
                        this->diagnostics.addError(this->currentLoc, "Stack underflow during DUP_STORE");
                        break;
                    }

                    const auto& name = std::get<std::string>(cur.constants[instr.operand]);
                    this->storeVariable(chunk, frame, name, this->stack.back());
            } break;
            default: break;
        }
    }

}
