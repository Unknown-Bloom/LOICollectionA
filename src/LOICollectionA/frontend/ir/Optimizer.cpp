#include <cmath>
#include <limits>
#include <vector>
#include <variant>
#include <unordered_map>
#include <unordered_set>

#include "LOICollectionA/utils/core/MathUtils.h"

#include "LOICollectionA/frontend/ir/VM.h"

#include "LOICollectionA/frontend/ir/Optimizer.h"

namespace LOICollection::frontend::ir {
    struct TrackedValue {
        ValueNode::ValueType value;
        int producer = -1; 
        bool removable = false;
    };

    using StackEntry = std::variant<TrackedValue, std::monostate>;

    bool isJump(OpCode op) {
        return op == OpCode::JMP || op == OpCode::JMP_IF_FALSE || op == OpCode::JMP_IF_TRUE;
    }

    bool isTerminator(OpCode op) {
        return op == OpCode::JMP || op == OpCode::RETURN || op == OpCode::HALT;
    }

    bool isKnown(const StackEntry& entry) {
        return std::holds_alternative<TrackedValue>(entry);
    }

    bool isScalarValue(const ValueNode::ValueType& value) {
        return !std::holds_alternative<ArrayRef>(value) &&
            !std::holds_alternative<ObjectRef>(value) &&
            !std::holds_alternative<FunctionRefPtr>(value);
    }

    const TrackedValue& knownValue(const StackEntry& entry) {
        return std::get<TrackedValue>(entry);
    }

    StackEntry popEntry(std::vector<StackEntry>& stack) {
        if (stack.size() > 1) {
            StackEntry top = std::move(stack.back());
            stack.pop_back();
            return top;
        }

        return std::monostate{};
    }

    std::string arithmeticOpName(OpCode op) {
        switch (op) {
            case OpCode::ADD: return "+";
            case OpCode::SUB: return "-";
            case OpCode::MUL: return "*";
            case OpCode::DIV: return "/";
            case OpCode::MOD: return "%";
            case OpCode::POW: return "^";
            default: return "";
        }
    }

    std::string comparisonOpName(OpCode op) {
        switch (op) {
            case OpCode::CMP_EQ: return "==";
            case OpCode::CMP_NE: return "!=";
            case OpCode::CMP_GT: return ">";
            case OpCode::CMP_LT: return "<";
            case OpCode::CMP_GE: return ">=";
            case OpCode::CMP_LE: return "<=";
            default: return "";
        }
    }

    // Opcodes that may execute script code (function bodies, lambdas, constructors) or
    // create object fields; any of these can rebind what a later LOAD_VAR resolves to.
    bool canWriteVariables(OpCode op) {
        switch (op) {
            case OpCode::CALL:
            case OpCode::CALL_MACRO:
            case OpCode::CALL_METHOD:
            case OpCode::CALL_METHOD_VIRTUAL:
            case OpCode::CALL_FUNC:
            case OpCode::CALL_LAMBDA:
            case OpCode::CALL_SUPER_CTOR:
            case OpCode::NEW:
            case OpCode::STORE_FIELD:
                return true;
            default:
                return false;
        }
    }

