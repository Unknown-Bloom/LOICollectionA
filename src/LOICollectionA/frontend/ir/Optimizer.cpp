#include <vector>
#include <variant>
#include <unordered_map>
#include <unordered_set>

#include "LOICollectionA/frontend/DiagnosticEngine.h"

#include "LOICollectionA/frontend/ir/VM.h"

#include "LOICollectionA/frontend/ir/opt/OptContext.h"
#include "LOICollectionA/frontend/ir/opt/analysis/FoldPredicates.h"
#include "LOICollectionA/frontend/ir/opt/analysis/OpTraits.h"

#include "LOICollectionA/frontend/ir/Optimizer.h"

namespace LOICollection::frontend::ir {
    using namespace opt;

    Optimizer::Stats Optimizer::optimize(BytecodeChunk& chunk) {
        Stats total;

        Stats mainStats = this->optimizeChunk(chunk);
        total.folded += mainStats.folded;
        total.removed += mainStats.removed;

        for (auto& body : chunk.methodBodies) {
            Stats bodyStats = this->optimizeChunk(*body);
            total.folded += bodyStats.folded;
            total.removed += bodyStats.removed;
        }

        return total;
    }

    // Repeat the single pass until it stops making progress: each round's rewrites
    // (forwarded loads, folded branches, dropped operands) expose new opportunities
    // for the next round. A pass that changes nothing means the chunk is stable.
    Optimizer::Stats Optimizer::optimizeChunk(BytecodeChunk& chunk) {
        Stats total;

        for (int pass = 0; pass < 16; ++pass) {
            Stats once = this->optimizeChunkOnce(chunk);
            total.folded += once.folded;
            total.removed += once.removed;

            if (once.folded == 0 && once.removed == 0)
                break;
        }

        return total;
    }

