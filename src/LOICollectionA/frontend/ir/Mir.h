#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "LOICollectionA/frontend/AST.h"

namespace LOICollection::frontend::ir {
    struct FuncMeta {
        std::string name;

        int argCount = 0;
    };

    struct MacroMeta {
        std::string name;

        int argCount = 0;
    };

    struct ClassMeta {
        std::string name;
        int baseClassIndex = -1;
        std::vector<std::string> fieldNames;
        std::vector<ValueNode::ValueType> defaults;
        std::vector<bool> hasDefault;

        std::vector<std::string> staticFieldNames;
        std::vector<ValueNode::ValueType> staticDefaults;
        std::vector<bool> staticHasDefault;

        int constructorIndex = -1;
        std::vector<int> methods;
        std::vector<std::string> methodSignatures;
        std::vector<int> staticMethods;
        std::vector<std::string> staticMethodSignatures;

        std::vector<int> ancestorIndices;
    };

    struct VirtualCallMeta {
        int classIndex = -1;
        int ordinal = -1;
        int argCount = 0;
    };

    struct SuperCallMeta {
        int constructorIndex = -1;
        int argCount = 0;
    };

    struct ByNameCallMeta {
        std::string methodName;
        int argCount = 0;
    };

    struct MethodMeta {
        std::string name;
        int argCount = 0;
        int classIndex = -1;
        int bodyIndex = -1;
    };

    struct LambdaMeta {
        int bodyIndex = -1;
        int argCount = 0;
        int captureCount = 0;
    };

    struct NativeCallMeta {
        std::string className;
        std::string name;
        int argCount = 0;
        bool isStatic = false;
    };

    enum class MirOp : uint8_t {
        LOAD_CONST,
        LOAD_VAR,
        LOAD_SLOT,
        STORE_VAR,
        STORE_SLOT,
        MOVE,

        ADD, SUB, MUL, DIV, MOD, POW,
        ADD_I, SUB_I, MUL_I, MOD_I,

        CMP_EQ, CMP_NE, CMP_GT, CMP_LT, CMP_GE, CMP_LE,
        CMP_EQ_I, CMP_NE_I, CMP_GT_I, CMP_LT_I, CMP_GE_I, CMP_LE_I,

        LOGIC_AND, LOGIC_OR,

        NEG, NEG_I, NOT,
        UNWRAP, TYPE_OF, HAS_VALUE, IS_NONE,

        LOAD_FIELD, LOAD_FIELD_SLOT,
        STORE_FIELD, STORE_FIELD_SLOT,

        MAKE_ARRAY,
        LOAD_INDEX,
        STORE_INDEX,

        LOAD_THIS,
        MAKE_LAMBDA,

        INSTANCEOF,

        NEW,
        NEW_NATIVE,

        CALL, CALL_MACRO,
        CALL_METHOD, CALL_METHOD_VIRTUAL, CALL_METHOD_BY_NAME,
        CALL_FUNC, CALL_NATIVE_METHOD, CALL_LAMBDA, CALL_SUPER_CTOR,

        RETURN,

        JMP_IF_FALSE,
        JMP_IF_TRUE,
        JMP,
        HALT,

        BIND_THIS,
        LOAD_LEN,

        COUNT
    };

    struct MirInstr {
        MirOp op{};
        int operand = 0;
        int dst = -1;
        int src1 = -1;
        int src2 = -1;
        int src3 = -1;
        int imm = 0;
        TypeInfo type{};
        SourceLocation loc{};
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

        size_t emit(MirOp op, int operand = 0, const SourceLocation& loc = {}) {
            this->code.push_back({ op, operand, -1, -1, -1, -1, 0, {}, loc });
            return this->code.size() - 1;
        }

        size_t emit(MirOp op, int operand, int dst, int src1, int src2,
                    const SourceLocation& loc = {}, const TypeInfo& type = {}) {
            this->code.push_back({ op, operand, dst, src1, src2, -1, 0, type, loc });
            return this->code.size() - 1;
        }

        size_t emit(MirOp op, int operand, int dst, int src1, int src2, int src3,
                    const SourceLocation& loc = {}, const TypeInfo& type = {}) {
            this->code.push_back({ op, operand, dst, src1, src2, src3, 0, type, loc });
            return this->code.size() - 1;
        }

        size_t emitLoad(MirOp op, int operand, int dst, const SourceLocation& loc = {}, const TypeInfo& type = {}) {
            this->code.push_back({ op, operand, dst, -1, -1, -1, 0, type, loc });
            return this->code.size() - 1;
        }

        size_t emitCall(MirOp op, int operand, int dst, int src1, int src2, int imm,
                        const SourceLocation& loc = {}, const TypeInfo& type = {}) {
            this->code.push_back({ op, operand, dst, src1, src2, -1, imm, type, loc });
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