    // `kept op identity == kept`, applicable when the kept operand is a known number of a
    // compatible kind. Divided by type-promotion rules: `x / 1` and `x ^ 1` always produce
    // floats, and float `x + 0.0` flips -0.0, so those combinations stay untouched.
    bool identityEligible(OpCode op, const StackEntry& kept, const StackEntry& identity) {
        if (!isKnown(kept) || !isKnown(identity) || !knownValue(identity).removable)
            return false;

        const auto& keptValue = knownValue(kept).value;
        const auto& identityValue = knownValue(identity).value;

        bool keptIsInt = std::holds_alternative<int>(keptValue);
        bool keptIsFloat = std::holds_alternative<float>(keptValue);
        bool identityIntZero = std::holds_alternative<int>(identityValue) && std::get<int>(identityValue) == 0;
        bool identityIntOne = std::holds_alternative<int>(identityValue) && std::get<int>(identityValue) == 1;
        bool identityFloatOne = std::holds_alternative<float>(identityValue) && std::get<float>(identityValue) == 1.0f;

        switch (op) {
            case OpCode::ADD: // x + 0 / 0 + x
                return keptIsInt && identityIntZero;
            case OpCode::SUB: // x - 0
                return keptIsInt && identityIntZero;
            case OpCode::MUL: // x * 1 / 1 * x
                return (keptIsInt && identityIntOne) || (keptIsFloat && (identityIntOne || identityFloatOne));
            case OpCode::DIV: // x / 1
                return keptIsFloat && (identityIntOne || identityFloatOne);
            case OpCode::POW: // x ^ 1
                return keptIsFloat && (identityIntOne || identityFloatOne);
            default:
                return false;
        }
    }

    // ValueType holds non-comparable alternatives, so scalar equality is checked per kind.
    // Floats compare bitwise-sign aware: `+0.0 == -0.0` under IEEE754, but they are
    // observably different values (`to_string` and `1/x` disagree), so pool dedup must
    // never conflate them.
    bool sameScalar(const ValueNode::ValueType& a, const ValueNode::ValueType& b) {
        if (a.index() != b.index() || !isScalarValue(a))
            return false;

        switch (a.index()) {
            case 0: return std::get<int>(a) == std::get<int>(b);
            case 1: {
                const float fa = std::get<float>(a);
                const float fb = std::get<float>(b);
                return fa == fb && std::signbit(fa) == std::signbit(fb);
            }
            case 2: return std::get<std::string>(a) == std::get<std::string>(b);
            case 3: return std::get<bool>(a) == std::get<bool>(b);
            default: return true;
        }
    }

    bool foldPureMath(
        const std::string& name,
        const std::vector<ValueNode::ValueType>& args,
        ValueNode::ValueType& out
    ) {
        auto isInt = [](const ValueNode::ValueType& v) { return std::holds_alternative<int>(v); };
        auto isFloat = [](const ValueNode::ValueType& v) { return std::holds_alternative<float>(v); };

        if (name == "math::abs" && args.size() == 1) {
            if (isInt(args[0])) {
                int v = std::get<int>(args[0]);
                if (v == std::numeric_limits<int>::min())
                    return false;

                out = std::abs(v);
                return true;
            }

            if (isFloat(args[0])) {
                out = std::abs(std::get<float>(args[0]));
                return true;
            }

            return false;
        }

        if ((name == "math::min" || name == "math::max") && args.size() == 2) {
            if (isInt(args[0]) && isInt(args[1])) {
                out = name == "math::min"
                    ? std::min(std::get<int>(args[0]), std::get<int>(args[1]))
                    : std::max(std::get<int>(args[0]), std::get<int>(args[1]));
                return true;
            }

            if (isFloat(args[0]) && isFloat(args[1])) {
                out = name == "math::min"
                    ? std::min(std::get<float>(args[0]), std::get<float>(args[1]))
                    : std::max(std::get<float>(args[0]), std::get<float>(args[1]));
                return true;
            }

            return false;
        }

        if ((name == "math::sqrt" || name == "math::log" || name == "math::sin" ||
             name == "math::cos") && args.size() == 1) {
            if (!isInt(args[0]) && !isFloat(args[0]))
                return false;

            double v = isInt(args[0]) ? std::get<int>(args[0]) : std::get<float>(args[0]);
            out = static_cast<float>(
                name == "math::sqrt" ? std::sqrt(v) :
                name == "math::log" ? std::log(v) :
                name == "math::sin" ? std::sin(v) : std::cos(v)
            );
            return true;
        }

        if (name == "math::pow" && args.size() == 2) {
            if (isInt(args[0]) && isInt(args[1])) {
                out = static_cast<float>(
                    MathUtils::pow(std::get<int>(args[0]), std::get<int>(args[1]))
                );
                return true;
            }

            if (isFloat(args[0]) && isFloat(args[1])) {
                out = static_cast<float>(std::pow(std::get<float>(args[0]), std::get<float>(args[1])));
                return true;
            }

            return false;
        }

        return false;
    }

