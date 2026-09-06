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

    void VM::storeVariable(
        const MirChunk& chunk,
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

    void VM::execOptional(ExecArgs& s) {
        const auto& instr = s.instr;
        Frame& frame = s.frame;

        auto value = this->regOf(frame, instr.src1);
        const bool isNone = std::holds_alternative<std::monostate>(value);

        switch (instr.op) {
            case MirOp::IS_NONE:
                this->setReg(frame, instr.dst, ValueNode::ValueType{ isNone });
                break;
            case MirOp::UNWRAP:
                if (isNone) {
                    this->diagnostics.addError(this->currentLoc, "Optional value is empty");
                    break;
                }

                this->setReg(frame, instr.dst, std::move(value));
                break;
            case MirOp::TYPE_OF:
                this->setReg(frame, instr.dst, ValueNode::ValueType{ VM::typeNameOf(value) });
                break;
            case MirOp::HAS_VALUE:
                this->setReg(frame, instr.dst, ValueNode::ValueType{ !isNone });
                break;
            default: break;
        }
    }

    void VM::execLocalSlot(ExecArgs& s) {
        const auto& instr = s.instr;
        Frame& frame = s.frame;

        switch (instr.op) {
            case MirOp::LOAD_SLOT: {
                auto value = this->regOf(frame, instr.operand);
                this->setReg(frame, instr.dst, std::move(value));
            } break;
            case MirOp::STORE_SLOT: {
                auto value = this->regOf(frame, instr.src1);
                this->setReg(frame, instr.operand, std::move(value));
            } break;
            default: break;
        }
    }

    void VM::execVariable(ExecArgs& s) {
        const auto& instr = s.instr;
        Frame& frame = s.frame;
        const MirChunk& cur = s.cur;
        const MirChunk& chunk = s.chunk;
        switch (instr.op) {
            case MirOp::LOAD_VAR: {
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

                            this->setReg(frame, instr.dst, *field);
                            break;
                        }
                    }

                    if (const auto* existing = obj->find(name)) {
                        this->setReg(frame, instr.dst, *existing);
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

                    this->setReg(frame, instr.dst, result.value());
                    break;
                }

                auto globalIt = this->globals->find(name);
                if (globalIt == this->globals->end()) {
                    this->diagnostics.addError(this->currentLoc, "Undefined variable: " + name);
                    break;
                }

                this->setReg(frame, instr.dst, globalIt->second);
            } break;
            case MirOp::STORE_VAR: {
                const auto& name = std::get<std::string>(cur.constants[instr.operand]);

                this->storeVariable(chunk, frame, name, this->regOf(frame, instr.src1));
            } break;
            default: break;
        }
    }

}
