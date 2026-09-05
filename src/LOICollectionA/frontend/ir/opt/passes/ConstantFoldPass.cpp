#include <algorithm>
#include <charconv>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/Unicode.h"

#include "LOICollectionA/frontend/ir/VM.h"

#include "LOICollectionA/frontend/ir/opt/analysis/FoldPredicates.h"
#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"

#include "LOICollectionA/frontend/ir/opt/passes/ConstantFoldPass.h"

namespace LOICollection::frontend::ir::opt {
    namespace {
        bool alwaysJumps(MirOp op, const ValueNode::ValueType& value) {
            const bool truthy = VM::valueToBool(value);
            return (op == MirOp::JMP_IF_FALSE) ? !truthy : truthy;
        }

        bool valuesEqual(const ValueNode::ValueType& left, const ValueNode::ValueType& right) {
            if (auto li = std::get_if<int>(&left)) {
                if (auto ri = std::get_if<int>(&right)) return *li == *ri;
                if (auto rf = std::get_if<float>(&right)) return *li == *rf;
                return false;
            }
            if (auto lf = std::get_if<float>(&left)) {
                if (auto ri = std::get_if<int>(&right)) return *lf == *ri;
                if (auto rf = std::get_if<float>(&right)) return *lf == *rf;
                return false;
            }
            if (auto ls = std::get_if<std::string>(&left)) {
                if (auto rs = std::get_if<std::string>(&right)) return *ls == *rs;
                return false;
            }
            if (auto lb = std::get_if<bool>(&left)) {
                if (auto rb = std::get_if<bool>(&right)) return *lb == *rb;
                return false;
            }
            if (auto lo = std::get_if<ObjectRef>(&left)) {
                if (auto ro = std::get_if<ObjectRef>(&right)) return *lo == *ro;
                return false;
            }
            if (auto la = std::get_if<ArrayRef>(&left)) {
                if (auto ra = std::get_if<ArrayRef>(&right)) return *la == *ra;
                return false;
            }
            return false;
        }

        bool isZero(const ValueNode::ValueType& v) {
            if (auto i = std::get_if<int>(&v)) return *i == 0;
            if (auto f = std::get_if<float>(&v)) return *f == 0.0f;
            return false;
        }

        bool isOne(const ValueNode::ValueType& v) {
            if (auto i = std::get_if<int>(&v)) return *i == 1;
            if (auto f = std::get_if<float>(&v)) return *f == 1.0f;
            return false;
        }

        bool identityEligible(MirOp op, const ValueNode::ValueType& identity) {
            switch (op) {
                case MirOp::ADD:
                case MirOp::SUB:
                    return isZero(identity);
                case MirOp::MUL:
                case MirOp::DIV:
                case MirOp::POW:
                    return isOne(identity);
                default:
                    return false;
            }
        }
    }

    void ConstantFoldPass::run(bool enabled) {
        for (size_t i = 0; i < mChunk.code.size(); ++i) {
            const int oldIdx = static_cast<int>(i);

            if (mJumps.isTarget(oldIdx)) {
                mRegValues.clear();
                mSlotValues.clear();
                mNameValues.clear();
            } else if (canWriteVariables(mChunk.code[i].op)) {
                mSlotValues.clear();
                mNameValues.clear();
            }

            const Step step = enabled ? fold(oldIdx) : emitOriginal(mChunk.code[i]);

            if (step.emittedAt >= 0) {
                mCtx.oldToNew[oldIdx] = step.emittedAt;
                mCtx.newToOld.push_back(oldIdx);
            }
        }
    }

