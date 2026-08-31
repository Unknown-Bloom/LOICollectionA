#include <algorithm>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>
#include <functional>
#include <unordered_map>
#include <unordered_set>

#include "LOICollectionA/frontend/ir/ByteCode.h"
#include "LOICollectionA/frontend/Callback.h"

#include "LOICollectionA/frontend/ir/Compiler.h"

namespace LOICollection::frontend::ir {

    void Compiler::emitIterableLength(
        const IterableProtocol& protocol, int seqSlot, const SourceLocation& loc
    ) {
        this->current.get().emit(OpCode::LOAD_SLOT, seqSlot, loc);

        if (protocol.shape == IterableShape::Convention) {
            if (ClassCall::getInstance().isRegistered(protocol.className)) {
                int metaIdx = this->addNativeCall(protocol.className, std::string(lengthMethod), 0);
                this->current.get().emit(OpCode::CALL_NATIVE_METHOD, metaIdx, loc);

                return;
            }

            int metaIdx = this->addByNameCall(std::string(lengthMethod), 0);
            this->current.get().emit(OpCode::CALL_METHOD_BY_NAME, metaIdx, loc);

            return;
        }

        this->current.get().emit(OpCode::LOAD_LEN, 0, loc);
    }

    void Compiler::emitIterableElement(
        const IterableProtocol& protocol, int seqSlot, int idxSlot, const SourceLocation& loc
    ) {
        if (protocol.shape == IterableShape::Convention) {
            if (ClassCall::getInstance().isRegistered(protocol.className)) {
                this->current.get().emit(OpCode::LOAD_SLOT, idxSlot, loc);
                this->current.get().emit(OpCode::LOAD_SLOT, seqSlot, loc);

                int metaIdx = this->addNativeCall(protocol.className, std::string(elementMethod), 1);
                this->current.get().emit(OpCode::CALL_NATIVE_METHOD, metaIdx, loc);

                return;
            }

            this->current.get().emit(OpCode::LOAD_SLOT, seqSlot, loc);
            this->current.get().emit(OpCode::LOAD_SLOT, idxSlot, loc);

            int metaIdx = this->addByNameCall(std::string(elementMethod), 1);
            this->current.get().emit(OpCode::CALL_METHOD_BY_NAME, metaIdx, loc);

            return;
        }

        this->current.get().emit(OpCode::LOAD_SLOT, seqSlot, loc);
        this->current.get().emit(OpCode::LOAD_SLOT, idxSlot, loc);
        this->current.get().emit(OpCode::LOAD_INDEX, 0, loc);
    }

    void Compiler::compileForInIterable(ForInNode& node, size_t uid, const IterableProtocol& protocol) {
        const int seqSlot = this->declareSlot("__forin_seq_" + std::to_string(uid));
        const int idxSlot = this->declareSlot("__forin_idx_" + std::to_string(uid));
        const int elemSlot = this->declareSlot(node.elementVar);
        const int indexSlot = node.hasIndexVar ? this->declareSlot(node.indexVar) : -1;

        this->compileValue(*node.iterable, node.loc);
        this->current.get().emit(OpCode::STORE_SLOT, seqSlot, node.loc);

        int zeroIdx = this->addConstant(0);
        this->current.get().emit(OpCode::PUSH_INT, zeroIdx, node.loc);
        this->current.get().emit(OpCode::STORE_SLOT, idxSlot, node.loc);

        size_t loopStart = this->current.get().currentIP();

        this->current.get().emit(OpCode::LOAD_SLOT, idxSlot, node.loc);
        this->emitIterableLength(protocol, seqSlot, node.loc);
        this->current.get().emit(OpCode::CMP_LT, 0, node.loc);

        size_t jmpFalseIdx = this->current.get().emit(OpCode::JMP_IF_FALSE, 0, node.loc);

        this->loopStack.push_back(LoopContext{});
        this->loopStack.back().continueTarget = loopStart;

        this->emitIterableElement(protocol, seqSlot, idxSlot, node.loc);
        this->current.get().emit(OpCode::STORE_SLOT, elemSlot, node.loc);

        if (indexSlot >= 0) {
            this->current.get().emit(OpCode::LOAD_SLOT, idxSlot, node.loc);
            this->current.get().emit(OpCode::STORE_SLOT, indexSlot, node.loc);
        }

        int oneIdx = this->addConstant(1);
        this->current.get().emit(OpCode::LOAD_SLOT, idxSlot, node.loc);
        this->current.get().emit(OpCode::PUSH_INT, oneIdx, node.loc);
        this->current.get().emit(OpCode::ADD, 0, node.loc);
        this->current.get().emit(OpCode::STORE_SLOT, idxSlot, node.loc);

        node.body->accept(*this);
        this->current.get().emit(OpCode::POP, 0, node.loc);

        size_t jmpBackIdx = this->current.get().emit(OpCode::JMP, 0, node.loc);
        this->current.get().patchJump(jmpBackIdx, static_cast<int>(loopStart) - static_cast<int>(jmpBackIdx) - 1);

        size_t exitPos = this->current.get().currentIP();
        this->current.get().patchJump(jmpFalseIdx, static_cast<int>(exitPos) - static_cast<int>(jmpFalseIdx) - 1);

        for (size_t idx : this->loopStack.back().breakJumps)
            this->current.get().patchJump(idx, static_cast<int>(exitPos) - static_cast<int>(idx) - 1);
        for (size_t idx : this->loopStack.back().continueJumps)
            this->current.get().patchJump(idx, static_cast<int>(loopStart) - static_cast<int>(idx) - 1);

        this->loopStack.pop_back();

        int emptyIdx = this->addConstant(std::string(""));
        this->current.get().emit(OpCode::PUSH_STR, emptyIdx, node.loc);
    }

