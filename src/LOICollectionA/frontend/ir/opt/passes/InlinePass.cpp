#include <vector>

#include "LOICollectionA/frontend/ir/opt/passes/InlinePass.h"

namespace LOICollection::frontend::ir::opt {
    namespace {
        bool transfersControl(MirOp op) {
            switch (op) {
                case MirOp::CALL:
                case MirOp::CALL_METHOD:
                case MirOp::CALL_METHOD_VIRTUAL:
                case MirOp::CALL_METHOD_BY_NAME:
                case MirOp::CALL_FUNC:
                case MirOp::CALL_NATIVE_METHOD:
                case MirOp::CALL_LAMBDA:
                case MirOp::CALL_SUPER_CTOR:
                case MirOp::CALL_MACRO:
                case MirOp::MAKE_LAMBDA:
                case MirOp::BIND_THIS:
                case MirOp::NEW_NATIVE:
                case MirOp::JMP:
                case MirOp::JMP_IF_FALSE:
                case MirOp::JMP_IF_TRUE:
                    return true;
                default:
                    return false;
            }
        }

        bool isBranch(MirOp op) {
            return op == MirOp::JMP || op == MirOp::JMP_IF_FALSE || op == MirOp::JMP_IF_TRUE;
        }

        bool usesConstantPool(MirOp op) {
            return op == MirOp::LOAD_CONST || op == MirOp::LOAD_FIELD ||
                   op == MirOp::STORE_FIELD || op == MirOp::INSTANCEOF ||
                   op == MirOp::LOAD_VAR || op == MirOp::STORE_VAR;
        }
    }

    InlinePass::InlinePass(MirChunk& chunk) : mChunk(chunk) {}

    void InlinePass::remap(MirInstr& instr, int base) {
        if (instr.dst >= 0) instr.dst += base;
        if (instr.src1 >= 0) instr.src1 += base;
        if (instr.src2 >= 0) instr.src2 += base;
        if (instr.src3 >= 0) instr.src3 += base;
    }

    int InlinePass::importConstant(const MirChunk& body, int operand) {
        if (operand < 0 || operand >= static_cast<int>(body.constants.size()))
            return operand;

        const ValueNode::ValueType& value = body.constants[operand];

        for (size_t i = 0; i < mChunk.constants.size(); ++i)
            if (mChunk.constants[i] == value)
                return static_cast<int>(i);

        mChunk.constants.push_back(value);
        return static_cast<int>(mChunk.constants.size()) - 1;
    }

    bool InlinePass::derivedFrom(const ClassMeta& cls, int ancestor) const {
        for (const int index : cls.ancestorIndices)
            if (index == ancestor)
                return true;
        return false;
    }

    bool InlinePass::isFinal(int classIndex, int ordinal) const {
        if (classIndex < 0 || classIndex >= static_cast<int>(mChunk.classes.size()))
            return false;

        const ClassMeta& base = mChunk.classes[classIndex];
        if (ordinal < 0 || ordinal >= static_cast<int>(base.methods.size()))
            return false;

        const int baseIndex = base.methods[ordinal];
        if (baseIndex < 0 || baseIndex >= static_cast<int>(mChunk.methods.size()))
            return false;

        const int baseBody = mChunk.methods[baseIndex].bodyIndex;

        for (int i = 0; i < static_cast<int>(mChunk.classes.size()); ++i) {
            if (i == classIndex)
                continue;

            const ClassMeta& cls = mChunk.classes[i];
            if (!this->derivedFrom(cls, classIndex))
                continue;
            if (ordinal >= static_cast<int>(cls.methods.size()))
                return false;
            if (mChunk.methods[cls.methods[ordinal]].bodyIndex != baseBody)
                return false;
        }

        return true;
    }

    std::optional<InlinePass::ResolvedCall> InlinePass::resolveTarget(const MirInstr& call) const {
        if (call.op == MirOp::CALL_METHOD) {
            if (call.operand < 0 || call.operand >= static_cast<int>(mChunk.methods.size()))
                return std::nullopt;

            const MethodMeta& method = mChunk.methods[call.operand];
            if (method.bodyIndex < 0 ||
                method.bodyIndex >= static_cast<int>(mChunk.methodBodies.size()))
                return std::nullopt;

            return ResolvedCall{ method.bodyIndex, method.argCount };
        }

        if (call.op == MirOp::CALL_METHOD_VIRTUAL) {
            if (call.operand < 0 ||
                call.operand >= static_cast<int>(mChunk.virtualCalls.size()))
                return std::nullopt;

            const VirtualCallMeta& virt = mChunk.virtualCalls[call.operand];
            if (!this->isFinal(virt.classIndex, virt.ordinal))
                return std::nullopt;

            if (virt.classIndex < 0 ||
                virt.classIndex >= static_cast<int>(mChunk.classes.size()))
                return std::nullopt;

            const ClassMeta& cls = mChunk.classes[virt.classIndex];
            if (virt.ordinal < 0 || virt.ordinal >= static_cast<int>(cls.methods.size()))
                return std::nullopt;

            const int methodIndex = cls.methods[virt.ordinal];
            if (methodIndex < 0 || methodIndex >= static_cast<int>(mChunk.methods.size()))
                return std::nullopt;

            const MethodMeta& method = mChunk.methods[methodIndex];
            if (method.bodyIndex < 0 ||
                method.bodyIndex >= static_cast<int>(mChunk.methodBodies.size()))
                return std::nullopt;

            return ResolvedCall{ method.bodyIndex, method.argCount };
        }

        return std::nullopt;
    }

