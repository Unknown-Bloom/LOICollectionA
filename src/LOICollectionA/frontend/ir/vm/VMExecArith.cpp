#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <ll/api/Expected.h>

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/Unicode.h"

#include "LOICollectionA/frontend/ir/OpCode.h"

#include "LOICollectionA/utils/core/MathUtils.h"

#include "LOICollectionA/frontend/ir/VM.h"

namespace LOICollection::frontend::ir {

    static OpCode genericOp(OpCode op) {
        switch (op) {
            case OpCode::ADD_I: return OpCode::ADD;
            case OpCode::SUB_I: return OpCode::SUB;
            case OpCode::MUL_I: return OpCode::MUL;
            case OpCode::MOD_I: return OpCode::MOD;
            case OpCode::CMP_EQ_I: return OpCode::CMP_EQ;
            case OpCode::CMP_NE_I: return OpCode::CMP_NE;
            case OpCode::CMP_GT_I: return OpCode::CMP_GT;
            case OpCode::CMP_LT_I: return OpCode::CMP_LT;
            case OpCode::CMP_GE_I: return OpCode::CMP_GE;
            case OpCode::CMP_LE_I: return OpCode::CMP_LE;
            case OpCode::NEG_I: return OpCode::NEG;
            default: return op;
        }
    }

    static std::string_view opToken(OpCode op) {
        op = genericOp(op);
        switch (op) {
            case OpCode::ADD: return "+";
            case OpCode::SUB: return "-";
            case OpCode::MUL: return "*";
            case OpCode::DIV: return "/";
            case OpCode::MOD: return "%";
            case OpCode::POW: return "^";
            case OpCode::CMP_EQ: return "==";
            case OpCode::CMP_NE: return "!=";
            case OpCode::CMP_GT: return ">";
            case OpCode::CMP_LT: return "<";
            case OpCode::CMP_GE: return ">=";
            case OpCode::CMP_LE: return "<=";
            case OpCode::NEG: return "-";
            case OpCode::NOT: return "!";
            default: return "?";
        }
    }

    std::optional<int> topInt(std::vector<ValueNode::ValueType>& stack, size_t back) {
        if (stack.size() <= back)
            return std::nullopt;
        if (auto p = std::get_if<int>(&stack[stack.size() - 1 - back]))
            return *p;
        return std::nullopt;
    }

    static std::optional<OpCode> arithmeticOp(std::string_view op) {
        if (op == "+") return OpCode::ADD;
        if (op == "-") return OpCode::SUB;
        if (op == "*") return OpCode::MUL;
        if (op == "/") return OpCode::DIV;
        if (op == "%") return OpCode::MOD;
        if (op == "^") return OpCode::POW;
        return std::nullopt;
    }

    static std::optional<OpCode> comparisonOp(std::string_view op) {
        if (op == "==") return OpCode::CMP_EQ;
        if (op == "!=") return OpCode::CMP_NE;
        if (op == ">") return OpCode::CMP_GT;
        if (op == "<") return OpCode::CMP_LT;
        if (op == ">=") return OpCode::CMP_GE;
        if (op == "<=") return OpCode::CMP_LE;
        return std::nullopt;
    }

