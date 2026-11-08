#pragma once

#include "LOICollectionA/frontend/ir/Lir.h"

namespace LOICollection::tests::lir {
    namespace ir = LOICollection::frontend::ir;

    inline ir::Chunk makeCountdownLoopChunk() {
        ir::Chunk chunk;
        chunk.emit(ir::Op::LOAD_CONST, 0);
        chunk.emit(ir::Op::STORE_VAR, 0);
        chunk.emit(ir::Op::LOAD_VAR, 0);
        chunk.emit(ir::Op::LOAD_CONST, 1);
        chunk.emit(ir::Op::CMP_LT);
        chunk.emit(ir::Op::JMP_IF_FALSE, 2);
        chunk.emit(ir::Op::STORE_VAR, 0);
        chunk.emit(ir::Op::JMP, -6);
        chunk.emit(ir::Op::RETURN);
        return chunk;
    }

    inline ir::Chunk makeNestedLoopChunk() {
        ir::Chunk chunk;
        chunk.emit(ir::Op::LOAD_CONST, 0);
        chunk.emit(ir::Op::STORE_VAR, 0);
        chunk.emit(ir::Op::LOAD_VAR, 0);
        chunk.emit(ir::Op::LOAD_CONST, 1);
        chunk.emit(ir::Op::CMP_LT);
        chunk.emit(ir::Op::JMP_IF_FALSE, 9);
        chunk.emit(ir::Op::LOAD_CONST, 2);
        chunk.emit(ir::Op::STORE_VAR, 1);
        chunk.emit(ir::Op::LOAD_VAR, 1);
        chunk.emit(ir::Op::LOAD_CONST, 3);
        chunk.emit(ir::Op::CMP_LT);
        chunk.emit(ir::Op::JMP_IF_FALSE, 2);
        chunk.emit(ir::Op::STORE_VAR, 1);
        chunk.emit(ir::Op::JMP, -6);
        chunk.emit(ir::Op::JMP, -13);
        chunk.emit(ir::Op::RETURN);
        return chunk;
    }

    inline ir::Chunk makeStraightLineChunk() {
        ir::Chunk chunk;
        chunk.emit(ir::Op::LOAD_CONST, 0);
        chunk.emit(ir::Op::STORE_VAR, 0);
        chunk.emit(ir::Op::RETURN);
        return chunk;
    }

    inline ir::Chunk makeConditionalFallthroughChunk() {
        ir::Chunk chunk;
        chunk.emit(ir::Op::JMP_IF_FALSE, 1);
        chunk.emit(ir::Op::LOAD_CONST, 0);
        chunk.emit(ir::Op::LOAD_CONST, 1);
        return chunk;
    }

    inline ir::Chunk makeUnconditionalJumpChunk() {
        ir::Chunk chunk;
        chunk.emit(ir::Op::LOAD_CONST, 0);
        chunk.emit(ir::Op::JMP, 1);
        chunk.emit(ir::Op::LOAD_CONST, 1);
        chunk.emit(ir::Op::RETURN);
        return chunk;
    }

    inline ir::Chunk makeSelfJumpChunk() {
        ir::Chunk chunk;
        chunk.emit(ir::Op::LOAD_CONST, 0);
        chunk.emit(ir::Op::JMP, -1);
        return chunk;
    }
}
