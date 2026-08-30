#pragma once

#include <cstdint>

namespace LOICollection::frontend::ir {
    enum class OpCode : uint8_t {
        PUSH_INT, PUSH_FLOAT, PUSH_STR, PUSH_BOOL, PUSH_NONE,
        POP, DUP, DUP2, ROT3, SWAP2, UNWRAP, TYPE_OF, HAS_VALUE, IS_NONE,

        LOAD_VAR, STORE_VAR,
        LOAD_SLOT, STORE_SLOT,

        ADD, SUB, MUL, DIV, MOD, POW,

        CMP_EQ, CMP_NE, CMP_GT, CMP_LT, CMP_GE, CMP_LE,

        LOGIC_AND, LOGIC_OR,

        NEG, NOT,

        CALL,
        CALL_MACRO,
        CALL_METHOD,
        CALL_METHOD_VIRTUAL,
        CALL_FUNC,
        CALL_NATIVE_METHOD,
        CALL_LAMBDA,
        CALL_SUPER_CTOR,

        NEW,
        NEW_NATIVE,
        LOAD_FIELD, STORE_FIELD,
        MAKE_ARRAY,
        LOAD_INDEX, STORE_INDEX,
        LOAD_THIS,
        MAKE_LAMBDA,
        INSTANCEOF,
        RETURN,

        JMP_IF_FALSE, JMP_IF_TRUE,
        JMP,
        HALT,

        DUP_STORE,
        DUP_STORE_SLOT,
        DUP_IS_NONE,
        LOAD_LEN,
        BIND_THIS,
        LOAD_FIELD_SLOT,
        STORE_FIELD_SLOT,

        COUNT
    };
}
