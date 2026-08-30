#pragma once

#include <deque>
#include <memory>
#include <vector>
#include <string>

#include "LOICollectionA/frontend/AST.h"
#include "LOICollectionA/frontend/ir/OpCode.h"

namespace LOICollection::frontend::ir {
    struct Instruction {
        OpCode op;

        int operand;
        SourceLocation loc;
    };

    struct FuncMeta {
        std::string name;

        int argCount;
    };

    struct MacroMeta {
        std::string name;

        int argCount;
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

    struct BytecodeChunk {
        std::vector<Instruction> code;
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
        /* Deque of owners on purpose: the compiler keeps `current` references
         * into nested bodies while appending siblings, so references must stay
         * valid across push_back. The bodies are held by unique_ptr because
         * MSVC STL's std::deque does not support an incomplete value type
         * (a self-referencing member), while libstdc++ accepts it. */
        std::deque<std::unique_ptr<BytecodeChunk>> methodBodies;

        int slotCount = 0;

        size_t emit(OpCode op, int operand = 0, const SourceLocation& loc = {}) {
            this->code.push_back({op, operand, loc});
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