    int addConstant(BytecodeChunk& chunk, const ValueNode::ValueType& value) {
        if (isScalarValue(value)) {
            for (size_t i = 0; i < chunk.constants.size(); ++i) {
                if (sameScalar(chunk.constants[i], value))
                    return static_cast<int>(i);
            }
        }

        chunk.constants.push_back(value);
        return static_cast<int>(chunk.constants.size() - 1);
    }

    void emitPush(
        BytecodeChunk& chunk, std::vector<Instruction>& out, const ValueNode::ValueType& value,
        const SourceLocation& loc = {}
    ) {
        OpCode op = OpCode::PUSH_INT;

        switch (value.index()) {
            case 0: op = OpCode::PUSH_INT; break;
            case 1: op = OpCode::PUSH_FLOAT; break;
            case 2: op = OpCode::PUSH_STR; break;
            case 3: op = OpCode::PUSH_BOOL; break;
            case 7: op = OpCode::PUSH_NONE; break;
            default: op = OpCode::PUSH_INT; break;
        }

        out.push_back({ op, addConstant(chunk, value), loc });
    }

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

        // Straight-line scalar constants per variable name (the pool may hold the same
        // name at several indices, so keying by the string is the only stable identity).
        // Entries are dropped at merge points and around ops that can execute script
        // code, so a forwarded LOAD_VAR always reproduces the stored value.
        std::unordered_map<std::string, ValueNode::ValueType> varSlots;

        for (size_t i = 0; i < code.size(); ++i) {
            const auto& instr = code[i];
            const int oldIdx = static_cast<int>(i);
            int emittedAt = -1;
            bool skipNext = false;

            if (targets.contains(oldIdx)) {
                stack.assign(1, std::monostate{});
                varSlots.clear();
            } else if (canWriteVariables(instr.op)) {
                varSlots.clear();
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

                case OpCode::LOAD_VAR: {
                    const std::string& name = std::get<std::string>(chunk.constants[instr.operand]);
                    auto slot = varSlots.find(name);

                    if (slot != varSlots.end()) {
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

                case OpCode::STORE_VAR: {
                    StackEntry value = popEntry(stack);
                    const std::string& name = std::get<std::string>(chunk.constants[instr.operand]);

                    if (isKnown(value) && isScalarValue(knownValue(value).value))
                        varSlots[name] = knownValue(value).value;
                    else
                        varSlots.erase(name);

                    emittedAt = static_cast<int>(foldedCode.size());
                    foldedCode.push_back(instr);

                    break;
                }

                case OpCode::DUP_STORE: {
                    const std::string& name = std::get<std::string>(chunk.constants[instr.operand]);

                    if (isKnown(stack.back()) && isScalarValue(knownValue(stack.back()).value))
                        varSlots[name] = knownValue(stack.back()).value;
                    else
                        varSlots.erase(name);

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
                    if (i + 1 < code.size() && code[i + 1].op == OpCode::STORE_VAR &&
                        !targets.contains(oldIdx) && !targets.contains(oldIdx + 1)) {
                        if (auto* known = std::get_if<TrackedValue>(&stack.back()))
                            known->removable = false;

                        const auto& name =
                            std::get<std::string>(chunk.constants[code[i + 1].operand]);
                        if (isKnown(stack.back()) && isScalarValue(knownValue(stack.back()).value))
                            varSlots[name] = knownValue(stack.back()).value;
                        else
                            varSlots.erase(name);

                        emittedAt = static_cast<int>(foldedCode.size());
                        foldedCode.push_back({ OpCode::DUP_STORE, code[i + 1].operand, instr.loc });
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
