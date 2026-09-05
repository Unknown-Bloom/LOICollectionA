#include "LOICollectionA/frontend/ir/MirLowering.h"

namespace LOICollection::frontend::ir {
    namespace {
        OpCode lowerOp(MirOp op, const TypeInfo& type) {
            const bool isInt = type.kind == TypeKind::Int;
            switch (op) {
                case MirOp::ADD: return isInt ? OpCode::ADD_I : OpCode::ADD;
                case MirOp::SUB: return isInt ? OpCode::SUB_I : OpCode::SUB;
                case MirOp::MUL: return isInt ? OpCode::MUL_I : OpCode::MUL;
                case MirOp::MOD: return isInt ? OpCode::MOD_I : OpCode::MOD;
                case MirOp::CMP_EQ: return isInt ? OpCode::CMP_EQ_I : OpCode::CMP_EQ;
                case MirOp::CMP_NE: return isInt ? OpCode::CMP_NE_I : OpCode::CMP_NE;
                case MirOp::CMP_GT: return isInt ? OpCode::CMP_GT_I : OpCode::CMP_GT;
                case MirOp::CMP_LT: return isInt ? OpCode::CMP_LT_I : OpCode::CMP_LT;
                case MirOp::CMP_GE: return isInt ? OpCode::CMP_GE_I : OpCode::CMP_GE;
                case MirOp::CMP_LE: return isInt ? OpCode::CMP_LE_I : OpCode::CMP_LE;
                case MirOp::NEG: return isInt ? OpCode::NEG_I : OpCode::NEG;
                case MirOp::PUSH_INT: return OpCode::PUSH_INT;
                case MirOp::PUSH_FLOAT: return OpCode::PUSH_FLOAT;
                case MirOp::PUSH_STR: return OpCode::PUSH_STR;
                case MirOp::PUSH_BOOL: return OpCode::PUSH_BOOL;
                case MirOp::PUSH_NONE: return OpCode::PUSH_NONE;
                case MirOp::POP: return OpCode::POP;
                case MirOp::DUP: return OpCode::DUP;
                case MirOp::DUP2: return OpCode::DUP2;
                case MirOp::ROT3: return OpCode::ROT3;
                case MirOp::SWAP2: return OpCode::SWAP2;
                case MirOp::UNWRAP: return OpCode::UNWRAP;
                case MirOp::TYPE_OF: return OpCode::TYPE_OF;
                case MirOp::HAS_VALUE: return OpCode::HAS_VALUE;
                case MirOp::IS_NONE: return OpCode::IS_NONE;
                case MirOp::LOAD_VAR: return OpCode::LOAD_VAR;
                case MirOp::STORE_VAR: return OpCode::STORE_VAR;
                case MirOp::LOAD_SLOT: return OpCode::LOAD_SLOT;
                case MirOp::STORE_SLOT: return OpCode::STORE_SLOT;
                case MirOp::DIV: return OpCode::DIV;
                case MirOp::POW: return OpCode::POW;
                case MirOp::LOGIC_AND: return OpCode::LOGIC_AND;
                case MirOp::LOGIC_OR: return OpCode::LOGIC_OR;
                case MirOp::NOT: return OpCode::NOT;
                case MirOp::CALL: return OpCode::CALL;
                case MirOp::CALL_MACRO: return OpCode::CALL_MACRO;
                case MirOp::CALL_METHOD: return OpCode::CALL_METHOD;
                case MirOp::CALL_METHOD_VIRTUAL: return OpCode::CALL_METHOD_VIRTUAL;
                case MirOp::CALL_METHOD_BY_NAME: return OpCode::CALL_METHOD_BY_NAME;
                case MirOp::CALL_FUNC: return OpCode::CALL_FUNC;
                case MirOp::CALL_NATIVE_METHOD: return OpCode::CALL_NATIVE_METHOD;
                case MirOp::CALL_LAMBDA: return OpCode::CALL_LAMBDA;
                case MirOp::CALL_SUPER_CTOR: return OpCode::CALL_SUPER_CTOR;
                case MirOp::NEW: return OpCode::NEW;
                case MirOp::NEW_NATIVE: return OpCode::NEW_NATIVE;
                case MirOp::LOAD_FIELD: return OpCode::LOAD_FIELD;
                case MirOp::STORE_FIELD: return OpCode::STORE_FIELD;
                case MirOp::LOAD_FIELD_SLOT: return OpCode::LOAD_FIELD_SLOT;
                case MirOp::STORE_FIELD_SLOT: return OpCode::STORE_FIELD_SLOT;
                case MirOp::MAKE_ARRAY: return OpCode::MAKE_ARRAY;
                case MirOp::LOAD_INDEX: return OpCode::LOAD_INDEX;
                case MirOp::STORE_INDEX: return OpCode::STORE_INDEX;
                case MirOp::LOAD_THIS: return OpCode::LOAD_THIS;
                case MirOp::MAKE_LAMBDA: return OpCode::MAKE_LAMBDA;
                case MirOp::INSTANCEOF: return OpCode::INSTANCEOF;
                case MirOp::RETURN: return OpCode::RETURN;
                case MirOp::JMP_IF_FALSE: return OpCode::JMP_IF_FALSE;
                case MirOp::JMP_IF_TRUE: return OpCode::JMP_IF_TRUE;
                case MirOp::JMP: return OpCode::JMP;
                case MirOp::HALT: return OpCode::HALT;
                case MirOp::BIND_THIS: return OpCode::BIND_THIS;
                case MirOp::LOAD_LEN: return OpCode::LOAD_LEN;
                case MirOp::COUNT: break;
            }
            return OpCode::HALT;
        }
    }

    BytecodeChunk MirLowering::lower(const MirChunk& chunk) {
        BytecodeChunk out;
        out.constants = chunk.constants;
        out.functions = chunk.functions;
        out.macros = chunk.macros;
        out.classes = chunk.classes;
        out.methods = chunk.methods;
        out.nativeCalls = chunk.nativeCalls;
        out.virtualCalls = chunk.virtualCalls;
        out.byNameCalls = chunk.byNameCalls;
        out.superCalls = chunk.superCalls;
        out.lambdas = chunk.lambdas;
        out.slotCount = chunk.slotCount;

        out.code.reserve(chunk.code.size());
        for (const auto& instr : chunk.code)
            out.code.push_back({ lowerOp(instr.op, instr.type), instr.operand, instr.loc });

        for (const auto& body : chunk.methodBodies)
            out.methodBodies.push_back(std::make_unique<BytecodeChunk>(lower(*body)));

        return out;
    }
}