    bool InlinePass::inlinable(const MirChunk& body) const {
        
        
        
        
        
        
        
        for (const MirInstr& instr : body.code) {
            const MirOp op = instr.op;
            if (op == MirOp::RETURN)
                return true;
            if (transfersControl(op) || op == MirOp::HALT ||
                op == MirOp::LOAD_VAR || op == MirOp::STORE_VAR)
                return false;
        }

        return false;
    }

    std::size_t InlinePass::run(int maxBodySize) {
        const int size = static_cast<int>(mChunk.code.size());

        std::vector<MirInstr> code;
        std::vector<int> oldIndex;
        std::vector<int> newIndex(size, -1);

        auto emit = [&code, &oldIndex](const MirInstr& instr, int origin) -> void {
            code.push_back(instr);
            oldIndex.push_back(origin);
        };

        int slotBase = mChunk.slotCount;
        std::size_t inlined = 0;

        for (int i = 0; i < size; ++i) {
            const MirInstr& call = mChunk.code[i];

            const auto target = this->resolveTarget(call);
            if (target && target->bodyIndex >= 0 &&
                target->bodyIndex < static_cast<int>(mChunk.methodBodies.size()) &&
                static_cast<int>(mChunk.methodBodies[target->bodyIndex]->code.size()) <= maxBodySize &&
                this->inlinable(*mChunk.methodBodies[target->bodyIndex])) {
                newIndex[i] = static_cast<int>(code.size());

                const MirChunk& body = *mChunk.methodBodies[target->bodyIndex];
                const int base = slotBase;
                const int receiver = call.src1 + call.imm;

                for (int a = 0; a < target->argCount; ++a)
                    emit({ MirOp::MOVE, 0, base + a, call.src1 + a, -1, -1, 0, {}, call.loc }, -1);

                const std::size_t bodySize = body.code.size();

                
                
                
                
                std::size_t retIdx = bodySize;
                for (std::size_t r = 0; r < bodySize; ++r) {
                    if (body.code[r].op == MirOp::RETURN) {
                        retIdx = r;
                        break;
                    }
                }

                for (std::size_t b = 0; b < retIdx; ++b) {
                    MirInstr instr = body.code[b];
                    if (instr.op == MirOp::LOAD_THIS) {
                        emit({ MirOp::MOVE, 0, instr.dst + base, receiver, -1, -1, 0, {}, instr.loc }, -1);
                        continue;
                    }
                    if (usesConstantPool(instr.op))
                        instr.operand = this->importConstant(body, instr.operand);
                    if (instr.op == MirOp::LOAD_SLOT || instr.op == MirOp::STORE_SLOT)
                        instr.operand += base;
                    remap(instr, base);
                    emit(instr, -1);
                }

                if (retIdx < bodySize) {
                    const int retSrc = body.code[retIdx].src1;
                    if (call.dst >= 0 && retSrc >= 0)
                        emit({ MirOp::MOVE, 0, call.dst, retSrc + base, -1, -1, 0, {}, call.loc }, -1);
                }

                slotBase = base + body.slotCount;
                ++inlined;
                continue;
            }

            newIndex[i] = static_cast<int>(code.size());
            emit(call, i);
        }

        for (std::size_t n = 0; n < code.size(); ++n) {
            MirInstr& instr = code[n];
            if (!isBranch(instr.op) || oldIndex[n] < 0)
                continue;

            const int target = oldIndex[n] + 1 + instr.operand;
            if (target >= 0 && target < size && newIndex[target] >= 0) {
                instr.operand = newIndex[target] - static_cast<int>(n) - 1;
            } else if (target == size) {
                instr.operand = static_cast<int>(code.size()) - static_cast<int>(n) - 1;
            }
        }

        mChunk.slotCount = slotBase;
        mChunk.code = std::move(code);

        return inlined;
    }
}