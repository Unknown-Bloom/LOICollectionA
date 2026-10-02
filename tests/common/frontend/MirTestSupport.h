#pragma once

#include "LOICollectionA/frontend/ir/Mir.h"

namespace LOICollection::tests::mir {
    namespace ir = LOICollection::frontend::ir;

    inline ir::MirChunk makeCountdownLoopChunk() {
        ir::MirChunk chunk;
        chunk.emit(ir::MirOp::LOAD_CONST, 0);
        chunk.emit(ir::MirOp::STORE_VAR, 0);
        chunk.emit(ir::MirOp::LOAD_VAR, 0);
        chunk.emit(ir::MirOp::LOAD_CONST, 1);
        chunk.emit(ir::MirOp::CMP_LT);
        chunk.emit(ir::MirOp::JMP_IF_FALSE, 2);
        chunk.emit(ir::MirOp::STORE_VAR, 0);
        chunk.emit(ir::MirOp::JMP, -6);
        chunk.emit(ir::MirOp::RETURN);
        return chunk;
    }

    inline ir::MirChunk makeNestedLoopChunk() {
        ir::MirChunk chunk;
        chunk.emit(ir::MirOp::LOAD_CONST, 0);
        chunk.emit(ir::MirOp::STORE_VAR, 0);
        chunk.emit(ir::MirOp::LOAD_VAR, 0);
        chunk.emit(ir::MirOp::LOAD_CONST, 1);
        chunk.emit(ir::MirOp::CMP_LT);
        chunk.emit(ir::MirOp::JMP_IF_FALSE, 9);
        chunk.emit(ir::MirOp::LOAD_CONST, 2);
        chunk.emit(ir::MirOp::STORE_VAR, 1);
        chunk.emit(ir::MirOp::LOAD_VAR, 1);
        chunk.emit(ir::MirOp::LOAD_CONST, 3);
        chunk.emit(ir::MirOp::CMP_LT);
        chunk.emit(ir::MirOp::JMP_IF_FALSE, 2);
        chunk.emit(ir::MirOp::STORE_VAR, 1);
        chunk.emit(ir::MirOp::JMP, -6);
        chunk.emit(ir::MirOp::JMP, -13);
        chunk.emit(ir::MirOp::RETURN);
        return chunk;
    }

    inline ir::MirChunk makeStraightLineChunk() {
        ir::MirChunk chunk;
        chunk.emit(ir::MirOp::LOAD_CONST, 0);
        chunk.emit(ir::MirOp::STORE_VAR, 0);
        chunk.emit(ir::MirOp::RETURN);
        return chunk;
    }

    inline ir::MirChunk makeConditionalFallthroughChunk() {
        ir::MirChunk chunk;
        chunk.emit(ir::MirOp::JMP_IF_FALSE, 1);
        chunk.emit(ir::MirOp::LOAD_CONST, 0);
        chunk.emit(ir::MirOp::LOAD_CONST, 1);
        return chunk;
    }

    inline ir::MirChunk makeUnconditionalJumpChunk() {
        ir::MirChunk chunk;
        chunk.emit(ir::MirOp::LOAD_CONST, 0);
        chunk.emit(ir::MirOp::JMP, 1);
        chunk.emit(ir::MirOp::LOAD_CONST, 1);
        chunk.emit(ir::MirOp::RETURN);
        return chunk;
    }

    inline ir::MirChunk makeSelfJumpChunk() {
        ir::MirChunk chunk;
        chunk.emit(ir::MirOp::LOAD_CONST, 0);
        chunk.emit(ir::MirOp::JMP, -1);
        return chunk;
    }
}
