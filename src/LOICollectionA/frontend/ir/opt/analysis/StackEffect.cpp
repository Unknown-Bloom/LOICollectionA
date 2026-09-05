#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"

#include "LOICollectionA/frontend/ir/opt/analysis/StackEffect.h"

namespace LOICollection::frontend::ir::opt {
    bool stackEffectOf(const MirInstr& instr, const MirChunk& chunk, StackEffect& out) {
        out = {};

        switch (instr.op) {
            case MirOp::PUSH_INT:
            case MirOp::PUSH_FLOAT:
            case MirOp::PUSH_STR:
            case MirOp::PUSH_BOOL:
            case MirOp::PUSH_NONE:
            case MirOp::LOAD_SLOT:
            case MirOp::LOAD_VAR:
            case MirOp::LOAD_THIS:
            case MirOp::MAKE_LAMBDA:
                out.pushes = 1;
                return true;

            case MirOp::POP:
            case MirOp::STORE_SLOT:
            case MirOp::STORE_VAR:
                out.pops = 1;
                return true;

            case MirOp::DUP:
            case MirOp::DUP_IS_NONE:
                out.pushes = 1;
                out.peeks = 1;
                return true;

            case MirOp::DUP2:
                out.pushes = 2;
                out.peeks = 2;
                return true;

            case MirOp::DUP_STORE:
            case MirOp::DUP_STORE_SLOT:
                out.peeks = 1;
                return true;

            case MirOp::ROT3:
                out.pops = 3;
                out.pushes = 3;
                return true;

            case MirOp::SWAP2:
                out.pops = 4;
                out.pushes = 4;
                return true;

            case MirOp::UNWRAP:
            case MirOp::TYPE_OF:
            case MirOp::HAS_VALUE:
            case MirOp::IS_NONE:
            case MirOp::NEG:
            case MirOp::NOT:
                out.pops = 1;
                out.pushes = 1;
                return true;

            case MirOp::ADD:
            case MirOp::SUB:
            case MirOp::MUL:
            case MirOp::DIV:
            case MirOp::MOD:
            case MirOp::POW:
            case MirOp::CMP_EQ:
            case MirOp::CMP_NE:
            case MirOp::CMP_GT:
            case MirOp::CMP_LT:
            case MirOp::CMP_GE:
            case MirOp::CMP_LE:
            case MirOp::LOGIC_AND:
            case MirOp::LOGIC_OR:
                out.pops = 2;
                out.pushes = 1;
                return true;

            case MirOp::ADD_SS:
            case MirOp::SUB_SS:
            case MirOp::MUL_SS:
            case MirOp::MOD_SS:
            case MirOp::CMP_EQ_SS:
            case MirOp::CMP_NE_SS:
            case MirOp::CMP_GT_SS:
            case MirOp::CMP_LT_SS:
            case MirOp::CMP_GE_SS:
            case MirOp::CMP_LE_SS:
                out.pushes = 1;
                return true;

            case MirOp::JMP_IF_FALSE:
            case MirOp::JMP_IF_TRUE:
                out.pops = 1;
                return true;

            case MirOp::JMP:
            case MirOp::HALT:
                return true;

            default:
                return false;
        }
    }
}
