#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "LOICollectionA/frontend/AST.h"

namespace LOICollection::frontend::ir::lir {
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
        std::uint64_t ancestorMask = 0;
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

    struct LambdaCapture {
        std::string name;
        int sourceSlot = -1;
        int slot = -1;
    };

    struct LambdaMeta {
        int bodyIndex = -1;
        int argCount = 0;
        int captureCount = 0;
        std::vector<LambdaCapture> captures;
    };

    struct EnumMeta {
        std::string name;
        std::vector<std::string> variantNames;
        std::vector<std::uint8_t> variantArity;
    };

    struct NativeCallMeta {
        std::string className;
        std::string name;
        int argCount = 0;
        bool isStatic = false;
    };

    enum class Op : std::uint8_t {
        LOAD_CONST,
        LOAD_VAR,
        LOAD_SLOT,
        STORE_VAR,
        STORE_SLOT,
        MOVE,

        ADD, SUB, MUL, DIV, MOD, POW,

        CMP_EQ, CMP_NE, CMP_GT, CMP_LT, CMP_GE, CMP_LE,

        LOGIC_AND, LOGIC_OR,

        NEG, NOT,
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
        PANIC,

        BIND_THIS,
        LOAD_LEN,

        JMP_IF_CMP_FALSE,

        MAKE_ENUM,
        TAG_OF,
        LOAD_ENUM_PAYLOAD,

        JMP_IF_TAG_NE,

        COUNT
    };

    enum class Flag : int {
        None = 0,
        ScalarStore = 1 << 0,
    };

    struct Instr {
        Op op{};
        int operand = 0;
        int dst = -1;
        int src1 = -1;
        int src2 = -1;
        int src3 = -1;
        int imm = 0;
        TypeInfo type{};
        SourceLocation loc{};
    };

    struct Chunk {
        std::vector<Instr> code;
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
        std::vector<EnumMeta> enums;

        std::deque<std::unique_ptr<Chunk>> methodBodies;

        int slotCount = 0;

        bool hasRefStores = true;

        Chunk() = default;
        Chunk(Chunk&&) noexcept = default;
        Chunk& operator=(Chunk&&) noexcept = default;
        Chunk(const Chunk&) = delete;
        Chunk& operator=(const Chunk&) = delete;

        ~Chunk();

        std::size_t emit(Op op, int operand = 0, const SourceLocation& loc = {}) {
            this->code.push_back({ op, operand, -1, -1, -1, -1, 0, {}, loc });
            return this->code.size() - 1;
        }

        std::size_t emit(Op op, int operand, int dst, int src1, int src2,
                         const SourceLocation& loc = {}, const TypeInfo& type = {}) {
            this->code.push_back({ op, operand, dst, src1, src2, -1, 0, type, loc });
            return this->code.size() - 1;
        }

        std::size_t emit(Op op, int operand, int dst, int src1, int src2, int src3,
                         const SourceLocation& loc = {}, const TypeInfo& type = {}) {
            this->code.push_back({ op, operand, dst, src1, src2, src3, 0, type, loc });
            return this->code.size() - 1;
        }

        std::size_t emitLoad(Op op, int operand, int dst, const SourceLocation& loc = {}, const TypeInfo& type = {}) {
            this->code.push_back({ op, operand, dst, -1, -1, -1, 0, type, loc });
            return this->code.size() - 1;
        }

        std::size_t emitCall(Op op, int operand, int dst, int src1, int src2, int imm,
                             const SourceLocation& loc = {}, const TypeInfo& type = {}) {
            this->code.push_back({ op, operand, dst, src1, src2, -1, imm, type, loc });
            return this->code.size() - 1;
        }

        [[nodiscard]] std::size_t currentIP() const {
            return this->code.size();
        }

        void patchJump(std::size_t instrIndex, int targetOffset) {
            this->code[instrIndex].operand = targetOffset;
        }
    };
}

namespace LOICollection::frontend::ir {
    using lir::ByNameCallMeta;
    using lir::Chunk;
    using lir::ClassMeta;
    using lir::EnumMeta;
    using lir::Flag;
    using lir::FuncMeta;
    using lir::Instr;
    using lir::LambdaCapture;
    using lir::LambdaMeta;
    using lir::MacroMeta;
    using lir::MethodMeta;
    using lir::NativeCallMeta;
    using lir::Op;
    using lir::SuperCallMeta;
    using lir::VirtualCallMeta;
}
