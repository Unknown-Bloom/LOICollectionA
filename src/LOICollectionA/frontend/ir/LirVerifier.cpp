#include <cstddef>
#include <string>

#include "LOICollectionA/frontend/ir/LirVerifier.h"

namespace LOICollection::frontend::ir {
    namespace detail {
        bool isRegister(int value, int slotCount) {
            return value == -1 || (value >= 0 && value < slotCount);
        }

        bool isConstantIndex(const Chunk& chunk, int index) {
            return index >= 0 && static_cast<std::size_t>(index) < chunk.constants.size();
        }

        bool verifyInstr(const Chunk& chunk, const Chunk& metadata, const Instr& instr, DiagnosticEngine& diagnostics) {
            if (!isRegister(instr.dst, chunk.slotCount) ||
                !isRegister(instr.src1, chunk.slotCount) ||
                !isRegister(instr.src2, chunk.slotCount) ||
                !isRegister(instr.src3, chunk.slotCount)) {
                diagnostics.addError(instr.loc, "LIR register index out of range");
                return false;
            }

            switch (instr.op) {
                case Op::LOAD_CONST:
                    if (!isConstantIndex(chunk, instr.operand)) {
                        diagnostics.addError(instr.loc, "LIR constant index out of range");
                        return false;
                    }
                    break;
                case Op::LOAD_VAR:
                case Op::STORE_VAR:
                    if (!isConstantIndex(chunk, instr.operand)) {
                        diagnostics.addError(instr.loc, "LIR global name index out of range");
                        return false;
                    }
                    break;
                case Op::LOAD_FIELD:
                case Op::STORE_FIELD:
                    if (!isConstantIndex(chunk, instr.operand)) {
                        diagnostics.addError(instr.loc, "LIR field name index out of range");
                        return false;
                    }
                    break;
                case Op::INSTANCEOF:
                    if (!isConstantIndex(chunk, instr.operand)) {
                        diagnostics.addError(instr.loc, "LIR class name index out of range");
                        return false;
                    }
                    break;
                case Op::NEW:
                    if (instr.operand < 0 || static_cast<std::size_t>(instr.operand) >= metadata.classes.size()) {
                        diagnostics.addError(instr.loc, "LIR class index out of range");
                        return false;
                    }
                    break;
                case Op::NEW_NATIVE:
                case Op::CALL_NATIVE_METHOD:
                    if (instr.operand < 0 || static_cast<std::size_t>(instr.operand) >= metadata.nativeCalls.size()) {
                        diagnostics.addError(instr.loc, "LIR native call index out of range");
                        return false;
                    }
                    break;
                case Op::CALL:
                    if (instr.operand < 0 || static_cast<std::size_t>(instr.operand) >= chunk.functions.size()) {
                        diagnostics.addError(instr.loc, "LIR function index out of range");
                        return false;
                    }
                    break;
                case Op::CALL_FUNC:
                case Op::CALL_METHOD:
                    if (instr.operand < 0 || static_cast<std::size_t>(instr.operand) >= metadata.methods.size()) {
                        diagnostics.addError(instr.loc, "LIR method index out of range");
                        return false;
                    }
                    break;
                case Op::CALL_MACRO:
                    if (instr.operand < 0 || static_cast<std::size_t>(instr.operand) >= chunk.macros.size()) {
                        diagnostics.addError(instr.loc, "LIR macro index out of range");
                        return false;
                    }
                    break;
                case Op::CALL_METHOD_VIRTUAL:
                    if (instr.operand < 0 || static_cast<std::size_t>(instr.operand) >= chunk.virtualCalls.size()) {
                        diagnostics.addError(instr.loc, "LIR virtual call index out of range");
                        return false;
                    }
                    break;
                case Op::CALL_METHOD_BY_NAME:
                    if (instr.operand < 0 || static_cast<std::size_t>(instr.operand) >= chunk.byNameCalls.size()) {
                        diagnostics.addError(instr.loc, "LIR by-name call index out of range");
                        return false;
                    }
                    break;
                case Op::CALL_SUPER_CTOR:
                    if (instr.operand < 0 || static_cast<std::size_t>(instr.operand) >= chunk.superCalls.size()) {
                        diagnostics.addError(instr.loc, "LIR super call index out of range");
                        return false;
                    }
                    break;
                case Op::MAKE_LAMBDA:
                    if (instr.operand < 0 || static_cast<std::size_t>(instr.operand) >= chunk.lambdas.size()) {
                        diagnostics.addError(instr.loc, "LIR lambda index out of range");
                        return false;
                    }
                    break;
                case Op::MAKE_ENUM:
                    if (instr.operand < 0 || static_cast<std::size_t>(instr.operand) >= metadata.enums.size()) {
                        diagnostics.addError(instr.loc, "LIR enum index out of range");
                        return false;
                    }
                    break;
                default:
                    break;
            }

            return true;
        }

        bool verifyChunk(const Chunk& chunk, const Chunk& metadata, DiagnosticEngine& diagnostics) {
            if (chunk.slotCount < 0) {
                diagnostics.addError({}, "LIR slot count is negative");
                return false;
            }

            for (std::size_t index = 0; index < chunk.code.size(); ++index) {
                const Instr& instr = chunk.code[index];
                if (!verifyInstr(chunk, metadata, instr, diagnostics))
                    return false;

                if (instr.op == Op::JMP || instr.op == Op::JMP_IF_FALSE ||
                    instr.op == Op::JMP_IF_TRUE || instr.op == Op::JMP_IF_TAG_NE ||
                    instr.op == Op::JMP_IF_CMP_FALSE) {
                    const long long target = static_cast<long long>(index) + 1 + instr.operand;
                    if (target < 0 || target > static_cast<long long>(chunk.code.size())) {
                        diagnostics.addError(instr.loc, "LIR jump target out of range");
                        return false;
                    }
                }
            }

            for (const LambdaMeta& lambda : chunk.lambdas) {
                if (lambda.bodyIndex < 0 ||
                    static_cast<std::size_t>(lambda.bodyIndex) >= metadata.methodBodies.size()) {
                    diagnostics.addError({}, "LIR lambda body index out of range");
                    return false;
                }
            }

            for (const auto& body : chunk.methodBodies) {
                if (body && !verifyChunk(*body, metadata, diagnostics))
                    return false;
            }

            return true;
        }
    }

    bool LirVerifier::verify(const Chunk& chunk, DiagnosticEngine& diagnostics) {
        if (!detail::verifyChunk(chunk, chunk, diagnostics))
            return false;

        for (const MethodMeta& method : chunk.methods) {
            if (method.bodyIndex < 0 ||
                static_cast<std::size_t>(method.bodyIndex) >= chunk.methodBodies.size()) {
                diagnostics.addError({}, "LIR method body index out of range");
                return false;
            }
        }

        for (const LambdaMeta& lambda : chunk.lambdas) {
            if (lambda.bodyIndex < 0 ||
                static_cast<std::size_t>(lambda.bodyIndex) >= chunk.methodBodies.size()) {
                diagnostics.addError({}, "LIR lambda body index out of range");
                return false;
            }
        }

        return true;
    }
}