    void Compiler::compileForInCounter(ForInNode& node, size_t uid) {
        auto& range = static_cast<RangeNode&>(*node.iterable);

        const int idxSlot = this->declareSlot("__forin_idx_" + std::to_string(uid));
        const int endSlot = this->declareSlot("__forin_end_" + std::to_string(uid));
        const int dirSlot = this->declareSlot("__forin_dir_" + std::to_string(uid));
        const int elemSlot = this->declareSlot(node.elementVar);
        const int indexSlot = node.hasIndexVar ? this->declareSlot(node.indexVar) : -1;

        this->compileValue(*range.start, node.loc);
        this->current.get().emit(OpCode::STORE_SLOT, idxSlot, node.loc);

        this->compileValue(*range.end, node.loc);
        this->current.get().emit(OpCode::STORE_SLOT, endSlot, node.loc);

        this->current.get().emit(OpCode::LOAD_SLOT, idxSlot, node.loc);
        this->current.get().emit(OpCode::LOAD_SLOT, endSlot, node.loc);
        this->current.get().emit(OpCode::CMP_LE, 0, node.loc);

        size_t jmpDescIdx = this->current.get().emit(OpCode::JMP_IF_FALSE, 0, node.loc);

        int ascIdx = this->addConstant(1);
        this->current.get().emit(OpCode::PUSH_INT, ascIdx, node.loc);

        size_t jmpDirEndIdx = this->current.get().emit(OpCode::JMP, 0, node.loc);

        int descStart = static_cast<int>(this->current.get().currentIP());
        this->current.get().patchJump(jmpDescIdx, descStart - static_cast<int>(jmpDescIdx) - 1);

        int descIdx = this->addConstant(-1);
        this->current.get().emit(OpCode::PUSH_INT, descIdx, node.loc);

        int dirEndPos = static_cast<int>(this->current.get().currentIP());
        this->current.get().patchJump(jmpDirEndIdx, dirEndPos - static_cast<int>(jmpDirEndIdx) - 1);

        this->current.get().emit(OpCode::STORE_SLOT, dirSlot, node.loc);

        size_t loopStart = this->current.get().currentIP();

        this->current.get().emit(OpCode::LOAD_SLOT, idxSlot, node.loc);
        this->current.get().emit(OpCode::LOAD_SLOT, endSlot, node.loc);
        this->current.get().emit(OpCode::SUB, 0, node.loc);
        this->current.get().emit(OpCode::LOAD_SLOT, dirSlot, node.loc);
        this->current.get().emit(OpCode::MUL, 0, node.loc);
        int zeroIdx = this->addConstant(0);
        this->current.get().emit(OpCode::PUSH_INT, zeroIdx, node.loc);
        this->current.get().emit(OpCode::CMP_LT, 0, node.loc);

        size_t jmpFalseIdx = this->current.get().emit(OpCode::JMP_IF_FALSE, 0, node.loc);

        this->loopStack.push_back(LoopContext{});
        this->loopStack.back().continueTarget = loopStart;

        this->current.get().emit(OpCode::LOAD_SLOT, idxSlot, node.loc);
        this->current.get().emit(OpCode::STORE_SLOT, elemSlot, node.loc);

        if (indexSlot >= 0) {
            this->current.get().emit(OpCode::LOAD_SLOT, idxSlot, node.loc);
            this->current.get().emit(OpCode::STORE_SLOT, indexSlot, node.loc);
        }

        this->current.get().emit(OpCode::LOAD_SLOT, idxSlot, node.loc);
        this->current.get().emit(OpCode::LOAD_SLOT, dirSlot, node.loc);
        this->current.get().emit(OpCode::ADD, 0, node.loc);
        this->current.get().emit(OpCode::STORE_SLOT, idxSlot, node.loc);

        node.body->accept(*this);
        this->current.get().emit(OpCode::POP, 0, node.loc);

        size_t jmpBackIdx = this->current.get().emit(OpCode::JMP, 0, node.loc);
        this->current.get().patchJump(jmpBackIdx, static_cast<int>(loopStart) - static_cast<int>(jmpBackIdx) - 1);

        size_t exitPos = this->current.get().currentIP();
        this->current.get().patchJump(jmpFalseIdx, static_cast<int>(exitPos) - static_cast<int>(jmpFalseIdx) - 1);

        for (size_t idx : this->loopStack.back().breakJumps)
            this->current.get().patchJump(idx, static_cast<int>(exitPos) - static_cast<int>(idx) - 1);
        for (size_t idx : this->loopStack.back().continueJumps)
            this->current.get().patchJump(idx, static_cast<int>(loopStart) - static_cast<int>(idx) - 1);

        this->loopStack.pop_back();

        int emptyIdx = this->addConstant(std::string(""));
        this->current.get().emit(OpCode::PUSH_STR, emptyIdx, node.loc);
    }

}