    ConstantFoldPass::Step ConstantFoldPass::fold(int oldIdx) {
        const MirInstr& instr = mChunk.code[oldIdx];

        switch (instr.op) {
            case MirOp::LOAD_CONST: {
                const int at = mCtx.emit(instr);
                mRegValues[instr.dst] = { mChunk.constants[instr.operand], at, true };
                return { at };
            }

            case MirOp::MOVE: {
                const int at = mCtx.emit(instr);
                if (auto it = mRegValues.find(instr.src1); it != mRegValues.end())
                    mRegValues[instr.dst] = it->second;
                else
                    mRegValues.erase(instr.dst);
                return { at };
            }

            case MirOp::ADD: case MirOp::SUB: case MirOp::MUL: case MirOp::DIV:
            case MirOp::MOD: case MirOp::POW:
                return foldBinary(instr, oldIdx);

            case MirOp::CMP_EQ: case MirOp::CMP_NE: case MirOp::CMP_GT: case MirOp::CMP_LT:
            case MirOp::CMP_GE: case MirOp::CMP_LE:
            case MirOp::LOGIC_AND: case MirOp::LOGIC_OR:
                return foldComparison(instr, oldIdx);

            case MirOp::NEG: case MirOp::NOT:
                return foldUnary(instr, oldIdx);

            case MirOp::UNWRAP: case MirOp::TYPE_OF: case MirOp::HAS_VALUE: case MirOp::IS_NONE:
                return foldOptional(instr);

            case MirOp::LOAD_SLOT: return foldLoadSlot(instr);
            case MirOp::STORE_SLOT: return foldStoreSlot(instr);
            case MirOp::LOAD_VAR: return foldLoadVar(instr);
            case MirOp::STORE_VAR: return foldStoreVar(instr);

            case MirOp::MAKE_ARRAY: return foldMakeArray(instr, oldIdx);
            case MirOp::LOAD_INDEX: return foldLoadIndex(instr, oldIdx);
            case MirOp::STORE_INDEX: return foldStoreIndex(instr);

            case MirOp::CALL: return foldCall(instr, oldIdx);
            case MirOp::CALL_NATIVE_METHOD: return foldNativeMethod(instr, oldIdx);

            case MirOp::JMP_IF_FALSE:
            case MirOp::JMP_IF_TRUE:
                return foldBranch(instr, oldIdx);

            case MirOp::JMP:
            case MirOp::RETURN:
            case MirOp::HALT:
                return emitOriginal(instr);

            case MirOp::MAKE_LAMBDA:
            case MirOp::LOAD_THIS:
            case MirOp::LOAD_FIELD:
            case MirOp::LOAD_FIELD_SLOT:
            case MirOp::LOAD_LEN:
            case MirOp::INSTANCEOF:
                this->forgetDst(instr.dst);
                return emitOriginal(instr);

            case MirOp::STORE_FIELD:
            case MirOp::STORE_FIELD_SLOT:
            case MirOp::BIND_THIS:
                return emitOriginal(instr);

            case MirOp::NEW:
            case MirOp::NEW_NATIVE:
            case MirOp::CALL_MACRO:
            case MirOp::CALL_FUNC:
            case MirOp::CALL_LAMBDA:
            case MirOp::CALL_METHOD:
            case MirOp::CALL_METHOD_VIRTUAL:
            case MirOp::CALL_METHOD_BY_NAME:
            case MirOp::CALL_SUPER_CTOR:
                this->forgetDst(instr.dst);
                return emitOriginal(instr);

            default:
                return emitOriginal(instr);
        }
    }

    ConstantFoldPass::Step ConstantFoldPass::foldBinary(const MirInstr& instr, int oldIdx) {
        Known left, right;
        const bool lk = knownReg(instr.src1, left);
        const bool rk = knownReg(instr.src2, right);

        if (lk && rk) {
            DiagnosticEngine diag;
            const ValueNode::ValueType result = VM::applyArithmetic(
                left.value, right.value, instr.op, diag, instr.loc);

            if (!diag.hasErrors()) {
                ++mCtx.stats.folded;
                return emitConst(result, instr.loc);
            }
        }

        if (lk && identityEligible(instr.op, right.value)) {
            ++mCtx.stats.folded;
            return emitMove(instr.dst, instr.src1, instr.loc);
        }

        if (rk && identityEligible(instr.op, left.value)) {
            ++mCtx.stats.folded;
            return emitMove(instr.dst, instr.src2, instr.loc);
        }

        this->forgetDst(instr.dst);
        return emitOriginal(instr);
    }

    ConstantFoldPass::Step ConstantFoldPass::foldUnary(const MirInstr& instr, int oldIdx) {
        Known operand;
        if (!knownReg(instr.src1, operand))
            return emitOriginalAfterForget(instr);

        if (instr.op == MirOp::NOT) {
            ++mCtx.stats.folded;
            return emitConst(!VM::valueToBool(operand.value), instr.loc);
        }

        DiagnosticEngine diag;
        ValueNode::ValueType result = VM::applyUnary(operand.value, MirOp::NEG, diag, instr.loc);

        if (diag.hasErrors())
            return emitOriginalAfterForget(instr);

        ++mCtx.stats.folded;
        return emitConst(result, instr.loc);
    }

