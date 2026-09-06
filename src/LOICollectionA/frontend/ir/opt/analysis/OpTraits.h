#pragma once

#include <string>

#include "LOICollectionA/frontend/ir/Mir.h"

namespace LOICollection::frontend::ir::opt {
    inline bool isJump(MirOp op) {
        return op == MirOp::JMP || op == MirOp::JMP_IF_FALSE || op == MirOp::JMP_IF_TRUE;
    }

    inline bool isTerminator(MirOp op) {
        return op == MirOp::JMP || op == MirOp::RETURN || op == MirOp::HALT;
    }

    inline bool isStoreOp(MirOp op) {
        return op == MirOp::STORE_VAR || op == MirOp::STORE_SLOT;
    }

    inline bool isPureBinary(MirOp op) {
        switch (op) {
            case MirOp::ADD: case MirOp::SUB: case MirOp::MUL: case MirOp::DIV:
            case MirOp::MOD: case MirOp::POW:
            case MirOp::CMP_EQ: case MirOp::CMP_NE: case MirOp::CMP_GT: case MirOp::CMP_LT:
            case MirOp::CMP_GE: case MirOp::CMP_LE:
            case MirOp::LOGIC_AND: case MirOp::LOGIC_OR:
                return true;
            default:
                return false;
        }
    }

    inline bool isPureUnary(MirOp op) {
        switch (op) {
            case MirOp::NEG:
            case MirOp::NOT:
            case MirOp::INSTANCEOF:
            case MirOp::LOAD_LEN:
                return true;
            default:
                return false;
        }
    }

    inline bool canWriteVariables(MirOp op) {
        switch (op) {
            case MirOp::CALL:
            case MirOp::CALL_MACRO:
            case MirOp::CALL_METHOD:
            case MirOp::CALL_METHOD_VIRTUAL:
            case MirOp::CALL_METHOD_BY_NAME:
            case MirOp::CALL_FUNC:
            case MirOp::CALL_NATIVE_METHOD:
            case MirOp::CALL_LAMBDA:
            case MirOp::CALL_SUPER_CTOR:
            case MirOp::NEW:
            case MirOp::STORE_FIELD:
                return true;
            default:
                return false;
        }
    }
}