    ValueNode::ValueType VM::applyArithmetic(const ValueNode::ValueType& left, const ValueNode::ValueType& right, OpCode op, DiagnosticEngine& diagnostics, const SourceLocation& loc) {
        op = genericOp(op);
        const std::string_view token = opToken(op);

        if (auto leftObj = std::get_if<ObjectRef>(&left)) {
            if (ClassCall::getInstance().hasOperator((*leftObj)->className, std::string(token))) {
                auto result = ClassCall::getInstance().callOperator(
                    (*leftObj)->className, std::string(token), left, right, diagnostics, loc
                );

                if (!result.has_value()) {
                    diagnostics.addError(loc,
                        "Operator '" + std::string(token) + "' failed for class '" + (*leftObj)->className +
                        "': " + result.error().message());
                    return 0;
                }

                return result.value();
            }
        }

        if (auto rightObj = std::get_if<ObjectRef>(&right)) {
            if (ClassCall::getInstance().hasOperator((*rightObj)->className, std::string(token))) {
                auto result = ClassCall::getInstance().callOperator(
                    (*rightObj)->className, std::string(token), left, right, diagnostics, loc
                );

                if (!result.has_value()) {
                    diagnostics.addError(loc,
                        "Operator '" + std::string(token) + "' failed for class '" + (*rightObj)->className +
                        "': " + result.error().message());
                    return 0;
                }

                return result.value();
            }
        }

        return std::visit([&token, &diagnostics, &loc, op](auto&& l, auto&& r) -> ValueNode::ValueType {
            using T = std::decay_t<decltype(l)>;
            using U = std::decay_t<decltype(r)>;

            if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<U, std::monostate>) {
                diagnostics.addError(loc,
                    "Cannot perform arithmetic on an empty optional value");
                return 0;
            } else if constexpr (std::is_arithmetic_v<T> && std::is_arithmetic_v<U>) {
                auto dl = static_cast<double>(l);
                auto dr = static_cast<double>(r);

                switch (op) {
                    case OpCode::ADD:
                        if constexpr (std::is_integral_v<T> && std::is_integral_v<U>) {
                            long long result = static_cast<long long>(l) + static_cast<long long>(r);
                            if (result < std::numeric_limits<int>::min() || result > std::numeric_limits<int>::max()) {
                                diagnostics.addError(loc, "Integer overflow in addition");
                                return 0;
                            }
                            return static_cast<int>(result);
                        }
                        return static_cast<float>(dl + dr);
                    case OpCode::SUB:
                        if constexpr (std::is_integral_v<T> && std::is_integral_v<U>) {
                            long long result = static_cast<long long>(l) - static_cast<long long>(r);
                            if (result < std::numeric_limits<int>::min() || result > std::numeric_limits<int>::max()) {
                                diagnostics.addError(loc, "Integer overflow in subtraction");
                                return 0;
                            }
                            return static_cast<int>(result);
                        }
                        return static_cast<float>(dl - dr);
                    case OpCode::MUL:
                        if constexpr (std::is_integral_v<T> && std::is_integral_v<U>) {
                            long long result = static_cast<long long>(l) * static_cast<long long>(r);
                            if (result < std::numeric_limits<int>::min() || result > std::numeric_limits<int>::max()) {
                                diagnostics.addError(loc, "Integer overflow in multiplication");
                                return 0;
                            }
                            return static_cast<int>(result);
                        }
                        return static_cast<float>(dl * dr);
                    case OpCode::DIV: return static_cast<float>(dl / dr);
                    case OpCode::POW: return static_cast<float>(MathUtils::pow(dl, dr));
                    case OpCode::MOD:
                        if constexpr (std::is_integral_v<T> && std::is_integral_v<U>) {
                            auto divisor = static_cast<long long>(r);
                            if (divisor == 0) {
                                diagnostics.addError(loc, "Modulo by zero");
                                return 0;
                            }
                            return static_cast<int>(static_cast<long long>(l) % divisor);
                        }

                        diagnostics.addError(loc, "Modulo requires integral types");
                        return 0;
                    default:
                        diagnostics.addError(loc, "Unknown arithmetic op: " + std::string(token));
                        return 0;
                }
            } else {
                if (op == OpCode::ADD) return VM::valueToString(l) + VM::valueToString(r);

                diagnostics.addError(loc, "Type mismatch in arithmetic");
                return 0;
            }
        }, left, right);
    }

    ValueNode::ValueType VM::applyArithmetic(const ValueNode::ValueType& left, const ValueNode::ValueType& right, const std::string& op, DiagnosticEngine& diagnostics, const SourceLocation& loc) {
        if (auto oc = arithmeticOp(op))
            return applyArithmetic(left, right, *oc, diagnostics, loc);

        diagnostics.addError(loc, "Unknown arithmetic op: " + op);
        return 0;
    }

    ValueNode::ValueType VM::applyUnary(const ValueNode::ValueType& operand, OpCode op, DiagnosticEngine& diagnostics, const SourceLocation& loc) {
        op = genericOp(op);
        return std::visit([&diagnostics, &loc, op](auto&& arg) -> ValueNode::ValueType {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_arithmetic_v<T>) {
                switch (op) {
                    case OpCode::NEG:
                        if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
                            if (arg == std::numeric_limits<int>::min()) {
                                diagnostics.addError(loc, "Integer overflow in unary negation");
                                return 0;
                            }
                        }
                        return -arg;
                    case OpCode::NOT:
                        return !VM::valueToBool(arg);
                    default: break;
                }
            }

            if (op == OpCode::NOT) return !VM::valueToBool(arg);

            diagnostics.addError(loc, "Unknown unary op: " + std::string(opToken(op)));
            return 0;
        }, operand);
    }

    ValueNode::ValueType VM::applyUnary(const ValueNode::ValueType& operand, const std::string& op, DiagnosticEngine& diagnostics, const SourceLocation& loc) {
        if (op == "+")
            return std::visit([&diagnostics, &loc](auto&& arg) -> ValueNode::ValueType {
                if constexpr (std::is_arithmetic_v<std::decay_t<decltype(arg)>>)
                    return ValueNode::ValueType(arg);

                diagnostics.addError(loc, "Unknown unary op: +");
                return 0;
            }, operand);

        if (op == "-") return applyUnary(operand, OpCode::NEG, diagnostics, loc);
        if (op == "!") return applyUnary(operand, OpCode::NOT, diagnostics, loc);

        diagnostics.addError(loc, "Unknown unary op: " + op);
        return 0;
    }

    bool VM::applyComparison(const ValueNode::ValueType& left, const ValueNode::ValueType& right, OpCode op, DiagnosticEngine& diagnostics, const SourceLocation& loc) {
        op = genericOp(op);
        const std::string_view token = opToken(op);

        if (auto leftObj = std::get_if<ObjectRef>(&left)) {
            if (ClassCall::getInstance().hasOperator((*leftObj)->className, std::string(token))) {
                auto result = ClassCall::getInstance().callOperator(
                    (*leftObj)->className, std::string(token), left, right, diagnostics, loc
                );

                if (!result.has_value()) {
                    diagnostics.addError(loc,
                        "Operator '" + std::string(token) + "' failed for class '" + (*leftObj)->className +
                        "': " + result.error().message());
                    return false;
                }

                return VM::valueToBool(result.value());
            }
        }

        if (auto rightObj = std::get_if<ObjectRef>(&right)) {
            if (ClassCall::getInstance().hasOperator((*rightObj)->className, std::string(token))) {
                auto result = ClassCall::getInstance().callOperator(
                    (*rightObj)->className, std::string(token), left, right, diagnostics, loc
                );

                if (!result.has_value()) {
                    diagnostics.addError(loc,
                        "Operator '" + std::string(token) + "' failed for class '" + (*rightObj)->className +
                        "': " + result.error().message());
                    return false;
                }

                return VM::valueToBool(result.value());
            }
        }

        return std::visit([&token, &diagnostics, &loc, op](auto&& l, auto&& r) -> bool {
            using T = std::decay_t<decltype(l)>;
            using U = std::decay_t<decltype(r)>;

            if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<U, std::monostate>) {
                if constexpr (std::is_same_v<T, std::monostate> && std::is_same_v<U, std::monostate>) {
                    if (op == OpCode::CMP_EQ) return true;
                    if (op == OpCode::CMP_NE) return false;
                }

                diagnostics.addError(loc, "Cannot compare an empty optional value");
                return false;
            } else if constexpr (std::is_arithmetic_v<T> && std::is_arithmetic_v<U>) {
                auto cmp = static_cast<double>(l) <=> static_cast<double>(r);

                switch (op) {
                    case OpCode::CMP_EQ: return cmp == 0;
                    case OpCode::CMP_NE: return cmp != 0;
                    case OpCode::CMP_GT: return cmp > 0;
                    case OpCode::CMP_LT: return cmp < 0;
                    case OpCode::CMP_GE: return cmp >= 0;
                    case OpCode::CMP_LE: return cmp <= 0;
                    default: break;
                }

                diagnostics.addError(loc, "Unknown comparison op: " + std::string(token));
                return false;
            } else if constexpr (
                (std::is_same_v<T, ObjectRef> && std::is_same_v<U, ObjectRef>) ||
                (std::is_same_v<T, FunctionRefPtr> && std::is_same_v<U, FunctionRefPtr>) ||
                (std::is_same_v<T, ArrayRef> && std::is_same_v<U, ArrayRef>)
            ) {
                switch (op) {
                    case OpCode::CMP_EQ: return l == r;
                    case OpCode::CMP_NE: return l != r;
                    default: break;
                }

                diagnostics.addError(loc, "Unknown comparison op: " + std::string(token));
                return false;
            } else if constexpr (std::is_same_v<T, U>) {
                auto cmp = l <=> r;

                switch (op) {
                    case OpCode::CMP_EQ: return cmp == 0;
                    case OpCode::CMP_NE: return cmp != 0;
                    case OpCode::CMP_GT: return cmp > 0;
                    case OpCode::CMP_LT: return cmp < 0;
                    case OpCode::CMP_GE: return cmp >= 0;
                    case OpCode::CMP_LE: return cmp <= 0;
                    default: break;
                }

                diagnostics.addError(loc, "Unknown comparison op: " + std::string(token));
                return false;
            } else {
                diagnostics.addError(loc, "Type mismatch in comparison");
                return false;
            }
        }, left, right);
    }

    bool VM::applyComparison(const ValueNode::ValueType& left, const ValueNode::ValueType& right, const std::string& op, DiagnosticEngine& diagnostics, const SourceLocation& loc) {
        if (auto oc = comparisonOp(op))
            return applyComparison(left, right, *oc, diagnostics, loc);

        diagnostics.addError(loc, "Unknown comparison op: " + op);
        return false;
    }

    void VM::execArithmetic(ExecArgs& s) {
        const auto& instr = s.instr;
        switch (instr.op) {
            case OpCode::ADD_I: case OpCode::SUB_I: case OpCode::MUL_I: case OpCode::MOD_I: {
                if (this->stack.size() < 2 || !std::holds_alternative<int>(this->stack.back()) ||
                    !std::holds_alternative<int>(this->stack[this->stack.size() - 2])) {
                    auto rr = this->pop();
                    auto ll = this->pop();
                    this->push(VM::applyArithmetic(ll, rr, genericOp(instr.op), this->diagnostics, this->currentLoc));
                    break;
                }

                const int r = std::get<int>(this->stack.back());
                this->stack.pop_back();
                const int l = std::get<int>(this->stack.back());
                this->stack.pop_back();

                if (instr.op == OpCode::MOD_I) {
                    if (r == 0) {
                        this->diagnostics.addError(this->currentLoc, "Modulo by zero");
                        this->push(0);
                        break;
                    }
                    this->push(l % r);
                    break;
                }

                long long res = 0;
                switch (instr.op) {
                    case OpCode::ADD_I: res = static_cast<long long>(l) + r; break;
                    case OpCode::SUB_I: res = static_cast<long long>(l) - r; break;
                    case OpCode::MUL_I: res = static_cast<long long>(l) * r; break;
                    default: break;
                }

                if (res < std::numeric_limits<int>::min() || res > std::numeric_limits<int>::max()) {
                    this->diagnostics.addError(this->currentLoc,
                        instr.op == OpCode::ADD_I ? "Integer overflow in addition"
                        : instr.op == OpCode::SUB_I ? "Integer overflow in subtraction"
                        : "Integer overflow in multiplication");
                    this->push(0);
                    break;
                }

                this->push(static_cast<int>(res));
            } break;
            case OpCode::ADD: case OpCode::SUB: case OpCode::MUL: case OpCode::MOD: {
                auto r = topInt(this->stack, 0);
                auto l = topInt(this->stack, 1);
                if (l && r) {
                    this->stack.pop_back();
                    this->stack.pop_back();
                    long long res = 0;
                    switch (instr.op) {
                        case OpCode::ADD: res = static_cast<long long>(*l) + *r; break;
                        case OpCode::SUB: res = static_cast<long long>(*l) - *r; break;
                        case OpCode::MUL: res = static_cast<long long>(*l) * *r; break;
                        case OpCode::MOD:
                            if (*r == 0) {
                                this->diagnostics.addError(this->currentLoc, "Modulo by zero");
                                this->push(0);
                                break;
                            }
                            res = static_cast<long long>(*l) % *r;
                            break;
                        default: break;
                    }
                    if (instr.op != OpCode::MOD) {
                        if (res < std::numeric_limits<int>::min() || res > std::numeric_limits<int>::max()) {
                            this->diagnostics.addError(this->currentLoc,
                                instr.op == OpCode::ADD ? "Integer overflow in addition"
                                : instr.op == OpCode::SUB ? "Integer overflow in subtraction"
                                : "Integer overflow in multiplication");
                            this->push(0);
                            break;
                        }
                    }
                    this->push(static_cast<int>(res));
                    break;
                }
                auto rr = this->pop();
                auto ll = this->pop();
                auto result = VM::applyArithmetic(ll, rr, instr.op, this->diagnostics, this->currentLoc);
                if (std::holds_alternative<std::string>(result)) {
                    if (const auto violation = this->mBudget->accountString(std::get<std::string>(result).size());
                        violation != sandbox::SandboxBudget::Violation::None) {
                        this->failBudget(violation, "String size budget exhausted");
                        break;
                    }
                }
                this->push(std::move(result));
            } break;
            case OpCode::DIV: case OpCode::POW: {
                auto rr = this->pop();
                auto ll = this->pop();
                this->push(VM::applyArithmetic(ll, rr, instr.op, this->diagnostics, this->currentLoc));
            } break;
            default: break;
        }
    }

    void VM::execComparison(ExecArgs& s) {
        const auto& instr = s.instr;

        switch (instr.op) {
            case OpCode::CMP_EQ_I: case OpCode::CMP_NE_I: case OpCode::CMP_GT_I:
            case OpCode::CMP_LT_I: case OpCode::CMP_GE_I: case OpCode::CMP_LE_I: {
                if (this->stack.size() < 2 || !std::holds_alternative<int>(this->stack.back()) ||
                    !std::holds_alternative<int>(this->stack[this->stack.size() - 2])) {
                    auto rr = this->pop();
                    auto ll = this->pop();
                    this->push(VM::applyComparison(ll, rr, genericOp(instr.op), this->diagnostics, this->currentLoc));
                    return;
                }

                const int r = std::get<int>(this->stack.back());
                this->stack.pop_back();
                const int l = std::get<int>(this->stack.back());
                this->stack.pop_back();

                bool b = false;
                switch (instr.op) {
                    case OpCode::CMP_EQ_I: b = l == r; break;
                    case OpCode::CMP_NE_I: b = l != r; break;
                    case OpCode::CMP_GT_I: b = l > r; break;
                    case OpCode::CMP_LT_I: b = l < r; break;
                    case OpCode::CMP_GE_I: b = l >= r; break;
                    case OpCode::CMP_LE_I: b = l <= r; break;
                    default: break;
                }

                this->push(b);
                return;
            }
            default: break;
        }

        auto r = topInt(this->stack, 0);
        auto l = topInt(this->stack, 1);
        if (l && r) {
            this->stack.pop_back();
            this->stack.pop_back();
            bool b = false;
            switch (instr.op) {
                case OpCode::CMP_EQ: b = *l == *r; break;
                case OpCode::CMP_NE: b = *l != *r; break;
                case OpCode::CMP_GT: b = *l > *r; break;
                case OpCode::CMP_LT: b = *l < *r; break;
                case OpCode::CMP_GE: b = *l >= *r; break;
                case OpCode::CMP_LE: b = *l <= *r; break;
                default:
                    this->stack.push_back(*l);
                    this->stack.push_back(*r);
                    this->push(VM::applyComparison(ValueNode::ValueType(*l), ValueNode::ValueType(*r), instr.op, this->diagnostics, this->currentLoc));
                    return;
            }
            this->push(b);
            return;
        }
        auto rr = this->pop();
        auto ll = this->pop();
        this->push(VM::applyComparison(ll, rr, instr.op, this->diagnostics, this->currentLoc));
    }

    void VM::execLogic(ExecArgs& s) {
        const auto& instr = s.instr;
        switch (instr.op) {
            case OpCode::LOGIC_AND: {
                auto r = this->pop();
                auto l = this->pop();

                this->push(VM::valueToBool(l) && VM::valueToBool(r));
            } break;
            case OpCode::LOGIC_OR: {
                auto r = this->pop();
                auto l = this->pop();

                this->push(VM::valueToBool(l) || VM::valueToBool(r));
            } break;
            case OpCode::NEG_I: {
                if (this->stack.empty() || !std::holds_alternative<int>(this->stack.back())) {
                    auto v = this->pop();
                    this->push(VM::applyUnary(v, OpCode::NEG, this->diagnostics, this->currentLoc));
                    break;
                }

                const int v = std::get<int>(this->stack.back());
                if (v == std::numeric_limits<int>::min()) {
                    this->diagnostics.addError(this->currentLoc, "Integer overflow in unary negation");
                    this->stack.back() = 0;
                    break;
                }
                this->stack.back() = -v;
            } break;
            case OpCode::NEG: {
                if (!this->stack.empty() && std::holds_alternative<int>(this->stack.back())) {
                    const int v = std::get<int>(this->stack.back());
                    if (v == std::numeric_limits<int>::min()) {
                        this->diagnostics.addError(this->currentLoc, "Integer overflow in unary negation");
                        this->stack.back() = 0;
                        break;
                    }
                    this->stack.back() = -v;
                    break;
                }
                auto v = this->pop();
                this->push(VM::applyUnary(v, instr.op, this->diagnostics, this->currentLoc));
            } break;
            case OpCode::NOT: {
                auto v = this->pop();

                this->push(!VM::valueToBool(v));
            } break;
            default: break;
        }
    }

}