    ConstantFoldPass::Step ConstantFoldPass::foldComparison(const MirInstr& instr, int oldIdx) {
        Known left, right;
        const bool lk = knownReg(instr.src1, left);
        const bool rk = knownReg(instr.src2, right);

        if (!lk || !rk)
            return emitOriginalAfterForget(instr);

        bool result;
        if (instr.op == MirOp::LOGIC_AND)
            result = VM::valueToBool(left.value) && VM::valueToBool(right.value);
        else if (instr.op == MirOp::LOGIC_OR)
            result = VM::valueToBool(left.value) || VM::valueToBool(right.value);
        else {
            DiagnosticEngine diag;
            result = VM::applyComparison(left.value, right.value, instr.op, diag, instr.loc);
            if (diag.hasErrors())
                return emitOriginalAfterForget(instr);
        }

        ++mCtx.stats.folded;
        return emitConst(result, instr.loc);
    }

    ConstantFoldPass::Step ConstantFoldPass::foldOptional(const MirInstr& instr) {
        Known operand;
        if (!knownReg(instr.src1, operand))
            return emitOriginalAfterForget(instr);

        ValueNode::ValueType out;
        switch (instr.op) {
            case MirOp::UNWRAP:
                if (std::holds_alternative<std::monostate>(operand.value))
                    return emitOriginalAfterForget(instr);
                out = operand.value;
                break;
            case MirOp::TYPE_OF:
                out = VM::typeNameOf(operand.value);
                break;
            case MirOp::HAS_VALUE:
                out = !std::holds_alternative<std::monostate>(operand.value);
                break;
            case MirOp::IS_NONE:
                out = std::holds_alternative<std::monostate>(operand.value);
                break;
            default:
                return emitOriginalAfterForget(instr);
        }

        ++mCtx.stats.folded;
        return emitConst(out, instr.loc);
    }

    ConstantFoldPass::Step ConstantFoldPass::foldLoadSlot(const MirInstr& instr) {
        if (auto it = mSlotValues.find(instr.operand); it != mSlotValues.end()) {
            ++mCtx.stats.folded;
            const Step step = emitConst(it->second.value, instr.loc);
            mRegValues[instr.dst] = it->second;
            return step;
        }

        this->forgetDst(instr.dst);
        return emitOriginal(instr);
    }

    ConstantFoldPass::Step ConstantFoldPass::foldStoreSlot(const MirInstr& instr) {
        const int at = mCtx.emit(instr);

        Known value;
        if (knownReg(instr.src1, value))
            mSlotValues[instr.operand] = value;
        else
            mSlotValues.erase(instr.operand);

        return { at };
    }

    ConstantFoldPass::Step ConstantFoldPass::foldLoadVar(const MirInstr& instr) {
        const std::string& name = std::get<std::string>(mChunk.constants[instr.operand]);

        if (auto it = mNameValues.find(name); it != mNameValues.end()) {
            ++mCtx.stats.folded;
            const Step step = emitConst(it->second.value, instr.loc);
            mRegValues[instr.dst] = it->second;
            return step;
        }

        this->forgetDst(instr.dst);
        return emitOriginal(instr);
    }

    ConstantFoldPass::Step ConstantFoldPass::foldStoreVar(const MirInstr& instr) {
        const int at = mCtx.emit(instr);

        const std::string& name = std::get<std::string>(mChunk.constants[instr.operand]);
        Known value;
        if (knownReg(instr.src1, value))
            mNameValues[name] = value;
        else
            mNameValues.erase(name);

        return { at };
    }

    ConstantFoldPass::Step ConstantFoldPass::foldMakeArray(const MirInstr& instr, int oldIdx) {
        const int count = instr.operand;
        if (count < 0) {
            this->forgetDst(instr.dst);
            return emitOriginal(instr);
        }

        std::vector<ValueNode::ValueType> elements(count);
        for (int k = 0; k < count; ++k) {
            Known value;
            if (!knownReg(instr.src1 + k, value) || !value.removable) {
                this->forgetDst(instr.dst);
                return emitOriginal(instr);
            }
            elements[k] = value.value;
        }

        auto arr = std::make_shared<ArrayValue>();
        arr->elements = std::move(elements);

        ++mCtx.stats.folded;
        return emitConst(arr, instr.loc);
    }

