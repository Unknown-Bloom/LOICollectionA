#include <algorithm>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>
#include <functional>
#include <unordered_map>
#include <unordered_set>

#include "LOICollectionA/frontend/ir/Mir.h"
#include "LOICollectionA/frontend/Callback.h"

#include "LOICollectionA/frontend/ir/Compiler.h"

namespace LOICollection::frontend::ir {

    int Compiler::emitIterableLength(
        const IterableProtocol& protocol, int seqSlot, const SourceLocation& loc
    ) {
        if (protocol.shape == IterableShape::Convention) {
            const int base = this->reserveRegs(1);
            this->current.get().emit(MirOp::LOAD_SLOT, seqSlot, base, -1, -1, loc);

            const int dst = this->allocReg();

            if (ClassCall::getInstance().isRegistered(protocol.className)) {
                int metaIdx = this->addNativeCall(protocol.className, std::string(lengthMethod), 0);
                this->current.get().emitCall(
                    MirOp::CALL_NATIVE_METHOD, metaIdx, dst, base, -1, 0, loc);

                return dst;
            }

            int metaIdx = this->addByNameCall(std::string(lengthMethod), 0);
            this->current.get().emitCall(
                MirOp::CALL_METHOD_BY_NAME, metaIdx, dst, base, -1, 0, loc);

            return dst;
        }

        const int dst = this->allocReg();
        this->current.get().emit(MirOp::LOAD_LEN, 0, dst, seqSlot, -1, loc);

        return dst;
    }

    int Compiler::emitIterableElement(
        const IterableProtocol& protocol, int seqSlot, int idxSlot, const SourceLocation& loc
    ) {
        if (protocol.shape == IterableShape::Convention) {
            const int base = this->reserveRegs(2);
            this->current.get().emit(MirOp::LOAD_SLOT, idxSlot, base, -1, -1, loc);
            this->current.get().emit(MirOp::LOAD_SLOT, seqSlot, base + 1, -1, -1, loc);

            const int dst = this->allocReg();

            if (ClassCall::getInstance().isRegistered(protocol.className)) {
                int metaIdx = this->addNativeCall(protocol.className, std::string(elementMethod), 1);
                this->current.get().emitCall(
                    MirOp::CALL_NATIVE_METHOD, metaIdx, dst, base, -1, 1, loc);

                return dst;
            }

            int metaIdx = this->addByNameCall(std::string(elementMethod), 1);
            this->current.get().emitCall(
                MirOp::CALL_METHOD_BY_NAME, metaIdx, dst, base, -1, 1, loc);

            return dst;
        }

        const int dst = this->allocReg();
        this->current.get().emit(MirOp::LOAD_INDEX, 0, dst, seqSlot, idxSlot, loc);

        return dst;
    }

