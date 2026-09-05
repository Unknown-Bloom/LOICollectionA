#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "LOICollectionA/frontend/AST.h"
#include "LOICollectionA/frontend/ir/ByteCode.h"

namespace LOICollection::frontend::ir {
    enum class MirOp : uint8_t {
        PUSH_INT, PUSH_FLOAT, PUSH_STR, PUSH_BOOL, PUSH_NONE,
        POP, DUP, DUP2, ROT3, SWAP2, UNWRAP, TYPE_OF, HAS_VALUE, IS_NONE,

        LOAD_VAR, STORE_VAR,
        LOAD_SLOT, STORE_SLOT,
        DUP_STORE, DUP_STORE_SLOT, DUP_IS_NONE,

        ADD, SUB, MUL, DIV, MOD, POW,

        CMP_EQ, CMP_NE, CMP_GT, CMP_LT, CMP_GE, CMP_LE,

        LOGIC_AND, LOGIC_OR,

        NEG, NOT,

        ADD_SS, SUB_SS, MUL_SS, MOD_SS,
        CMP_EQ_SS, CMP_NE_SS, CMP_GT_SS, CMP_LT_SS, CMP_GE_SS, CMP_LE_SS,

        CALL,
        CALL_MACRO,
        CALL_METHOD,
        CALL_METHOD_VIRTUAL,
        CALL_METHOD_BY_NAME,
        CALL_FUNC,
        CALL_NATIVE_METHOD,
        CALL_LAMBDA,
        CALL_SUPER_CTOR,

        NEW,
        NEW_NATIVE,
        LOAD_FIELD, STORE_FIELD,
        LOAD_FIELD_SLOT, STORE_FIELD_SLOT,
        MAKE_ARRAY,
        LOAD_INDEX, STORE_INDEX,
        LOAD_THIS,
        MAKE_LAMBDA,
        INSTANCEOF,
        RETURN,

        JMP_IF_FALSE, JMP_IF_TRUE,
        JMP,
        HALT,

        BIND_THIS,
        LOAD_LEN,

        COUNT
    };

    struct MirInstr {
        MirOp op;
        int operand = 0;
        SourceLocation loc{};
        TypeInfo type{};
    };

    struct MirChunk {
        std::vector<MirInstr> code;
        std::vector<ValueNode::ValueType> constants;
        std::vector<FuncMeta> functions;
        std::vector<MacroMeta> macros;
        std::vector<ClassMeta> classes;
        std::vector<MethodMeta> methods;
        std::vector<NativeCallMeta> nativeCalls;
        std::vector<VirtualCallMeta> virtualCalls;
        std::vector<ByNameCallMeta> byNameCalls;
        std::vector<SuperCallMeta> superCalls;
        std::vector<LambdaMeta> lambdas;

        std::deque<std::unique_ptr<MirChunk>> methodBodies;

        int slotCount = 0;

        size_t emit(MirOp op, int operand = 0, const SourceLocation& loc = {}, const TypeInfo& type = {}) {
            this->code.push_back({ op, operand, loc, type });
            return this->code.size() - 1;
        }

        [[nodiscard]] size_t currentIP() const {
            return this->code.size();
        }

        void patchJump(size_t instrIndex, int targetOffset) {
            this->code[instrIndex].operand = targetOffset;
        }
    };
}