    ConstantFoldPass::Step ConstantFoldPass::foldLoadIndex(const MirInstr& instr, int oldIdx) {
        Known target, index;
        if (!knownReg(instr.src1, target) || !knownReg(instr.src2, index))
            return emitOriginalAfterForget(instr);

        if (!std::holds_alternative<int>(index.value) ||
            !std::holds_alternative<ArrayRef>(target.value)) {
            return emitOriginalAfterForget(instr);
        }

        const int idx = std::get<int>(index.value);
        const auto& arr = std::get<ArrayRef>(target.value);

        if (idx < 0 || idx >= static_cast<int>(arr->elements.size()) ||
            std::holds_alternative<ArrayRef>(arr->elements[idx])) {
            return emitOriginalAfterForget(instr);
        }

        ++mCtx.stats.folded;
        return emitConst(arr->elements[idx], instr.loc);
    }

    ConstantFoldPass::Step ConstantFoldPass::foldStoreIndex(const MirInstr& instr) {
        return emitOriginal(instr);
    }

    ConstantFoldPass::Step ConstantFoldPass::foldCall(const MirInstr& instr, int oldIdx) {
        const FuncMeta* meta = instr.operand >= 0 && instr.operand < static_cast<int>(mChunk.functions.size())
            ? &mChunk.functions[instr.operand]
            : nullptr;

        if (meta && meta->argCount == instr.imm) {
            std::vector<ValueNode::ValueType> args(instr.imm);
            bool allKnown = true;

            for (int k = 0; k < instr.imm; ++k) {
                Known value;
                if (knownReg(instr.src1 + k, value))
                    args[k] = value.value;
                else {
                    allKnown = false;
                    break;
                }
            }

            if (allKnown) {
                ValueNode::ValueType result;
                if (foldPureMath(meta->name, args, result)) {
                    ++mCtx.stats.folded;
                    return emitConst(result, instr.loc);
                }
            }
        }

        this->forgetDst(instr.dst);
        return emitOriginal(instr);
    }

    ConstantFoldPass::Step ConstantFoldPass::foldNativeMethod(const MirInstr& instr, int oldIdx) {
        if (instr.operand < 0 || instr.operand >= static_cast<int>(mChunk.nativeCalls.size())) {
            this->forgetDst(instr.dst);
            return emitOriginal(instr);
        }

        const auto& meta = mChunk.nativeCalls[instr.operand];

        if (!meta.isStatic) {
            const int receiverReg = instr.src1 + instr.imm;

            Known recv;
            if (knownReg(receiverReg, recv)) {
                std::vector<Known> args(meta.argCount);
                bool allKnown = true;
                for (int k = 0; k < meta.argCount; ++k) {
                    if (!knownReg(instr.src1 + k, args[k])) {
                        allKnown = false;
                        break;
                    }
                }

                if (allKnown) {
                    ValueNode::ValueType result;
                    bool folded = false;

                    if (std::holds_alternative<std::string>(recv.value)) {
                        const auto& s = std::get<std::string>(recv.value);

                        if (meta.name == "length" && meta.argCount == 0) {
                            result = static_cast<int>(codepointCount(s));
                            folded = true;
                        } else if (meta.name == "contains" && meta.argCount == 1) {
                            result = s.find(std::get<std::string>(args[0].value)) != std::string::npos;
                            folded = true;
                        } else if (meta.name == "startsWith" && meta.argCount == 1) {
                            const auto& p = std::get<std::string>(args[0].value);
                            result = s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
                            folded = true;
                        } else if (meta.name == "endsWith" && meta.argCount == 1) {
                            const auto& p = std::get<std::string>(args[0].value);
                            result = s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
                            folded = true;
                        } else if (meta.name == "indexOf" && meta.argCount == 1) {
                            const auto& needle = std::get<std::string>(args[0].value);
                            const size_t pos = s.find(needle);
                            result = pos == std::string::npos ? -1 : static_cast<int>(codepointDistance(s, pos));
                            folded = true;
                        } else if (meta.name == "split" && meta.argCount == 1) {
                            const auto& sep = std::get<std::string>(args[0].value);
                            auto arr = std::make_shared<ArrayValue>();
                            if (sep.empty()) {
                                for (size_t i = 0; i < s.size(); i += codepointWidth(s[i]))
                                    arr->elements.emplace_back(s.substr(i, codepointWidth(s[i])));
                            } else {
                                size_t start = 0;
                                while (true) {
                                    const size_t pos = s.find(sep, start);
                                    if (pos == std::string::npos) {
                                        arr->elements.emplace_back(s.substr(start));
                                        break;
                                    }
                                    arr->elements.emplace_back(s.substr(start, pos - start));
                                    start = pos + sep.size();
                                }
                            }
                            result = arr;
                            folded = true;
                        } else if (meta.name == "toInt" && meta.argCount == 0) {
                            int v = 0;
                            const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
                            if (ec == std::errc{} && ptr == s.data() + s.size()) {
                                result = v;
                                folded = true;
                            }
                        } else if (meta.name == "toFloat" && meta.argCount == 0) {
                            float v = 0.0f;
                            const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
                            if (ec == std::errc{} && ptr == s.data() + s.size()) {
                                result = v;
                                folded = true;
                            }
                        }
                    } else if (std::holds_alternative<ArrayRef>(recv.value)) {
                        const auto& arr = std::get<ArrayRef>(recv.value);

                        if (meta.name == "length" && meta.argCount == 0) {
                            result = static_cast<int>(arr->elements.size());
                            folded = true;
                        } else if (meta.name == "contains" && meta.argCount == 1) {
                            const auto& needle = args[0].value;
                            bool found = false;
                            for (const auto& element : arr->elements) {
                                if (valuesEqual(element, needle)) {
                                    found = true;
                                    break;
                                }
                            }
                            result = found;
                            folded = true;
                        } else if (meta.name == "indexOf" && meta.argCount == 1) {
                            const auto& needle = args[0].value;
                            int found = -1;
                            for (size_t i = 0; i < arr->elements.size(); ++i) {
                                if (valuesEqual(arr->elements[i], needle)) {
                                    found = static_cast<int>(i);
                                    break;
                                }
                            }
                            result = found;
                            folded = true;
                        } else if (meta.name == "join" && meta.argCount == 1) {
                            const auto& sep = std::get<std::string>(args[0].value);
                            std::string out;
                            for (size_t i = 0; i < arr->elements.size(); ++i) {
                                if (i != 0)
                                    out += sep;
                                out += VM::valueToString(arr->elements[i]);
                            }
                            result = out;
                            folded = true;
                        } else if (meta.name == "slice" && meta.argCount == 2) {
                            const int len = static_cast<int>(arr->elements.size());
                            const int start = std::clamp(std::get<int>(args[0].value), 0, len);
                            const int end = std::clamp(std::get<int>(args[1].value), 0, len);
                            auto out = std::make_shared<ArrayValue>();
                            if (start < end)
                                out->elements.assign(arr->elements.begin() + start, arr->elements.begin() + end);
                            result = out;
                            folded = true;
                        }
                    }

                    if (folded) {
                        ++mCtx.stats.folded;
                        return emitConst(result, instr.loc);
                    }
                }
            }
        }

        this->forgetDst(instr.dst);
        return emitOriginal(instr);
    }