    void Compiler::compileForInIterable(ForInNode& node, size_t uid, const IterableProtocol& protocol) {
        const int seqSlot = this->declareSlot("__forin_seq_" + std::to_string(uid));
        const int idxSlot = this->declareSlot("__forin_idx_" + std::to_string(uid));
        const int elemSlot = this->declareSlot(node.elementVar);
        const int indexSlot = node.hasIndexVar ? this->declareSlot(node.indexVar) : -1;

        const int seqReg = this->compileValue(*node.iterable, node.loc);
        this->current.get().emit(MirOp::STORE_SLOT, seqSlot, -1, seqReg, -1, node.loc);

        this->current.get().emit(
            MirOp::LOAD_CONST, this->addConstant(0), idxSlot, -1, -1, node.loc);

        size_t loopStart = this->current.get().currentIP();

        const int idxReg = this->allocReg();
        this->current.get().emit(MirOp::LOAD_SLOT, idxSlot, idxReg, -1, -1, node.loc);

        const int lenReg = this->emitIterableLength(protocol, seqSlot, node.loc);
        const int cmpReg = this->allocReg();
        this->current.get().emit(
            MirOp::CMP_LT_I, 0, cmpReg, idxReg, lenReg, node.loc, TypeInfo{ TypeKind::Int });

        size_t jmpFalseIdx = this->current.get().emit(
            MirOp::JMP_IF_FALSE, 0, -1, cmpReg, -1, node.loc);

        this->loopStack.push_back(LoopContext{});
        this->loopStack.back().continueTarget = loopStart;

        const int elemReg = this->emitIterableElement(protocol, seqSlot, idxSlot, node.loc);
        this->current.get().emit(MirOp::STORE_SLOT, elemSlot, -1, elemReg, -1, node.loc);

        if (indexSlot >= 0)
            this->current.get().emit(MirOp::LOAD_SLOT, idxSlot, indexSlot, -1, -1, node.loc);

        const int oneReg = this->allocReg();
        this->current.get().emit(
            MirOp::LOAD_CONST, this->addConstant(1), oneReg, -1, -1, node.loc);

        const int nextReg = this->allocReg();
        this->current.get().emit(
            MirOp::ADD_I, 0, nextReg, idxSlot, oneReg, node.loc, TypeInfo{ TypeKind::Int });
        this->current.get().emit(MirOp::STORE_SLOT, idxSlot, -1, nextReg, -1, node.loc);

        node.body->accept(*this);

        size_t jmpBackIdx = this->current.get().emit(MirOp::JMP, 0, node.loc);
        this->current.get().patchJump(jmpBackIdx, static_cast<int>(loopStart) - static_cast<int>(jmpBackIdx) - 1);

        size_t exitPos = this->current.get().currentIP();
        this->current.get().patchJump(jmpFalseIdx, static_cast<int>(exitPos) - static_cast<int>(jmpFalseIdx) - 1);

        for (size_t idx : this->loopStack.back().breakJumps)
            this->current.get().patchJump(idx, static_cast<int>(exitPos) - static_cast<int>(idx) - 1);
        for (size_t idx : this->loopStack.back().continueJumps)
            this->current.get().patchJump(idx, static_cast<int>(loopStart) - static_cast<int>(idx) - 1);

        this->loopStack.pop_back();

        this->lastResultReg = this->finishHint(-1, -1, node.loc);
    }

