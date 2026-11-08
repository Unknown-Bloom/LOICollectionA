#pragma once

#include <string>

#include "LOICollectionA/frontend/ir/Lir.h"

namespace LOICollection::frontend::ir::opt {
    inline bool isJump(Op op) {
        return op == Op::JMP || op == Op::JMP_IF_FALSE || op == Op::JMP_IF_TRUE
            || op == Op::JMP_IF_CMP_FALSE || op == Op::JMP_IF_TAG_NE;
    }

    inline bool isTerminator(Op op) {
        return op == Op::JMP || op == Op::RETURN || op == Op::HALT ||
               op == Op::PANIC;
    }

    inline bool isStoreOp(Op op) {
        return op == Op::STORE_VAR || op == Op::STORE_SLOT;
    }

    inline bool isPureBinary(Op op) {
        switch (op) {
            case Op::ADD: case Op::SUB: case Op::MUL: case Op::DIV:
            case Op::MOD: case Op::POW:
            case Op::CMP_EQ: case Op::CMP_NE: case Op::CMP_GT: case Op::CMP_LT:
            case Op::CMP_GE: case Op::CMP_LE:
            case Op::LOGIC_AND: case Op::LOGIC_OR:
                return true;
            default:
                return false;
        }
    }

    inline bool isPureUnary(Op op) {
        switch (op) {
            case Op::NEG:
            case Op::NOT:
            case Op::INSTANCEOF:
            case Op::LOAD_LEN:
            case Op::TAG_OF:
            case Op::LOAD_ENUM_PAYLOAD:
                return true;
            default:
                return false;
        }
    }

    inline bool canWriteVariables(Op op) {
        switch (op) {
            case Op::CALL:
            case Op::CALL_MACRO:
            case Op::CALL_METHOD:
            case Op::CALL_METHOD_VIRTUAL:
            case Op::CALL_METHOD_BY_NAME:
            case Op::CALL_FUNC:
            case Op::CALL_NATIVE_METHOD:
            case Op::CALL_LAMBDA:
            case Op::CALL_SUPER_CTOR:
            case Op::NEW:
            case Op::STORE_FIELD:
                return true;
            default:
                return false;
        }
    }
}
