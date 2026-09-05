#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "LOICollectionA/frontend/AST.h"

namespace LOICollection::frontend::ir {
    // ------------------------------------------------------------------
    // Metadata tables. These used to live in ByteCode.h; the bytecode layer
    // was removed when the VM started executing the three-address MIR
    // directly, so the metadata now lives next to MirChunk.
    // ------------------------------------------------------------------
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

    // ------------------------------------------------------------------
    // Three-address MIR.
    //
    // Every value lives in a virtual register (a slot in the frame's
    // register file, aliased by `localPool` in the VM). There is no operand
    // stack: instructions read theirinputs from registers and write their
    // result to a destination register.
    //
    // Operand conventions:
    //   operand - primary immediate: jump target, metadata table index,
    //             slot index, or constant-table index.
    //   dst     - destination register (-1 when the instruction produces none).
    //   src1/2/3- source registers (-1 when unused).
    //   imm     - secondary immediate: argument count, capture count, etc.
    // ------------------------------------------------------------------
    enum class MirOp : uint8_t {
        LOAD_CONST,          // dst = constants[operand]
        LOAD_VAR,            // dst = var(name at constants[operand])
        LOAD_SLOT,           // dst = regFile[operand] (copy local into a temp register)
        STORE_VAR,           // var(name at constants[operand]) = src1
        STORE_SLOT,          // regFile[operand] = src1
        MOVE,               // dst = src1

        ADD, SUB, MUL, DIV, MOD, POW,
        ADD_I, SUB_I, MUL_I, MOD_I,

        CMP_EQ, CMP_NE, CMP_GT, CMP_LT, CMP_GE, CMP_LE,
        CMP_EQ_I, CMP_NE_I, CMP_GT_I, CMP_LT_I, CMP_GE_I, CMP_LE_I,

        LOGIC_AND, LOGIC_OR,

        NEG, NEG_I, NOT,
        UNWRAP, TYPE_OF, HAS_VALUE, IS_NONE,

        LOAD_FIELD, LOAD_FIELD_SLOT,
        STORE_FIELD, STORE_FIELD_SLOT,

        MAKE_ARRAY,          // dst = array[regFile[src1 .. src1+operand)]
        LOAD_INDEX,          // dst = regFile[src1][regFile[src2]]
        STORE_INDEX,         // regFile[src1][regFile[src2]] = regFile[src3]

        LOAD_THIS,
        MAKE_LAMBDA,         // dst = lambda(meta=operand, captures regFile[src1 .. +imm])

        INSTANCEOF,          // dst = regFile[src1] isa constants[operand]

        NEW,                 // dst = new class[operand](regFile[src1 .. +imm])
        NEW_NATIVE,          // dst = new native[operand](regFile[src1 .. +imm])

        // Calls. Arguments are laid out in a consecutive register window
        // starting at `src1`; `imm` is the argument count (receiver excluded).
        // Opcodes whose receiver is an object read it at `src1 + imm`.
        // CALL_LAMBDA additionally reads the callee value from `src2`.
        CALL, CALL_MACRO,
        CALL_METHOD, CALL_METHOD_VIRTUAL, CALL_METHOD_BY_NAME,
        CALL_FUNC, CALL_NATIVE_METHOD, CALL_LAMBDA, CALL_SUPER_CTOR,

        RETURN,              // return regFile[src1] (empty string when src1 < 0)

        JMP_IF_FALSE,        // operand = relative offset, src1 = condition
        JMP_IF_TRUE,
        JMP,
        HALT,                // returns regFile[0]

        BIND_THIS,           // bind the object in src2 as `this` of the lambda in src1; dst = src1
        LOAD_LEN,            // dst = len(regFile[src1])

        COUNT
    };

    struct MirInstr {
        MirOp op{};
        int operand = 0;     // primary immediate (see conventions above)
        int dst = -1;        // destination register
        int src1 = -1;       // source register 1
        int src2 = -1;       // source register 2
        int src3 = -1;       // source register 3 (STORE_INDEX)
        int imm = 0;         // secondary immediate (arg count / capture count)
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