    void Compiler::compileForInCounter(ForInNode& node, size_t uid) {
        auto& range = static_cast<RangeNode&>(*node.iterable);

        const int idxSlot = this->declareSlot("__forin_idx_" + std::to_string(uid));
        const int endSlot = this->declareSlot("__forin_end_" + std::to_string(uid));
        const int dirSlot = this->declareSlot("__forin_dir_" + std::to_string(uid));
        const int elemSlot = this->declareSlot(node.elementVar);
        const int indexSlot = node.hasIndexVar ? this->declareSlot(node.indexVar) : -1;

        const int startReg = this->compileValue(*range.start, node.loc);
        this->current.get().emit(MirOp::STORE_SLOT, idxSlot, -1, startReg, -1, node.loc);

        const int endReg = this->compileValue(*range.end, node.loc);
        this->current.get().emit(MirOp::STORE_SLOT, endSlot, -1, endReg, -1, node.loc);

        const int dirReg = this->allocReg();

        const int startCmp = this->allocReg();
        this->current.get().emit(MirOp::LOAD_SLOT, idxSlot, startCmp, -1, -1, node.loc);

        const int endCmp = this->allocReg();
        this->current.get().emit(MirOp::LOAD_SLOT, endSlot, endCmp, -1, -1, node.loc);

        const int ascending = this->allocReg();
        this->current.get().emit(MirOp::CMP_LE, 0, ascending, startCmp, endCmp, node.loc);

        size_t jmpDescIdx = this->current.get().emit(
            MirOp::JMP_IF_FALSE, 0, -1, ascending, -1, node.loc);

        this->current.get().emit(
            MirOp::LOAD_CONST, this->addConstant(1), dirReg, -1, -1, node.loc);

        size_t jmpDirEndIdx = this->current.get().emit(MirOp::JMP, 0, node.loc);

        int descStart = static_cast<int>(this->current.get().currentIP());
        this->current.get().patchJump(jmpDescIdx, descStart - static_cast<int>(jmpDescIdx) - 1);

        this->current.get().emit(
            MirOp::LOAD_CONST, this->addConstant(-1), dirReg, -1, -1, node.loc);

        int dirEndPos = static_cast<int>(this->current.get().currentIP());
        this->current.get().patchJump(jmpDirEndIdx, dirEndPos - static_cast<int>(jmpDirEndIdx) - 1);

        this->current.get().emit(MirOp::STORE_SLOT, dirSlot, -1, dirReg, -1, node.loc);

        size_t loopStart = this->current.get().currentIP();

        const int curReg = this->allocReg();
        this->current.get().emit(MirOp::LOAD_SLOT, idxSlot, curReg, -1, -1, node.loc);

        const int limitReg = this->allocReg();
        this->current.get().emit(MirOp::LOAD_SLOT, endSlot, limitReg, -1, -1, node.loc);

        const int deltaReg = this->allocReg();
        this->current.get().emit(MirOp::SUB, 0, deltaReg, curReg, limitReg, node.loc);

        const int stepReg = this->allocReg();
        this->current.get().emit(MirOp::LOAD_SLOT, dirSlot, stepReg, -1, -1, node.loc);

        const int scaledReg = this->allocReg();
        this->current.get().emit(MirOp::MUL, 0, scaledReg, deltaReg, stepReg, node.loc);

        const int zeroReg = this->allocReg();
        this->current.get().emit(
            MirOp::LOAD_CONST, this->addConstant(0), zeroReg, -1, -1, node.loc);

        const int condReg = this->allocReg();
        this->current.get().emit(MirOp::CMP_LT, 0, condReg, scaledReg, zeroReg, node.loc);

        size_t jmpFalseIdx = this->current.get().emit(
            MirOp::JMP_IF_FALSE, 0, -1, condReg, -1, node.loc);

        this->loopStack.push_back(LoopContext{});
        this->loopStack.back().continueTarget = loopStart;

        this->current.get().emit(MirOp::LOAD_SLOT, idxSlot, elemSlot, -1, -1, node.loc);

        if (indexSlot >= 0)
            this->current.get().emit(MirOp::LOAD_SLOT, idxSlot, indexSlot, -1, -1, node.loc);

        const int idxVal = this->allocReg();
        this->current.get().emit(MirOp::LOAD_SLOT, idxSlot, idxVal, -1, -1, node.loc);

        const int stepVal = this->allocReg();
        this->current.get().emit(MirOp::LOAD_SLOT, dirSlot, stepVal, -1, -1, node.loc);

        const int nextReg = this->allocReg();
        this->current.get().emit(MirOp::ADD, 0, nextReg, idxVal, stepVal, node.loc);
        this->current.get().emit(MirOp::STORE_SLOT, idxSlot, -1, nextReg, -1, node.loc);

        node.body->accept(*this);

        size_t jmpBackIdx = this->current.get().emit(MirOp::JMP, 0, node.loc);
        this->current.get().patchJump(jmpBackIdx, static_cast<int>(loopStart) - static_cast<int>(jmpBackIdx) - 1);

        size_t exitPos = this->current.get().currentIP();
        this->current.get().patchJump(jmpFalseIdx, static_cast<int>(exitPos) - static_cast<int>(jmpFalseIdx) - 1);

        for (size_t idx : this->loopStack.back().breakJumps)
            this->current.get().patchJump(idx, static_cast<int>(exitPos) - static_cast<int>(idx) - 1);
        for (size_t idx : this->loopStack.back().continueJumps)
            this->current.get().patchJump(idx, static_cast<int>(loopStart) - static_cast<int>(idx) - 1);

        this->loopStack.pop_back();

        this->lastResultReg = this->finishHint(-1, -1, node.loc);
    }

}
