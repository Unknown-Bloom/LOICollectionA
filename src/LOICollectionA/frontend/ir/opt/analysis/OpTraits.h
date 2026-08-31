#pragma once

#include <string>

#include "LOICollectionA/frontend/ir/OpCode.h"

namespace LOICollection::frontend::ir::opt {
    inline bool isJump(OpCode op) {
        return op == OpCode::JMP || op == OpCode::JMP_IF_FALSE || op == OpCode::JMP_IF_TRUE;
    }

    inline bool isTerminator(OpCode op) {
        return op == OpCode::JMP || op == OpCode::RETURN || op == OpCode::HALT;
    }

    inline bool isStoreOp(OpCode op) {
        return op == OpCode::STORE_VAR || op == OpCode::STORE_SLOT;
    }

    // Opcodes that may execute script code (function bodies, lambdas, constructors) or
    // create object fields; any of these can rebind what a later LOAD_VAR resolves to.
    inline bool canWriteVariables(OpCode op) {
        switch (op) {
            case OpCode::CALL:
            case OpCode::CALL_MACRO:
            case OpCode::CALL_METHOD:
            case OpCode::CALL_METHOD_VIRTUAL:
            case OpCode::CALL_FUNC:
            case OpCode::CALL_LAMBDA:
            case OpCode::CALL_SUPER_CTOR:
            case OpCode::NEW:
            case OpCode::STORE_FIELD:
                return true;
            default:
                return false;
        }
    }

    std::string arithmeticOpName(OpCode op);
    std::string comparisonOpName(OpCode op);
}