    Optimizer::Stats Optimizer::optimizeChunkOnce(BytecodeChunk& chunk) {
        Stats stats;

        const auto& code = chunk.code;
        if (code.empty())
            return stats;

        std::unordered_set<int> targets;
        std::unordered_map<int, std::vector<int>> jumpSources;
        for (size_t i = 0; i < code.size(); ++i) {
            if (isJump(code[i].op)) {
                int target = static_cast<int>(i) + 1 + code[i].operand;
                if (target >= 0 && target < static_cast<int>(code.size())) {
                    targets.insert(target);
                    jumpSources[target].push_back(static_cast<int>(i));
                }
            }
        }

        std::vector<Instruction> foldedCode;
        std::vector<int> newToOld;
        std::vector<int> oldToNew(code.size(), -1);
        std::vector<bool> dropped(code.size(), false);
        std::vector<StackEntry> stack{ std::monostate{} };

        std::unordered_map<int, ValueNode::ValueType> slotValues;
        std::unordered_map<std::string, ValueNode::ValueType> nameValues;

        const auto clearTracked = [&slotValues, &nameValues] {
            slotValues.clear();
            nameValues.clear();
        };

        for (size_t i = 0; i < code.size(); ++i) {
            const auto& instr = code[i];
            const int oldIdx = static_cast<int>(i);
            int emittedAt = -1;
            bool skipNext = false;

            if (targets.contains(oldIdx)) {
                stack.assign(1, std::monostate{});
                clearTracked();
            } else if (canWriteVariables(instr.op)) {
                clearTracked();
            }

            switch (instr.op) {
                case OpCode::PUSH_INT:
                case OpCode::PUSH_FLOAT:
                case OpCode::PUSH_STR:
                case OpCode::PUSH_BOOL:
                case OpCode::PUSH_NONE:
                    emittedAt = static_cast<int>(foldedCode.size());
                    foldedCode.push_back(instr);
                    stack.emplace_back(TrackedValue{
                        chunk.constants[instr.operand], emittedAt, !targets.contains(oldIdx)
                    });
                    
                    break;

                case OpCode::UNWRAP: {
                    StackEntry operand = popEntry(stack);

                    if (isKnown(operand) && knownValue(operand).removable &&
                        !std::holds_alternative<std::monostate>(knownValue(operand).value)) {
                        dropped[knownValue(operand).producer] = true;

                        emitPush(chunk, foldedCode, knownValue(operand).value, instr.loc);
                        emittedAt = static_cast<int>(foldedCode.size()) - 1;
                        stack.emplace_back(TrackedValue{
                            knownValue(operand).value, emittedAt, !targets.contains(oldIdx)
                        });
                        stats.folded++;
                    } else {
                        emittedAt = static_cast<int>(foldedCode.size());
                        foldedCode.push_back(instr);
                        stack.emplace_back(std::monostate{});
                    }

                    break;
                }

                case OpCode::TYPE_OF: {
                    StackEntry operand = popEntry(stack);

                    if (isKnown(operand) && knownValue(operand).removable) {
                        dropped[knownValue(operand).producer] = true;

                        std::string name = VM::typeNameOf(knownValue(operand).value);
                        emitPush(chunk, foldedCode, name, instr.loc);
                        emittedAt = static_cast<int>(foldedCode.size()) - 1;
                        stack.emplace_back(TrackedValue{
                            name, emittedAt, !targets.contains(oldIdx)
                        });
                        stats.folded++;
                    } else {
                        emittedAt = static_cast<int>(foldedCode.size());
                        foldedCode.push_back(instr);
                        stack.emplace_back(std::monostate{});
                    }

                    break;
                }

                case OpCode::HAS_VALUE: {
                    StackEntry operand = popEntry(stack);

                    if (isKnown(operand) && knownValue(operand).removable) {
                        dropped[knownValue(operand).producer] = true;

                        bool result = !std::holds_alternative<std::monostate>(
                            knownValue(operand).value);
                        emitPush(chunk, foldedCode, result, instr.loc);
                        emittedAt = static_cast<int>(foldedCode.size()) - 1;
                        stack.emplace_back(TrackedValue{
                            result, emittedAt, !targets.contains(oldIdx)
                        });
                        stats.folded++;
                    } else {
                        emittedAt = static_cast<int>(foldedCode.size());
                        foldedCode.push_back(instr);
                        stack.emplace_back(std::monostate{});
                    }

                    break;
                }

                case OpCode::IS_NONE: {
                    if (i > 0 && code[i - 1].op == OpCode::DUP &&
                        !targets.contains(oldIdx) && !targets.contains(oldIdx - 1) &&
                        !foldedCode.empty() && foldedCode.back().op == OpCode::DUP) {
                        StackEntry operand = popEntry(stack);

                        dropped[static_cast<int>(foldedCode.size()) - 1] = true;

                        emittedAt = static_cast<int>(foldedCode.size());
                        foldedCode.push_back({ OpCode::DUP_IS_NONE, 0, instr.loc });

                        if (isKnown(operand)) {
                            bool result =
                                std::holds_alternative<std::monostate>(knownValue(operand).value);
                            stack.emplace_back(TrackedValue{ result, emittedAt, false });
                        } else {
                            stack.emplace_back(std::monostate{});
                        }

                        stats.folded++;
                        break;
                    }

                    StackEntry operand = popEntry(stack);

                    if (isKnown(operand) && knownValue(operand).removable) {
                        dropped[knownValue(operand).producer] = true;

                        bool result = std::holds_alternative<std::monostate>(
                            knownValue(operand).value);
                        emitPush(chunk, foldedCode, result, instr.loc);
                        emittedAt = static_cast<int>(foldedCode.size()) - 1;
                        stack.emplace_back(TrackedValue{
                            result, emittedAt, !targets.contains(oldIdx)
                        });
                        stats.folded++;
                    } else {
                        emittedAt = static_cast<int>(foldedCode.size());
                        foldedCode.push_back(instr);
                        stack.emplace_back(std::monostate{});
                    }

                    break;
                }

                case OpCode::DUP_IS_NONE: {
                    emittedAt = static_cast<int>(foldedCode.size());

                    if (stack.size() > 1 && isKnown(stack.back())) {
                        bool result = std::holds_alternative<std::monostate>(
                            knownValue(stack.back()).value);

                        if (auto* known = std::get_if<TrackedValue>(&stack.back()))
                            known->removable = false;

                        stack.emplace_back(TrackedValue{ result, emittedAt, false });
                    } else {
                        if (auto* known = std::get_if<TrackedValue>(&stack.back()))
                            known->removable = false;

                        stack.emplace_back(std::monostate{});
                    }

                    foldedCode.push_back(instr);

                    break;
                }

                case OpCode::LOAD_SLOT: {
                    auto slot = slotValues.find(instr.operand);

                    if (slot != slotValues.end()) {
                        emitPush(chunk, foldedCode, slot->second, instr.loc);
                        emittedAt = static_cast<int>(foldedCode.size()) - 1;
                        stack.emplace_back(TrackedValue{ slot->second, emittedAt, true });
                        stats.folded++;
                    } else {
                        emittedAt = static_cast<int>(foldedCode.size());
                        foldedCode.push_back(instr);
                        stack.emplace_back(std::monostate{});
                    }

                    break;
                }

                case OpCode::LOAD_VAR: {
                    const std::string& name = std::get<std::string>(chunk.constants[instr.operand]);
                    auto slot = nameValues.find(name);

                    if (slot != nameValues.end()) {
                        emitPush(chunk, foldedCode, slot->second, instr.loc);
                        emittedAt = static_cast<int>(foldedCode.size()) - 1;
                        stack.emplace_back(TrackedValue{ slot->second, emittedAt, true });
                        stats.folded++;
                    } else {
                        emittedAt = static_cast<int>(foldedCode.size());
                        foldedCode.push_back(instr);
                        stack.emplace_back(std::monostate{});
                    }

                    break;
                }

                case OpCode::STORE_SLOT: {
                    StackEntry value = popEntry(stack);

                    if (isKnown(value) && isScalarValue(knownValue(value).value))
                        slotValues[instr.operand] = knownValue(value).value;
                    else
                        slotValues.erase(instr.operand);

                    emittedAt = static_cast<int>(foldedCode.size());
                    foldedCode.push_back(instr);

                    break;
                }

                case OpCode::STORE_VAR: {
                    StackEntry value = popEntry(stack);
                    const std::string& name = std::get<std::string>(chunk.constants[instr.operand]);

                    if (isKnown(value) && isScalarValue(knownValue(value).value))
                        nameValues[name] = knownValue(value).value;
                    else
                        nameValues.erase(name);

                    emittedAt = static_cast<int>(foldedCode.size());
                    foldedCode.push_back(instr);

                    break;
                }

                case OpCode::DUP_STORE_SLOT: {
                    if (isKnown(stack.back()) && isScalarValue(knownValue(stack.back()).value))
                        slotValues[instr.operand] = knownValue(stack.back()).value;
                    else
                        slotValues.erase(instr.operand);

                    if (auto* known = std::get_if<TrackedValue>(&stack.back()))
                        known->removable = false;

                    emittedAt = static_cast<int>(foldedCode.size());
                    foldedCode.push_back(instr);

                    break;
                }

                case OpCode::DUP_STORE: {
                    const std::string& name = std::get<std::string>(chunk.constants[instr.operand]);

                    if (isKnown(stack.back()) && isScalarValue(knownValue(stack.back()).value))
                        nameValues[name] = knownValue(stack.back()).value;
                    else
                        nameValues.erase(name);

                    if (auto* known = std::get_if<TrackedValue>(&stack.back()))
                        known->removable = false;

                    emittedAt = static_cast<int>(foldedCode.size());
                    foldedCode.push_back(instr);

                    break;
                }

                case OpCode::POP: {
                    if (!targets.contains(oldIdx) && stack.size() > 1 &&
                        isKnown(stack.back()) && knownValue(stack.back()).removable) {
                        dropped[knownValue(stack.back()).producer] = true;
                        stack.pop_back();
                        stats.removed++;
                    } else {
                        popEntry(stack);
                        emittedAt = static_cast<int>(foldedCode.size());
                        foldedCode.push_back(instr);
                    }

                    break;
                }

                case OpCode::DUP: {
                    if (i + 1 < code.size() && isStoreOp(code[i + 1].op) &&
                        !targets.contains(oldIdx) && !targets.contains(oldIdx + 1)) {
                        if (auto* known = std::get_if<TrackedValue>(&stack.back()))
                            known->removable = false;

                        const bool scalar = isKnown(stack.back()) &&
                            isScalarValue(knownValue(stack.back()).value);

                        if (code[i + 1].op == OpCode::STORE_SLOT) {
                            if (scalar)
                                slotValues[code[i + 1].operand] = knownValue(stack.back()).value;
                            else
                                slotValues.erase(code[i + 1].operand);
                        } else {
                            const auto& name =
                                std::get<std::string>(chunk.constants[code[i + 1].operand]);

                            if (scalar)
                                nameValues[name] = knownValue(stack.back()).value;
                            else
                                nameValues.erase(name);
                        }

                        emittedAt = static_cast<int>(foldedCode.size());
                        foldedCode.push_back({
                            code[i + 1].op == OpCode::STORE_SLOT ? OpCode::DUP_STORE_SLOT : OpCode::DUP_STORE,
                            code[i + 1].operand,
                            instr.loc
                        });
                        stats.folded++;
                        skipNext = true;
                        break;
                    }

                    if (auto* known = std::get_if<TrackedValue>(&stack.back()))
                        known->removable = false;

                    stack.push_back(stack.back());
                    emittedAt = static_cast<int>(foldedCode.size());
                    foldedCode.push_back(instr);

                    break;
                }

                case OpCode::ADD:
                case OpCode::SUB:
                case OpCode::MUL:
                case OpCode::DIV:
                case OpCode::MOD:
                case OpCode::POW: {
                    StackEntry right = popEntry(stack);
                    StackEntry left = popEntry(stack);

                    if (isKnown(left) && isKnown(right) &&
                        knownValue(left).removable && knownValue(right).removable) {
                        dropped[knownValue(left).producer] = true;
                        dropped[knownValue(right).producer] = true;

                        DiagnosticEngine foldDiag;
                        ValueNode::ValueType result = VM::applyArithmetic(
                            knownValue(left).value, knownValue(right).value, arithmeticOpName(instr.op), foldDiag);

                        if (!foldDiag.hasErrors()) {
                            emitPush(chunk, foldedCode, result, instr.loc);
                            emittedAt = static_cast<int>(foldedCode.size()) - 1;
                            stack.emplace_back(TrackedValue{ result, emittedAt, !targets.contains(oldIdx) });
                            stats.folded++;
                        } else {
                            emittedAt = static_cast<int>(foldedCode.size());
                            foldedCode.push_back(instr);
                            stack.emplace_back(std::monostate{});
                        }
                    } else if (identityEligible(instr.op, left, right)) {
                        // x op identity -> x: drop the identity operand, keep the other side
                        dropped[knownValue(right).producer] = true;
                        stack.emplace_back(TrackedValue{
                            knownValue(left).value, knownValue(left).producer, knownValue(left).removable
                        });
                        stats.folded++;
                    } else if ((instr.op == OpCode::ADD || instr.op == OpCode::MUL) &&
                               identityEligible(instr.op, right, left)) {
                        dropped[knownValue(left).producer] = true;
                        stack.emplace_back(TrackedValue{
                            knownValue(right).value, knownValue(right).producer, knownValue(right).removable
                        });
                        stats.folded++;
                    } else {
                        emittedAt = static_cast<int>(foldedCode.size());
                        foldedCode.push_back(instr);
                        stack.emplace_back(std::monostate{});
                    }

                    break;
                }

                case OpCode::CMP_EQ:
                case OpCode::CMP_NE:
                case OpCode::CMP_GT:
                case OpCode::CMP_LT:
                case OpCode::CMP_GE:
                case OpCode::CMP_LE: {
                    StackEntry right = popEntry(stack);
                    StackEntry left = popEntry(stack);

                    if (isKnown(left) && isKnown(right) &&
                        knownValue(left).removable && knownValue(right).removable) {
                        dropped[knownValue(left).producer] = true;
                        dropped[knownValue(right).producer] = true;

                        DiagnosticEngine foldDiag;
                        bool result = VM::applyComparison(
                            knownValue(left).value, knownValue(right).value, comparisonOpName(instr.op), foldDiag);

                        if (!foldDiag.hasErrors()) {
                            emitPush(chunk, foldedCode, result, instr.loc);
                            emittedAt = static_cast<int>(foldedCode.size()) - 1;
                            stack.emplace_back(TrackedValue{ result, emittedAt, !targets.contains(oldIdx) });
                            stats.folded++;
                        } else {
                            emittedAt = static_cast<int>(foldedCode.size());
                            foldedCode.push_back(instr);
                            stack.emplace_back(std::monostate{});
                        }
                    } else {
                        emittedAt = static_cast<int>(foldedCode.size());
                        foldedCode.push_back(instr);
                        stack.emplace_back(std::monostate{});
                    }

                    break;
                }

                case OpCode::LOGIC_AND:
                case OpCode::LOGIC_OR: {
                    StackEntry right = popEntry(stack);
                    StackEntry left = popEntry(stack);

                    if (isKnown(left) && isKnown(right) &&
                        knownValue(left).removable && knownValue(right).removable) {
                        dropped[knownValue(left).producer] = true;
                        dropped[knownValue(right).producer] = true;

                        bool l = VM::valueToBool(knownValue(left).value);
                        bool r = VM::valueToBool(knownValue(right).value);
                        bool result = (instr.op == OpCode::LOGIC_AND) ? (l && r) : (l || r);
                        emitPush(chunk, foldedCode, result, instr.loc);
                        emittedAt = static_cast<int>(foldedCode.size()) - 1;
                        stack.emplace_back(TrackedValue{ result, emittedAt, !targets.contains(oldIdx) });
                        stats.folded++;
                    } else {
                        emittedAt = static_cast<int>(foldedCode.size());
                        foldedCode.push_back(instr);
                        stack.emplace_back(std::monostate{});
                    }
                    break;
                }

                case OpCode::MAKE_ARRAY: {
                    int count = instr.operand;
                    bool foldable = count == 0 || static_cast<int>(stack.size()) > count;
                    std::vector<int> producers;
                    std::vector<ValueNode::ValueType> elements;

                    if (foldable && count > 0) {
                        elements.resize(count);
                        producers.resize(count);

                        for (int i = count - 1; i >= 0; --i) {
                            StackEntry entry = popEntry(stack);
                            if (!isKnown(entry) || !knownValue(entry).removable) {
                                foldable = false;
                                break;
                            }

                            elements[i] = knownValue(entry).value;
                            producers[i] = knownValue(entry).producer;
                        }
                    }

                    if (foldable) {
                        for (int producer : producers)
                            dropped[producer] = true;

                        auto arr = std::make_shared<ArrayValue>();
                        arr->elements = std::move(elements);

                        emitPush(chunk, foldedCode, arr, instr.loc);
                        emittedAt = static_cast<int>(foldedCode.size()) - 1;
                        stack.emplace_back(TrackedValue{ arr, emittedAt, !targets.contains(oldIdx) });
                        stats.folded++;
                    } else {
                        emittedAt = static_cast<int>(foldedCode.size());
                        foldedCode.push_back(instr);
                        stack.assign(1, std::monostate{});
                    }

                    break;
                }

                case OpCode::LOAD_INDEX: {
                    StackEntry indexEntry = popEntry(stack);
                    StackEntry targetEntry = popEntry(stack);

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

                    if (foldable) {
                        dropped[knownValue(indexEntry).producer] = true;
                        dropped[knownValue(targetEntry).producer] = true;

                        const auto& element =
                            std::get<ArrayRef>(knownValue(targetEntry).value)->elements[index];
                        emitPush(chunk, foldedCode, element, instr.loc);
                        emittedAt = static_cast<int>(foldedCode.size()) - 1;
                        stack.emplace_back(TrackedValue{ element, emittedAt, !targets.contains(oldIdx) });
                        stats.folded++;
                    } else {
                        emittedAt = static_cast<int>(foldedCode.size());
                        foldedCode.push_back(instr);
                        stack.emplace_back(std::monostate{});
                    }

                    break;
                }

                case OpCode::STORE_INDEX: {
                    popEntry(stack);
                    popEntry(stack);
                    popEntry(stack);

                    emittedAt = static_cast<int>(foldedCode.size());
                    foldedCode.push_back(instr);
                    break;
                }

                case OpCode::NEG:
                case OpCode::NOT: {
                    StackEntry operand = popEntry(stack);

                    if (isKnown(operand) && knownValue(operand).removable) {
                        dropped[knownValue(operand).producer] = true;

                        if (instr.op == OpCode::NEG) {
                            DiagnosticEngine foldDiag;
                            ValueNode::ValueType result = VM::applyUnary(knownValue(operand).value, "-", foldDiag);

                            if (!foldDiag.hasErrors()) {
                                emitPush(chunk, foldedCode, result, instr.loc);
                                emittedAt = static_cast<int>(foldedCode.size()) - 1;
                                stack.emplace_back(TrackedValue{ result, emittedAt, !targets.contains(oldIdx) });
                            } else {
                                emittedAt = static_cast<int>(foldedCode.size());
                                foldedCode.push_back(instr);
                                stack.emplace_back(std::monostate{});
                            }
                        } else {
                            bool result = !VM::valueToBool(knownValue(operand).value);
                            emitPush(chunk, foldedCode, result, instr.loc);
                            emittedAt = static_cast<int>(foldedCode.size()) - 1;
                            stack.emplace_back(TrackedValue{ result, emittedAt, !targets.contains(oldIdx) });
                        }
                        stats.folded++;
                    } else {
                        emittedAt = static_cast<int>(foldedCode.size());
                        foldedCode.push_back(instr);
                        stack.emplace_back(std::monostate{});
                    }
                    break;
                }

                case OpCode::JMP_IF_FALSE:
                case OpCode::JMP_IF_TRUE: {
                    StackEntry cond = popEntry(stack);
                    bool folded = false;

                    if (i + 1 < code.size() && code[i + 1].op == OpCode::JMP &&
                        !targets.contains(oldIdx) && !targets.contains(oldIdx + 1) &&
                        oldIdx + 1 + instr.operand == oldIdx + 2 + code[i + 1].operand) {
                        if (isKnown(cond) && knownValue(cond).removable) {
                            dropped[knownValue(cond).producer] = true;
                            stats.removed++;
                        } else {
                            emittedAt = static_cast<int>(foldedCode.size());
                            foldedCode.push_back({ OpCode::POP, 0, instr.loc });
                        }

                        stats.folded++;
                        stack.assign(1, std::monostate{});
                        break;
                    }

                    if (!targets.contains(oldIdx) && isKnown(cond) && knownValue(cond).removable) {
                        dropped[knownValue(cond).producer] = true;

                        bool value = VM::valueToBool(knownValue(cond).value);
                        bool alwaysJump = (instr.op == OpCode::JMP_IF_FALSE) ? !value : value;

                        if (alwaysJump) {
                            emittedAt = static_cast<int>(foldedCode.size());
                            foldedCode.push_back({ OpCode::JMP, instr.operand, instr.loc });
                        } else {
                            stats.removed++;
                        }
                        
                        stats.folded++;
                        folded = true;
                    } else if (isKnown(cond) && !knownValue(cond).removable && isScalarValue(knownValue(cond).value)) {
                        int producer = knownValue(cond).producer;
                        bool producerIsScalarPush = producer >= 0 && producer < static_cast<int>(foldedCode.size()) &&
                            (foldedCode[producer].op == OpCode::PUSH_INT ||
                             foldedCode[producer].op == OpCode::PUSH_FLOAT ||
                             foldedCode[producer].op == OpCode::PUSH_STR ||
                             foldedCode[producer].op == OpCode::PUSH_BOOL ||
                             foldedCode[producer].op == OpCode::PUSH_NONE);

                        bool backwardOnlyTarget = false;
                        if (producerIsScalarPush && producer < static_cast<int>(newToOld.size())) {
                            int producerOld = newToOld[producer];
                            auto it = jumpSources.find(producerOld);
                            if (it != jumpSources.end()) {
                                backwardOnlyTarget = std::ranges::all_of(it->second, [producerOld](int source) {
                                    return source > producerOld;
                                });
                            }
                        }

                        bool value = VM::valueToBool(knownValue(cond).value);
                        bool alwaysJump = (instr.op == OpCode::JMP_IF_FALSE) ? !value : value;

                        if (backwardOnlyTarget && alwaysJump) {
                            dropped[producer] = true;

                            emittedAt = static_cast<int>(foldedCode.size());
                            foldedCode.push_back({ OpCode::JMP, instr.operand, instr.loc });

                            stats.folded++;
                            folded = true;
                        }
                    } else if (i > 0 && code[i - 1].op == OpCode::NOT &&
                               !targets.contains(oldIdx) && !targets.contains(oldIdx - 1) &&
                               !foldedCode.empty() && foldedCode.back().op == OpCode::NOT) {
                        // Fuse a preceding untargeted NOT into the branch: invert the
                        // condition instead of computing its negation at runtime.
                        dropped[static_cast<int>(foldedCode.size()) - 1] = true;

                        OpCode fused = (instr.op == OpCode::JMP_IF_FALSE)
                            ? OpCode::JMP_IF_TRUE : OpCode::JMP_IF_FALSE;

                        emittedAt = static_cast<int>(foldedCode.size());
                        foldedCode.push_back({ fused, instr.operand, instr.loc });
                        stats.folded++;
                    } else {
                        emittedAt = static_cast<int>(foldedCode.size());
                        foldedCode.push_back(instr);
                    }

                    if (!folded && emittedAt < 0) {
                        emittedAt = static_cast<int>(foldedCode.size());
                        foldedCode.push_back(instr);
                    }

                    stack.assign(1, std::monostate{});
                    break;
                }

                case OpCode::CALL: {
                    const FuncMeta* meta = instr.operand >= 0 &&
                        instr.operand < static_cast<int>(chunk.functions.size())
                        ? &chunk.functions[instr.operand]
                        : nullptr;

                    bool foldable = meta != nullptr &&
                        meta->argCount <= static_cast<int>(stack.size()) - 1;

                    std::vector<StackEntry> popped;
                    if (foldable) {
                        popped.reserve(meta->argCount);
                        for (int k = 0; k < meta->argCount && foldable; ++k) {
                            StackEntry entry = popEntry(stack);
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

                    if (foldable) {
                        for (const auto& entry : popped)
                            dropped[knownValue(entry).producer] = true;

                        emitPush(chunk, foldedCode, result, instr.loc);
                        emittedAt = static_cast<int>(foldedCode.size()) - 1;
                        stack.emplace_back(TrackedValue{
                            result, emittedAt, !targets.contains(oldIdx)
                        });
                        stats.folded++;
                    } else {
                        emittedAt = static_cast<int>(foldedCode.size());
                        foldedCode.push_back(instr);
                        stack.assign(1, std::monostate{});
                    }

                    break;
                }

                case OpCode::JMP:
                case OpCode::RETURN:
                case OpCode::HALT:
                default:
                    emittedAt = static_cast<int>(foldedCode.size());
                    foldedCode.push_back(instr);
                    stack.assign(1, std::monostate{});
                    break;
            }

            if (emittedAt >= 0) {
                oldToNew[oldIdx] = emittedAt;
                newToOld.push_back(oldIdx);
            }

            if (skipNext)
                ++i;
        }

        {
            std::vector<Instruction> compactCode;
            std::vector<int> compactNewToOld;
            std::vector<int> compactOldToNew(code.size(), -1);

            for (size_t j = 0; j < foldedCode.size(); ++j) {
                if (dropped[j])
                    continue;

                compactOldToNew[newToOld[j]] = static_cast<int>(compactCode.size());
                compactCode.push_back(foldedCode[j]);
                compactNewToOld.push_back(newToOld[j]);
            }

            foldedCode = std::move(compactCode);
            newToOld = std::move(compactNewToOld);
            oldToNew = std::move(compactOldToNew);
        }

        auto foldedTargetOf = [&](int j) -> int {
            if (j < 0 || j >= static_cast<int>(newToOld.size()))
                return -1;

            int oldTarget = newToOld[j] + 1 + foldedCode[j].operand;
            if (oldTarget < 0 || oldTarget >= static_cast<int>(oldToNew.size()))
                return -1;

            return oldToNew[oldTarget];
        };

        auto resolveTarget = [&](int j) -> int {
            int target = foldedTargetOf(j);
            int guard = 0;

            while (target >= 0 && target < static_cast<int>(foldedCode.size()) &&
                   foldedCode[target].op == OpCode::JMP) {
                if (++guard > static_cast<int>(foldedCode.size()))
                    break;

                int next = foldedTargetOf(target);
                if (next == target || next < 0)
                    break;

                target = next;
            }

            return target;
        };

        std::vector<bool> reachable(foldedCode.size(), false);
        std::vector<int> queue;
        reachable[0] = true;
        queue.push_back(0);

        while (!queue.empty()) {
            int j = queue.back();
            queue.pop_back();

            auto mark = [&](int target) {
                if (target >= 0 && target < static_cast<int>(foldedCode.size()) && !reachable[target]) {
                    reachable[target] = true;
                    queue.push_back(target);
                }
            };

            if (isJump(foldedCode[j].op))
                mark(resolveTarget(j));

            if (!isTerminator(foldedCode[j].op))
                mark(j + 1);
        }

        std::vector<Instruction> finalCode;
        std::vector<int> finalToFolded;
        std::vector<int> foldedToFinal(foldedCode.size(), -1);

        for (size_t j = 0; j < foldedCode.size(); ++j) {
            if (!reachable[j]) {
                stats.removed++;
                continue;
            }

            if (isJump(foldedCode[j].op) && resolveTarget(static_cast<int>(j)) == static_cast<int>(j) + 1) {
                stats.removed++;
                continue;
            }

            foldedToFinal[j] = static_cast<int>(finalCode.size());
            finalToFolded.push_back(static_cast<int>(j));
            finalCode.push_back(foldedCode[j]);
        }

        for (size_t f = 0; f < finalCode.size(); ++f) {
            auto& instr = finalCode[f];
            if (!isJump(instr.op))
                continue;

            int foldedIdx = finalToFolded[f];
            int target = resolveTarget(foldedIdx);
            int finalTarget = (target >= 0 && target < static_cast<int>(foldedToFinal.size()))
                ? foldedToFinal[target] : static_cast<int>(f) + 1;

            instr.operand = finalTarget - static_cast<int>(f) - 1;
        }

        chunk.code = std::move(finalCode);
        return stats;
    }
}
