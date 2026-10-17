#include <algorithm>
#include <string>
#include <variant>
#include <vector>

#include "LOICollectionA/frontend/ir/opt/analysis/JumpTargetAnalysis.h"
#include "LOICollectionA/frontend/ir/opt/analysis/RefStoreAnalysis.h"

namespace LOICollection::frontend::ir::opt::detail {
    bool isScalarConstant(const ValueNode::ValueType& value) {
        return !std::holds_alternative<ObjectRef>(value)
            && !std::holds_alternative<ArrayRef>(value)
            && !std::holds_alternative<FunctionRefPtr>(value);
    }
}

namespace LOICollection::frontend::ir::opt {
    using namespace detail;

    void RefStoreAnalysis::run() {
        const std::size_t slots = this->mChunk.slotCount < 0
            ? 0
            : static_cast<std::size_t>(this->mChunk.slotCount);

        std::vector<bool> scalar(slots, false);
        bool refStores = false;

        const JumpTargetAnalysis jumps{ this->mChunk.code };

        const auto reads = [&scalar](int reg) {
            return reg >= 0 && static_cast<std::size_t>(reg) < scalar.size()
                && scalar[static_cast<std::size_t>(reg)];
        };

        for (std::size_t ip = 0; ip < this->mChunk.code.size(); ++ip) {
            if (jumps.isTarget(static_cast<int>(ip)))
                std::fill(scalar.begin(), scalar.end(), false);

            MirInstr& instr = this->mChunk.code[ip];

            const auto writes = [&scalar](int reg, bool isScalar) {
                if (reg >= 0 && static_cast<std::size_t>(reg) < scalar.size())
                    scalar[static_cast<std::size_t>(reg)] = isScalar;
            };

            switch (instr.op) {
                case MirOp::LOAD_CONST:
                    writes(instr.dst, isScalarConstant(this->mChunk.constants[static_cast<std::size_t>(instr.operand)]));
                    break;
                case MirOp::MOVE:
                    writes(instr.dst, reads(instr.src1));
                    break;
                case MirOp::LOAD_SLOT:
                    writes(instr.dst, reads(instr.operand));
                    break;
                case MirOp::STORE_SLOT:
                    writes(instr.operand, reads(instr.src1));
                    break;
                case MirOp::LOAD_VAR:
                case MirOp::LOAD_FIELD:
                case MirOp::LOAD_FIELD_SLOT:
                case MirOp::LOAD_INDEX:
                case MirOp::LOAD_THIS:
                case MirOp::UNWRAP:
                    writes(instr.dst, false);
                    break;
                case MirOp::NEW:
                case MirOp::NEW_NATIVE:
                case MirOp::CALL:
                case MirOp::CALL_MACRO:
                case MirOp::CALL_METHOD:
                case MirOp::CALL_METHOD_VIRTUAL:
                case MirOp::CALL_METHOD_BY_NAME:
                case MirOp::CALL_FUNC:
                case MirOp::CALL_NATIVE_METHOD:
                case MirOp::CALL_LAMBDA:
                case MirOp::CALL_SUPER_CTOR:
                    writes(instr.dst, false);
                    break;
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
                case MirOp::NEG:
                case MirOp::NOT:
                case MirOp::TYPE_OF:
                case MirOp::HAS_VALUE:
                case MirOp::IS_NONE:
                case MirOp::LOAD_LEN:
                case MirOp::INSTANCEOF:
                    writes(instr.dst, true);
                    break;
                case MirOp::STORE_FIELD:
                case MirOp::STORE_FIELD_SLOT: {
                    if (reads(instr.src2))
                        instr.imm |= static_cast<int>(MirFlag::ScalarStore);
                    else
                        refStores = true;
                } break;
                case MirOp::STORE_INDEX: {
                    if (reads(instr.src3))
                        instr.imm |= static_cast<int>(MirFlag::ScalarStore);
                    else
                        refStores = true;
                } break;
                case MirOp::MAKE_ARRAY:
                case MirOp::MAKE_LAMBDA: {
                    if (instr.op == MirOp::MAKE_LAMBDA) {
                        refStores = true;
                        break;
                    }

                    for (int i = 0; i < instr.operand; ++i) {
                        if (reads(instr.src1 + i))
                            continue;

                        refStores = true;
                        break;
                    }
                } break;
                default:
                    break;
            }
        }

        this->mChunk.hasRefStores = refStores;
    }
}
