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
    bool alwaysJumps(OpCode op, const ValueNode::ValueType& value) {
        const bool truthy = VM::valueToBool(value);
        return (op == OpCode::JMP_IF_FALSE) ? !truthy : truthy;
    }

    OpCode invertedBranch(OpCode op) {
        return op == OpCode::JMP_IF_FALSE ? OpCode::JMP_IF_TRUE : OpCode::JMP_IF_FALSE;
    }

    bool applyUnaryOperator(OpCode op, const ValueNode::ValueType& value, ValueNode::ValueType& out) {
        if (op == OpCode::NEG || op == OpCode::NEG_I) {
            DiagnosticEngine foldDiag;
            out = VM::applyUnary(value, OpCode::NEG, foldDiag);
            return !foldDiag.hasErrors();
        }

        out = !VM::valueToBool(value);
        return true;
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
}

    void ConstantFoldPass::run(bool enabled) {
        for (size_t i = 0; i < mChunk.code.size(); ++i) {
            const int oldIdx = static_cast<int>(i);

            if (mJumps.isTarget(oldIdx)) {
                mCtx.resetStack();
                mCtx.clearTracked();
            } else if (canWriteVariables(mChunk.code[i].op)) {
                mCtx.clearTracked();
            }

            const Step step = enabled ? fold(oldIdx) : emitOpaque(mChunk.code[i]);

            if (step.emittedAt >= 0) {
                mCtx.oldToNew[oldIdx] = step.emittedAt;
                mCtx.newToOld.push_back(oldIdx);
            }

            if (step.skipNext)
                ++i;
        }
    }

    ConstantFoldPass::Step ConstantFoldPass::fold(int oldIdx) {
        const Instruction& instr = mChunk.code[oldIdx];

        switch (instr.op) {
            case OpCode::PUSH_INT:
            case OpCode::PUSH_FLOAT:
            case OpCode::PUSH_STR:
            case OpCode::PUSH_BOOL:
            case OpCode::PUSH_NONE:
                return foldPush(instr, oldIdx);

            case OpCode::UNWRAP:
            case OpCode::TYPE_OF:
            case OpCode::HAS_VALUE:
            case OpCode::IS_NONE:
                return foldNullish(instr, oldIdx);

            case OpCode::DUP_IS_NONE:
                return foldDupIsNone(instr);

            case OpCode::LOAD_SLOT:
            case OpCode::LOAD_VAR:
            case OpCode::STORE_SLOT:
            case OpCode::STORE_VAR:
            case OpCode::DUP_STORE_SLOT:
            case OpCode::DUP_STORE:
                return foldVariable(instr);

            case OpCode::DUP:
            case OpCode::POP:
                return foldStack(instr, oldIdx);

            case OpCode::ADD:
            case OpCode::SUB:
            case OpCode::MUL:
            case OpCode::DIV:
            case OpCode::MOD:
            case OpCode::POW:
            case OpCode::ADD_I:
            case OpCode::SUB_I:
            case OpCode::MUL_I:
            case OpCode::MOD_I:
                return foldArithmetic(instr, oldIdx);

            case OpCode::NEG:
            case OpCode::NEG_I:
            case OpCode::NOT:
                return foldUnary(instr, oldIdx);

            case OpCode::CMP_EQ:
            case OpCode::CMP_NE:
            case OpCode::CMP_GT:
            case OpCode::CMP_LT:
            case OpCode::CMP_GE:
            case OpCode::CMP_LE:
            case OpCode::CMP_EQ_I:
            case OpCode::CMP_NE_I:
            case OpCode::CMP_GT_I:
            case OpCode::CMP_LT_I:
            case OpCode::CMP_GE_I:
            case OpCode::CMP_LE_I:
            case OpCode::LOGIC_AND:
            case OpCode::LOGIC_OR:
                return foldComparison(instr, oldIdx);

            case OpCode::MAKE_ARRAY:
                return foldMakeArray(instr, oldIdx);

            case OpCode::LOAD_INDEX:
                return foldLoadIndex(instr, oldIdx);

            case OpCode::STORE_INDEX:
                return foldStoreIndex(instr);

            case OpCode::CALL:
                return foldCall(instr, oldIdx);

            case OpCode::CALL_NATIVE_METHOD:
                return foldNativeMethod(instr, oldIdx);

            case OpCode::JMP_IF_FALSE:
            case OpCode::JMP_IF_TRUE:
                return foldBranch(instr, oldIdx);

            case OpCode::JMP:
            case OpCode::RETURN:
            case OpCode::HALT:
            default:
                return emitOpaque(instr);
        }
    }

    ConstantFoldPass::Step ConstantFoldPass::foldPush(const Instruction& instr, int oldIdx) {
        const int at = mCtx.emit(instr);

        mCtx.stack.emplace_back(TrackedValue{ mChunk.constants[instr.operand], at, !mJumps.isTarget(oldIdx) });

        return { at, false };
    }

    ConstantFoldPass::Step ConstantFoldPass::foldNullish(const Instruction& instr, int oldIdx) {
        const bool fusable = instr.op == OpCode::IS_NONE && oldIdx > 0 &&
            mChunk.code[oldIdx - 1].op == OpCode::DUP &&
            !mJumps.isTarget(oldIdx) && !mJumps.isTarget(oldIdx - 1) &&
            !mCtx.foldedCode.empty() && mCtx.foldedCode.back().op == OpCode::DUP;

        if (fusable) {
            const StackEntry operand = popEntry(mCtx.stack);

            mCtx.drop(static_cast<int>(mCtx.foldedCode.size()) - 1);

            const int at = mCtx.emit({ OpCode::DUP_IS_NONE, 0, instr.loc });

            if (isKnown(operand))
                mCtx.stack.emplace_back(TrackedValue{ std::holds_alternative<std::monostate>(knownValue(operand).value), at, false });
            else
                mCtx.stack.emplace_back(std::monostate{});

            ++mCtx.stats.folded;

            return { at, false };
        }

        switch (instr.op) {
            case OpCode::UNWRAP:
                return foldOperand(instr, oldIdx, [](const ValueNode::ValueType& value, ValueNode::ValueType& out) {
                    if (std::holds_alternative<std::monostate>(value))
                        return false;

                    out = value;
                    return true;
                });

            case OpCode::TYPE_OF:
                return foldOperand(instr, oldIdx, [](const ValueNode::ValueType& value, ValueNode::ValueType& out) {
                    out = VM::typeNameOf(value);
                    return true;
                });

            case OpCode::HAS_VALUE:
                return foldOperand(instr, oldIdx, [](const ValueNode::ValueType& value, ValueNode::ValueType& out) {
                    out = !std::holds_alternative<std::monostate>(value);
                    return true;
                });

            default:
                return foldOperand(instr, oldIdx, [](const ValueNode::ValueType& value, ValueNode::ValueType& out) {
                    out = std::holds_alternative<std::monostate>(value);
                    return true;
                });
        }
    }

    ConstantFoldPass::Step ConstantFoldPass::foldDupIsNone(const Instruction& instr) {
        const int at = mCtx.emit(instr);

        if (mCtx.stack.size() > 1 && isKnown(mCtx.stack.back())) {
            const bool result = std::holds_alternative<std::monostate>(knownValue(mCtx.stack.back()).value);

            mCtx.pinTop();
            mCtx.stack.emplace_back(TrackedValue{ result, at, false });
        } else {
            mCtx.pinTop();
            mCtx.stack.emplace_back(std::monostate{});
        }

        return { at, false };
    }

    ConstantFoldPass::Step ConstantFoldPass::foldVariable(const Instruction& instr) {
        switch (instr.op) {
            case OpCode::LOAD_SLOT: {
                if (auto slot = mCtx.slotValues.find(instr.operand); slot != mCtx.slotValues.end()) {
                    const int at = emitConstant(slot->second, instr.loc);

                    mCtx.stack.emplace_back(TrackedValue{ slot->second, at, true });
                    ++mCtx.stats.folded;

                    return { at, false };
                }

                return emitUnknown(instr);
            }

            case OpCode::LOAD_VAR: {
                const std::string& name = std::get<std::string>(mChunk.constants[instr.operand]);

                if (auto slot = mCtx.nameValues.find(name); slot != mCtx.nameValues.end()) {
                    const int at = emitConstant(slot->second, instr.loc);

                    mCtx.stack.emplace_back(TrackedValue{ slot->second, at, true });
                    ++mCtx.stats.folded;

                    return { at, false };
                }

                return emitUnknown(instr);
            }

            case OpCode::STORE_SLOT:
                trackSlot(instr.operand, popEntry(mCtx.stack));
                return { mCtx.emit(instr), false };

            case OpCode::STORE_VAR:
                trackName(std::get<std::string>(mChunk.constants[instr.operand]), popEntry(mCtx.stack));
                return { mCtx.emit(instr), false };

            case OpCode::DUP_STORE_SLOT:
                trackSlot(instr.operand, mCtx.stack.back());
                mCtx.pinTop();
                return { mCtx.emit(instr), false };

            default:
                trackName(std::get<std::string>(mChunk.constants[instr.operand]), mCtx.stack.back());
                mCtx.pinTop();
                return { mCtx.emit(instr), false };
        }
    }

    ConstantFoldPass::Step ConstantFoldPass::foldStack(const Instruction& instr, int oldIdx) {
        if (instr.op == OpCode::POP) {
            if (!mJumps.isTarget(oldIdx) && mCtx.stack.size() > 1 &&
                isKnown(mCtx.stack.back()) && knownValue(mCtx.stack.back()).removable) {
                mCtx.drop(knownValue(mCtx.stack.back()).producer);
                mCtx.stack.pop_back();
                ++mCtx.stats.removed;

                return {};
            }

            popEntry(mCtx.stack);

            return { mCtx.emit(instr), false };
        }

        const bool fusable = oldIdx + 1 < static_cast<int>(mChunk.code.size()) &&
            isStoreOp(mChunk.code[oldIdx + 1].op) &&
            !mJumps.isTarget(oldIdx) && !mJumps.isTarget(oldIdx + 1);

        if (fusable) {
            const Instruction& next = mChunk.code[oldIdx + 1];

            mCtx.pinTop();

            if (next.op == OpCode::STORE_SLOT)
                trackSlot(next.operand, mCtx.stack.back());
            else
                trackName(std::get<std::string>(mChunk.constants[next.operand]), mCtx.stack.back());

            ++mCtx.stats.folded;

            const int at = mCtx.emit({
                next.op == OpCode::STORE_SLOT ? OpCode::DUP_STORE_SLOT : OpCode::DUP_STORE,
                next.operand,
                instr.loc
            });

            return { at, true };
        }

        mCtx.pinTop();
        mCtx.stack.push_back(mCtx.stack.back());

        return { mCtx.emit(instr), false };
    }

    ConstantFoldPass::Step ConstantFoldPass::foldArithmetic(const Instruction& instr, int oldIdx) {
        const StackEntry right = popEntry(mCtx.stack);
        const StackEntry left = popEntry(mCtx.stack);

        if (isKnown(left) && isKnown(right) && knownValue(left).removable && knownValue(right).removable) {
            DiagnosticEngine foldDiag;
            const ValueNode::ValueType result = VM::applyArithmetic(
                knownValue(left).value, knownValue(right).value, instr.op, foldDiag);

            if (foldDiag.hasErrors())
                return emitUnknown(instr);

            mCtx.drop(knownValue(left).producer);
            mCtx.drop(knownValue(right).producer);

            const int at = emitConstant(result, instr.loc);

            mCtx.stack.emplace_back(TrackedValue{ result, at, !mJumps.isTarget(oldIdx) });
            ++mCtx.stats.folded;

            return { at, false };
        }

        if (identityEligible(instr.op, left, right)) {
            mCtx.drop(knownValue(right).producer);
            mCtx.stack.emplace_back(knownValue(left));
            ++mCtx.stats.folded;

            return {};
        }

        const bool identityOp = instr.op == OpCode::ADD || instr.op == OpCode::MUL ||
            instr.op == OpCode::ADD_I || instr.op == OpCode::MUL_I;

        if (identityOp && identityEligible(instr.op, right, left)) {
            mCtx.drop(knownValue(left).producer);
            mCtx.stack.emplace_back(knownValue(right));
            ++mCtx.stats.folded;

            return {};
        }

        return emitUnknown(instr);
    }

    ConstantFoldPass::Step ConstantFoldPass::foldUnary(const Instruction& instr, int oldIdx) {
        const StackEntry operand = popEntry(mCtx.stack);

        if (!isKnown(operand) || !knownValue(operand).removable)
            return emitUnknown(instr);

        ValueNode::ValueType result;
        const bool foldable = applyUnaryOperator(instr.op, knownValue(operand).value, result);

        ++mCtx.stats.folded;

        if (!foldable)
            return emitUnknown(instr);

        mCtx.drop(knownValue(operand).producer);

        const int at = emitConstant(result, instr.loc);

        mCtx.stack.emplace_back(TrackedValue{ result, at, !mJumps.isTarget(oldIdx) });

        return { at, false };
    }

    ConstantFoldPass::Step ConstantFoldPass::foldComparison(const Instruction& instr, int oldIdx) {
        const StackEntry right = popEntry(mCtx.stack);
        const StackEntry left = popEntry(mCtx.stack);

        if (!isKnown(left) || !isKnown(right) || !knownValue(left).removable || !knownValue(right).removable)
            return emitUnknown(instr);

        bool result;

        if (instr.op == OpCode::LOGIC_AND || instr.op == OpCode::LOGIC_OR) {
            const bool l = VM::valueToBool(knownValue(left).value);
            const bool r = VM::valueToBool(knownValue(right).value);

            result = (instr.op == OpCode::LOGIC_AND) ? (l && r) : (l || r);
        } else {
            DiagnosticEngine foldDiag;

            result = VM::applyComparison(
                knownValue(left).value, knownValue(right).value, instr.op, foldDiag);

            if (foldDiag.hasErrors())
                return emitUnknown(instr);
        }

        mCtx.drop(knownValue(left).producer);
        mCtx.drop(knownValue(right).producer);

        const int at = emitConstant(result, instr.loc);

        mCtx.stack.emplace_back(TrackedValue{ result, at, !mJumps.isTarget(oldIdx) });
        ++mCtx.stats.folded;

        return { at, false };
    }

    ConstantFoldPass::Step ConstantFoldPass::foldMakeArray(const Instruction& instr, int oldIdx) {
        const int count = instr.operand;
        bool foldable = count == 0 || static_cast<int>(mCtx.stack.size()) > count;

        std::vector<ValueNode::ValueType> elements;
        std::vector<int> producers;

        if (foldable && count > 0) {
            elements.resize(count);
            producers.resize(count);

            for (int k = count - 1; k >= 0; --k) {
                const StackEntry entry = popEntry(mCtx.stack);
                if (!isKnown(entry) || !knownValue(entry).removable) {
                    foldable = false;
                    break;
                }

                elements[k] = knownValue(entry).value;
                producers[k] = knownValue(entry).producer;
            }
        }

        if (!foldable) {
            mCtx.resetStack();

            return { mCtx.emit(instr), false };
        }

        for (const int producer : producers)
            mCtx.drop(producer);

        auto arr = std::make_shared<ArrayValue>();
        arr->elements = std::move(elements);

        const int at = emitConstant(arr, instr.loc);

        mCtx.stack.emplace_back(TrackedValue{ arr, at, !mJumps.isTarget(oldIdx) });
        ++mCtx.stats.folded;

        return { at, false };
    }

    ConstantFoldPass::Step ConstantFoldPass::foldLoadIndex(const Instruction& instr, int oldIdx) {
        const StackEntry indexEntry = popEntry(mCtx.stack);
        const StackEntry targetEntry = popEntry(mCtx.stack);

        bool foldable = isKnown(indexEntry) && isKnown(targetEntry) &&
            knownValue(indexEntry).removable && knownValue(targetEntry).removable &&
            std::holds_alternative<int>(knownValue(indexEntry).value) &&
            std::holds_alternative<ArrayRef>(knownValue(targetEntry).value);

        int index = 0;
        if (foldable) {
            index = std::get<int>(knownValue(indexEntry).value);

            const auto& target = std::get<ArrayRef>(knownValue(targetEntry).value);

            foldable = index >= 0 && index < static_cast<int>(target->elements.size()) &&
                !std::holds_alternative<ArrayRef>(target->elements[index]);
        }

        if (!foldable)
            return emitUnknown(instr);

        mCtx.drop(knownValue(indexEntry).producer);
        mCtx.drop(knownValue(targetEntry).producer);

        const auto& element = std::get<ArrayRef>(knownValue(targetEntry).value)->elements[index];

        const int at = emitConstant(element, instr.loc);

        mCtx.stack.emplace_back(TrackedValue{ element, at, !mJumps.isTarget(oldIdx) });
        ++mCtx.stats.folded;

        return { at, false };
    }

    ConstantFoldPass::Step ConstantFoldPass::foldStoreIndex(const Instruction& instr) {
        popEntry(mCtx.stack);
        popEntry(mCtx.stack);
        popEntry(mCtx.stack);

        return { mCtx.emit(instr), false };
    }

    ConstantFoldPass::Step ConstantFoldPass::foldCall(const Instruction& instr, int oldIdx) {
        const FuncMeta* meta = instr.operand >= 0 && instr.operand < static_cast<int>(mChunk.functions.size())
            ? &mChunk.functions[instr.operand]
            : nullptr;

        bool foldable = meta != nullptr && meta->argCount <= static_cast<int>(mCtx.stack.size()) - 1;

        std::vector<StackEntry> popped;
        if (foldable) {
            popped.reserve(meta->argCount);

            for (int k = 0; k < meta->argCount && foldable; ++k) {
                StackEntry entry = popEntry(mCtx.stack);

                foldable = isKnown(entry) && knownValue(entry).removable;
                popped.push_back(std::move(entry));
            }
        }

        ValueNode::ValueType result;
        if (foldable) {
            std::vector<ValueNode::ValueType> args(popped.size());
            for (size_t k = 0; k < popped.size(); ++k)
                args[popped.size() - 1 - k] = knownValue(popped[k]).value;

            foldable = foldPureMath(meta->name, args, result);
        }

        if (!foldable) {
            mCtx.resetStack();

            return { mCtx.emit(instr), false };
        }

        for (const auto& entry : popped)
            mCtx.drop(knownValue(entry).producer);

        const int at = emitConstant(result, instr.loc);

        mCtx.stack.emplace_back(TrackedValue{ result, at, !mJumps.isTarget(oldIdx) });
        ++mCtx.stats.folded;

        return { at, false };
    }

    ConstantFoldPass::Step ConstantFoldPass::foldNativeMethod(const Instruction& instr, int oldIdx) {
        if (instr.operand < 0 || instr.operand >= static_cast<int>(mChunk.nativeCalls.size()))
            return emitOpaque(instr);

        const auto& meta = mChunk.nativeCalls[instr.operand];

        if (meta.isStatic || static_cast<int>(mCtx.stack.size()) <= meta.argCount + 1)
            return emitOpaque(instr);

        const StackEntry receiver = popEntry(mCtx.stack);
        std::vector<StackEntry> args(meta.argCount);
        for (int i = 0; i < meta.argCount; ++i)
            args[meta.argCount - 1 - i] = popEntry(mCtx.stack);

        if (!isKnown(receiver) || !knownValue(receiver).removable)
            return emitOpaque(instr);
        for (const auto& arg : args)
            if (!isKnown(arg) || !knownValue(arg).removable)
                return emitOpaque(instr);

        const auto& recv = knownValue(receiver).value;

        ValueNode::ValueType result;
        bool folded = false;

        if (std::holds_alternative<std::string>(recv)) {
            const auto& s = std::get<std::string>(recv);

            if (meta.name == "length" && meta.argCount == 0) {
                result = static_cast<int>(codepointCount(s));
                folded = true;
            } else if (meta.name == "contains" && meta.argCount == 1) {
                result = s.find(std::get<std::string>(knownValue(args[0]).value)) != std::string::npos;
                folded = true;
            } else if (meta.name == "startsWith" && meta.argCount == 1) {
                const auto& p = std::get<std::string>(knownValue(args[0]).value);
                result = s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
                folded = true;
            } else if (meta.name == "endsWith" && meta.argCount == 1) {
                const auto& p = std::get<std::string>(knownValue(args[0]).value);
                result = s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
                folded = true;
            } else if (meta.name == "indexOf" && meta.argCount == 1) {
                const auto& needle = std::get<std::string>(knownValue(args[0]).value);
                const size_t pos = s.find(needle);
                result = pos == std::string::npos ? -1 : static_cast<int>(codepointDistance(s, pos));
                folded = true;
            } else if (meta.name == "split" && meta.argCount == 1) {
                const auto& sep = std::get<std::string>(knownValue(args[0]).value);
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
        } else if (std::holds_alternative<ArrayRef>(recv)) {
            const auto& arr = std::get<ArrayRef>(recv);

            if (meta.name == "length" && meta.argCount == 0) {
                result = static_cast<int>(arr->elements.size());
                folded = true;
            } else if (meta.name == "contains" && meta.argCount == 1) {
                const auto& needle = knownValue(args[0]).value;
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
                const auto& needle = knownValue(args[0]).value;
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
                const auto& sep = std::get<std::string>(knownValue(args[0]).value);
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
                const int start = std::clamp(std::get<int>(knownValue(args[0]).value), 0, len);
                const int end = std::clamp(std::get<int>(knownValue(args[1]).value), 0, len);
                auto out = std::make_shared<ArrayValue>();
                if (start < end)
                    out->elements.assign(arr->elements.begin() + start, arr->elements.begin() + end);
                result = out;
                folded = true;
            }
        }

        if (!folded)
            return emitOpaque(instr);

        mCtx.drop(knownValue(receiver).producer);
        for (const auto& arg : args)
            mCtx.drop(knownValue(arg).producer);

        const int at = emitConstant(result, instr.loc);
        mCtx.stack.emplace_back(TrackedValue{ result, at, !mJumps.isTarget(oldIdx) });
        ++mCtx.stats.folded;

        return { at, false };
    }

    ConstantFoldPass::Step ConstantFoldPass::foldBranch(const Instruction& instr, int oldIdx) {
        const StackEntry cond = popEntry(mCtx.stack);
        const bool targeted = mJumps.isTarget(oldIdx);

        const bool deadElse = oldIdx + 1 < static_cast<int>(mChunk.code.size()) &&
            mChunk.code[oldIdx + 1].op == OpCode::JMP &&
            !targeted && !mJumps.isTarget(oldIdx + 1) &&
            oldIdx + 1 + instr.operand == oldIdx + 2 + mChunk.code[oldIdx + 1].operand;

        if (deadElse) {
            Step step;

            if (isKnown(cond) && knownValue(cond).removable) {
                mCtx.drop(knownValue(cond).producer);
                ++mCtx.stats.removed;
            } else {
                step = { mCtx.emit({ OpCode::POP, 0, instr.loc }), false };
            }

            ++mCtx.stats.folded;
            mCtx.resetStack();

            return step;
        }

        if (!targeted && isKnown(cond) && knownValue(cond).removable) {
            mCtx.drop(knownValue(cond).producer);

            Step step;

            if (alwaysJumps(instr.op, knownValue(cond).value))
                step = { mCtx.emit({ OpCode::JMP, instr.operand, instr.loc }), false };
            else
                ++mCtx.stats.removed;

            ++mCtx.stats.folded;
            mCtx.resetStack();

            return step;
        }

        if (isKnown(cond) && !knownValue(cond).removable && isScalarValue(knownValue(cond).value) &&
            alwaysJumps(instr.op, knownValue(cond).value) &&
            reachedOnlyByBackwardJumps(knownValue(cond).producer)) {
            mCtx.drop(knownValue(cond).producer);

            ++mCtx.stats.folded;
            mCtx.resetStack();

            return { mCtx.emit({ OpCode::JMP, instr.operand, instr.loc }), false };
        }

        const bool fusable = oldIdx > 0 && mChunk.code[oldIdx - 1].op == OpCode::NOT &&
            !targeted && !mJumps.isTarget(oldIdx - 1) &&
            !mCtx.foldedCode.empty() && mCtx.foldedCode.back().op == OpCode::NOT;

        if (fusable) {
            mCtx.drop(static_cast<int>(mCtx.foldedCode.size()) - 1);

            ++mCtx.stats.folded;
            mCtx.resetStack();

            return { mCtx.emit({ invertedBranch(instr.op), instr.operand, instr.loc }), false };
        }

        mCtx.resetStack();

        return { mCtx.emit(instr), false };
    }

    ConstantFoldPass::Step ConstantFoldPass::emitUnknown(const Instruction& instr) {
        const int at = mCtx.emit(instr);

        mCtx.stack.emplace_back(std::monostate{});

        return { at, false };
    }

    ConstantFoldPass::Step ConstantFoldPass::emitOpaque(const Instruction& instr) {
        const int at = mCtx.emit(instr);

        mCtx.resetStack();

        return { at, false };
    }

    template <typename Fold>
    ConstantFoldPass::Step ConstantFoldPass::foldOperand(const Instruction& instr, int oldIdx, Fold&& fold) {
        const StackEntry operand = popEntry(mCtx.stack);

        if (!isKnown(operand) || !knownValue(operand).removable)
            return emitUnknown(instr);

        ValueNode::ValueType result;
        if (!fold(knownValue(operand).value, result))
            return emitUnknown(instr);

        mCtx.drop(knownValue(operand).producer);

        const int at = emitConstant(result, instr.loc);

        mCtx.stack.emplace_back(TrackedValue{ result, at, !mJumps.isTarget(oldIdx) });
        ++mCtx.stats.folded;

        return { at, false };
    }

    int ConstantFoldPass::emitConstant(const ValueNode::ValueType& value, const SourceLocation& loc) {
        emitPush(mChunk, mCtx.foldedCode, value, loc);

        return static_cast<int>(mCtx.foldedCode.size()) - 1;
    }

    void ConstantFoldPass::trackSlot(int slot, const StackEntry& value) {
        if (isKnown(value) && isScalarValue(knownValue(value).value))
            mCtx.slotValues[slot] = knownValue(value).value;
        else
            mCtx.slotValues.erase(slot);
    }

    void ConstantFoldPass::trackName(const std::string& name, const StackEntry& value) {
        if (isKnown(value) && isScalarValue(knownValue(value).value))
            mCtx.nameValues[name] = knownValue(value).value;
        else
            mCtx.nameValues.erase(name);
    }

    bool ConstantFoldPass::reachedOnlyByBackwardJumps(int producer) const {
        if (producer < 0 || producer >= static_cast<int>(mCtx.foldedCode.size()))
            return false;

        switch (mCtx.foldedCode[producer].op) {
            case OpCode::PUSH_INT:
            case OpCode::PUSH_FLOAT:
            case OpCode::PUSH_STR:
            case OpCode::PUSH_BOOL:
            case OpCode::PUSH_NONE:
                break;
            default:
                return false;
        }

        if (producer >= static_cast<int>(mCtx.newToOld.size()))
            return false;

        const int producerOld = mCtx.newToOld[producer];
        const std::vector<int>* sources = mJumps.sourcesOf(producerOld);

        return sources != nullptr && std::ranges::all_of(*sources, [producerOld](int source) {
            return source > producerOld;
        });
    }
}
