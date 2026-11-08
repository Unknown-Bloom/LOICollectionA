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

            Instr& instr = this->mChunk.code[ip];

            const auto writes = [&scalar](int reg, bool isScalar) {
                if (reg >= 0 && static_cast<std::size_t>(reg) < scalar.size())
                    scalar[static_cast<std::size_t>(reg)] = isScalar;
            };

            switch (instr.op) {
                case Op::LOAD_CONST: {
                    const std::size_t index = static_cast<std::size_t>(instr.operand);
                    writes(instr.dst, instr.operand >= 0
                        && index < this->mChunk.constants.size()
                        && isScalarConstant(this->mChunk.constants[index]));
                } break;
                case Op::MOVE:
                    writes(instr.dst, reads(instr.src1));
                    break;
                case Op::LOAD_SLOT:
                    writes(instr.dst, reads(instr.operand));
                    break;
                case Op::STORE_SLOT:
                    writes(instr.operand, reads(instr.src1));
                    break;
                case Op::LOAD_VAR:
                case Op::LOAD_FIELD:
                case Op::LOAD_FIELD_SLOT:
                case Op::LOAD_INDEX:
                case Op::LOAD_THIS:
                case Op::UNWRAP:
                    writes(instr.dst, false);
                    break;
                case Op::NEW:
                case Op::NEW_NATIVE:
                case Op::CALL:
                case Op::CALL_MACRO:
                case Op::CALL_METHOD:
                case Op::CALL_METHOD_VIRTUAL:
                case Op::CALL_METHOD_BY_NAME:
                case Op::CALL_FUNC:
                case Op::CALL_NATIVE_METHOD:
                case Op::CALL_LAMBDA:
                case Op::CALL_SUPER_CTOR:
                    writes(instr.dst, false);
                    break;
                case Op::ADD:
                case Op::SUB:
                case Op::MUL:
                case Op::DIV:
                case Op::MOD:
                case Op::POW:
                case Op::CMP_EQ:
                case Op::CMP_NE:
                case Op::CMP_GT:
                case Op::CMP_LT:
                case Op::CMP_GE:
                case Op::CMP_LE:
                case Op::LOGIC_AND:
                case Op::LOGIC_OR:
                case Op::NEG:
                case Op::NOT:
                case Op::TYPE_OF:
                case Op::HAS_VALUE:
                case Op::IS_NONE:
                case Op::LOAD_LEN:
                case Op::INSTANCEOF:
                    writes(instr.dst, true);
                    break;
                case Op::MAKE_ENUM:
                    
                    
                    reads(instr.src1);
                    reads(instr.src2);
                    reads(instr.src3);
                    writes(instr.dst, false);
                    break;
                case Op::TAG_OF:
                    writes(instr.dst, true);
                    break;
                case Op::LOAD_ENUM_PAYLOAD:
                    writes(instr.dst, false);
                    break;
                case Op::STORE_FIELD:
                case Op::STORE_FIELD_SLOT: {
                    if (reads(instr.src2))
                        instr.imm |= static_cast<int>(Flag::ScalarStore);
                    else
                        refStores = true;
                } break;
                case Op::STORE_INDEX: {
                    if (reads(instr.src3))
                        instr.imm |= static_cast<int>(Flag::ScalarStore);
                    else
                        refStores = true;
                } break;
                case Op::MAKE_LAMBDA:
                    writes(instr.dst, false);
                    refStores = true;
                    break;
                case Op::MAKE_ARRAY: {
                    writes(instr.dst, false);

                    bool allScalar = instr.operand >= 0 && instr.src1 >= 0;
                    const std::size_t first = static_cast<std::size_t>(instr.src1);
                    const std::size_t count = static_cast<std::size_t>(instr.operand);

                    if (allScalar) {
                        allScalar = first + count <= scalar.size() && first + count >= first;

                        for (std::size_t i = 0; allScalar && i < count; ++i)
                            allScalar = scalar[first + i];
                    }

                    if (!allScalar)
                        refStores = true;
                } break;
                default:
                    break;
            }
        }

        this->mChunk.hasRefStores = refStores;
    }
}