    ConstantFoldPass::Step ConstantFoldPass::foldBranch(const MirInstr& instr, int oldIdx) {
        if (instr.op == MirOp::JMP)
            return emitOriginal(instr);

        Known cond;
        if (!knownReg(instr.src1, cond))
            return emitOriginal(instr);

        if (alwaysJumps(instr.op, cond.value)) {
            ++mCtx.stats.folded;
            return { mCtx.emit({ MirOp::JMP, instr.operand, -1, -1, -1, -1, 0, {}, instr.loc }) };
        }

        ++mCtx.stats.removed;
        return {};
    }

    ConstantFoldPass::Step ConstantFoldPass::emitConst(const ValueNode::ValueType& value, const SourceLocation& loc) {
        const int at = emitLoadConst(mChunk, mCtx.foldedCode, value, loc);
        return { at };
    }

    ConstantFoldPass::Step ConstantFoldPass::emitMove(int dst, int src, const SourceLocation& loc) {
        const int at = mCtx.emit({ MirOp::MOVE, 0, dst, src, -1, -1, 0, {}, loc });
        if (auto it = mRegValues.find(src); it != mRegValues.end())
            mRegValues[dst] = it->second;
        else
            mRegValues.erase(dst);
        return { at };
    }

    ConstantFoldPass::Step ConstantFoldPass::emitOriginal(const MirInstr& instr) {
        return { mCtx.emit(instr) };
    }

    ConstantFoldPass::Step ConstantFoldPass::emitOriginalAfterForget(const MirInstr& instr) {
        this->forgetDst(instr.dst);
        return { mCtx.emit(instr) };
    }

    bool ConstantFoldPass::knownReg(int reg, Known& out) const {
        auto it = mRegValues.find(reg);
        if (it == mRegValues.end())
            return false;
        out = it->second;
        return true;
    }

    void ConstantFoldPass::forgetDst(int dst) {
        if (dst >= 0)
            mRegValues.erase(dst);
    }
}
