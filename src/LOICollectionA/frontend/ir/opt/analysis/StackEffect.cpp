#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"

#include "LOICollectionA/frontend/ir/opt/analysis/StackEffect.h"

namespace LOICollection::frontend::ir::opt {
    bool stackEffectOf(const Instruction& instr, const BytecodeChunk& chunk, StackEffect& out) {
        out = {};

        switch (instr.op) {
            case OpCode::PUSH_INT:
            case OpCode::PUSH_FLOAT:
            case OpCode::PUSH_STR:
            case OpCode::PUSH_BOOL:
            case OpCode::PUSH_NONE:
            case OpCode::LOAD_SLOT:
            case OpCode::LOAD_VAR:
            case OpCode::LOAD_THIS:
            case OpCode::MAKE_LAMBDA:
                out.pushes = 1;
                return true;

            case OpCode::POP:
            case OpCode::STORE_SLOT:
            case OpCode::STORE_VAR:
                out.pops = 1;
                return true;

            case OpCode::DUP:
            case OpCode::DUP_IS_NONE:
                out.pushes = 1;
                out.peeks = 1;
                return true;

            case OpCode::DUP2:
                out.pushes = 2;
                out.peeks = 2;
                return true;

            case OpCode::DUP_STORE:
            case OpCode::DUP_STORE_SLOT:
                out.peeks = 1;
                return true;

            case OpCode::ROT3:
                out.pops = 3;
                out.pushes = 3;
                return true;

            case OpCode::SWAP2:
                out.pops = 4;
                out.pushes = 4;
                return true;

            case OpCode::UNWRAP:
            case OpCode::TYPE_OF:
            case OpCode::HAS_VALUE:
            case OpCode::IS_NONE:
            case OpCode::NEG:
            case OpCode::NEG_I:
            case OpCode::NOT:
                out.pops = 1;
                out.pushes = 1;
                return true;

            case OpCode::ADD:
            case OpCode::SUB:
            case OpCode::MUL:
            case OpCode::DIV:
            case OpCode::MOD:
            case OpCode::POW:
            case OpCode::CMP_EQ:
            case OpCode::CMP_NE:
            case OpCode::CMP_GT:
            case OpCode::CMP_LT:
            case OpCode::CMP_GE:
            case OpCode::CMP_LE:
            case OpCode::ADD_I:
            case OpCode::SUB_I:
            case OpCode::MUL_I:
            case OpCode::MOD_I:
            case OpCode::CMP_EQ_I:
            case OpCode::CMP_NE_I:
            case OpCode::CMP_GT_I:
            case OpCode::CMP_LT_I:
            case OpCode::CMP_GE_I:
            case OpCode::CMP_LE_I:
            case OpCode::LOGIC_AND:
            case OpCode::LOGIC_OR:
                out.pops = 2;
                out.pushes = 1;
                return true;

            case OpCode::JMP_IF_FALSE:
            case OpCode::JMP_IF_TRUE:
                out.pops = 1;
                return true;

            case OpCode::JMP:
            case OpCode::HALT:
                return true;

            default:
                return false;
        }
    }
}
