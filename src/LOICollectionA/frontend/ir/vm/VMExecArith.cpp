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

#include "LOICollectionA/utils/core/MathUtils.h"

#include "LOICollectionA/frontend/ir/VM.h"

namespace LOICollection::frontend::ir {

    static std::string_view opToken(Op op) {
        switch (op) {
            case Op::ADD: return "+";
            case Op::SUB: return "-";
            case Op::MUL: return "*";
            case Op::DIV: return "/";
            case Op::MOD: return "%";
            case Op::POW: return "^";
            case Op::CMP_EQ: return "==";
            case Op::CMP_NE: return "!=";
            case Op::CMP_GT: return ">";
            case Op::CMP_LT: return "<";
            case Op::CMP_GE: return ">=";
            case Op::CMP_LE: return "<=";
            case Op::NEG: return "-";
            case Op::NOT: return "!";
            default: return "?";
        }
    }

    static std::optional<Op> arithmeticOp(std::string_view op) {
        if (op == "+") return Op::ADD;
        if (op == "-") return Op::SUB;
        if (op == "*") return Op::MUL;
        if (op == "/") return Op::DIV;
        if (op == "%") return Op::MOD;
        if (op == "^") return Op::POW;
        return std::nullopt;
    }

    static std::optional<Op> comparisonOp(std::string_view op) {
        if (op == "==") return Op::CMP_EQ;
        if (op == "!=") return Op::CMP_NE;
        if (op == ">") return Op::CMP_GT;
        if (op == "<") return Op::CMP_LT;
        if (op == ">=") return Op::CMP_GE;
        if (op == "<=") return Op::CMP_LE;
        return std::nullopt;
    }

    static std::string valueClassOf(const ValueNode::ValueType& val) {
        if (std::holds_alternative<std::string>(val))
            return "String";
        if (std::holds_alternative<ArrayRef>(val))
            return "Array";

        return {};
    }

    static std::optional<ll::Expected<TypedValue>> registeredValue(
        const ValueNode::ValueType& left, const ValueNode::ValueType& right,
        std::string_view token, DiagnosticEngine& diagnostics, const SourceLocation& loc
    ) {
        const std::string cls = valueClassOf(left);
        if (cls.empty() || !ClassCall::getInstance().hasOperator(cls, std::string(token)))
            return std::nullopt;

        return ClassCall::getInstance().callOperator(cls, std::string(token), left, right, diagnostics, loc);
    }

    static std::optional<TypedValue> registeredArith(
        const ValueNode::ValueType& left, const ValueNode::ValueType& right,
        std::string_view token, DiagnosticEngine& diagnostics, const SourceLocation& loc
    ) {
        if (auto result = registeredValue(left, right, token, diagnostics, loc); result.has_value()) {
            if (result->has_value())
                return result->value();

            diagnostics.addError(loc,
                "Operator '" + std::string(token) + "' failed for class '" + valueClassOf(left) +
                "': " + result->error().message());
            return 0;
        }

        return std::nullopt;
    }

    ValueNode::ValueType VM::applyArithmetic(const ValueNode::ValueType& left, const ValueNode::ValueType& right, Op op, DiagnosticEngine& diagnostics, const SourceLocation& loc) {
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

        if (auto result = registeredArith(left, right, token, diagnostics, loc); result)
            return *result;

        return std::visit([&token, &diagnostics, &loc, op](auto&& l, auto&& r) -> ValueNode::ValueType {
            using T = std::decay_t<decltype(l)>;
            using U = std::decay_t<decltype(r)>;

            if constexpr (std::is_same_v<T, EnumValue> || std::is_same_v<U, EnumValue>) {
                diagnostics.addError(loc, "Cannot perform arithmetic on an enum value");
                return 0;
            } else if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<U, std::monostate>) {
                diagnostics.addError(loc,
                    "Cannot perform arithmetic on an empty optional value");
                return 0;
            } else if constexpr (std::is_arithmetic_v<T> && std::is_arithmetic_v<U>) {
                auto dl = static_cast<double>(l);
                auto dr = static_cast<double>(r);

                switch (op) {
                    case Op::ADD:
                        if constexpr (std::is_integral_v<T> && std::is_integral_v<U>) {
                            long long result = static_cast<long long>(l) + static_cast<long long>(r);
                            if (result < std::numeric_limits<int>::min() || result > std::numeric_limits<int>::max()) {
                                diagnostics.addError(loc, "Integer overflow in addition");
                                return 0;
                            }
                            return static_cast<int>(result);
                        }
                        return static_cast<float>(dl + dr);
                    case Op::SUB:
                        if constexpr (std::is_integral_v<T> && std::is_integral_v<U>) {
                            long long result = static_cast<long long>(l) - static_cast<long long>(r);
                            if (result < std::numeric_limits<int>::min() || result > std::numeric_limits<int>::max()) {
                                diagnostics.addError(loc, "Integer overflow in subtraction");
                                return 0;
                            }
                            return static_cast<int>(result);
                        }
                        return static_cast<float>(dl - dr);
                    case Op::MUL:
                        if constexpr (std::is_integral_v<T> && std::is_integral_v<U>) {
                            long long result = static_cast<long long>(l) * static_cast<long long>(r);
                            if (result < std::numeric_limits<int>::min() || result > std::numeric_limits<int>::max()) {
                                diagnostics.addError(loc, "Integer overflow in multiplication");
                                return 0;
                            }
                            return static_cast<int>(result);
                        }
                        return static_cast<float>(dl * dr);
                    case Op::DIV: return static_cast<float>(dl / dr);
                    case Op::POW: return static_cast<float>(MathUtils::pow(dl, dr));
                    case Op::MOD:
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
                if (op == Op::ADD) return VM::valueToString(l) + VM::valueToString(r);

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

    ValueNode::ValueType VM::applyUnary(const ValueNode::ValueType& operand, Op op, DiagnosticEngine& diagnostics, const SourceLocation& loc) {
        return std::visit([&diagnostics, &loc, op](auto&& arg) -> ValueNode::ValueType {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_arithmetic_v<T>) {
                switch (op) {
                    case Op::NEG:
                        if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
                            if (arg == std::numeric_limits<int>::min()) {
                                diagnostics.addError(loc, "Integer overflow in unary negation");
                                return 0;
                            }
                        }
                        return -arg;
                    case Op::NOT:
                        return !VM::valueToBool(arg);
                    default: break;
                }
            }

            if (op == Op::NOT) return !VM::valueToBool(arg);

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

        if (op == "-") return applyUnary(operand, Op::NEG, diagnostics, loc);
        if (op == "!") return applyUnary(operand, Op::NOT, diagnostics, loc);

        diagnostics.addError(loc, "Unknown unary op: " + op);
        return 0;
    }

    bool VM::applyComparison(const ValueNode::ValueType& left, const ValueNode::ValueType& right, Op op, DiagnosticEngine& diagnostics, const SourceLocation& loc) {
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

        if (auto result = registeredArith(left, right, token, diagnostics, loc); result)
            return VM::valueToBool(*result);

        return std::visit([&token, &diagnostics, &loc, op](auto&& l, auto&& r) -> bool {
            using T = std::decay_t<decltype(l)>;
            using U = std::decay_t<decltype(r)>;

            if constexpr (std::is_same_v<T, EnumValue> || std::is_same_v<U, EnumValue>) {
                if constexpr (std::is_same_v<T, EnumValue> && std::is_same_v<U, EnumValue>) {
                    const bool same = (l == r);

                    if (op == Op::CMP_EQ) return same;
                    if (op == Op::CMP_NE) return !same;
                }

                diagnostics.addError(loc, "Cannot compare enum values");
                return false;
            } else if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<U, std::monostate>) {
                if constexpr (std::is_same_v<T, std::monostate> && std::is_same_v<U, std::monostate>) {
                    if (op == Op::CMP_EQ) return true;
                    if (op == Op::CMP_NE) return false;
                }

                diagnostics.addError(loc, "Cannot compare an empty optional value");
                return false;
            } else if constexpr (std::is_arithmetic_v<T> && std::is_arithmetic_v<U>) {
                auto cmp = static_cast<double>(l) <=> static_cast<double>(r);

                switch (op) {
                    case Op::CMP_EQ: return cmp == 0;
                    case Op::CMP_NE: return cmp != 0;
                    case Op::CMP_GT: return cmp > 0;
                    case Op::CMP_LT: return cmp < 0;
                    case Op::CMP_GE: return cmp >= 0;
                    case Op::CMP_LE: return cmp <= 0;
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
                    case Op::CMP_EQ: return l == r;
                    case Op::CMP_NE: return l != r;
                    default: break;
                }

                diagnostics.addError(loc, "Unknown comparison op: " + std::string(token));
                return false;
            } else if constexpr (std::is_same_v<T, U>) {
                auto cmp = l <=> r;

                switch (op) {
                    case Op::CMP_EQ: return cmp == 0;
                    case Op::CMP_NE: return cmp != 0;
                    case Op::CMP_GT: return cmp > 0;
                    case Op::CMP_LT: return cmp < 0;
                    case Op::CMP_GE: return cmp >= 0;
                    case Op::CMP_LE: return cmp <= 0;
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
        Frame& frame = s.frame;

        const ValueNode::ValueType& lv = this->regOf(frame, instr.src1);
        const ValueNode::ValueType& rv = this->regOf(frame, instr.src2);

        const auto* li = std::get_if<int>(&lv);
        const auto* ri = std::get_if<int>(&rv);

        const bool integralFastPath =
            instr.op == Op::ADD ||
            instr.op == Op::SUB ||
            instr.op == Op::MUL ||
            instr.op == Op::MOD;

        if (li && ri && integralFastPath) {
            const int l = *li;
            const int r = *ri;

            if (instr.op == Op::MOD) {
                if (r == 0) {
                    this->diagnostics.addError(this->currentLoc, "Modulo by zero");
                    this->setReg(frame, instr.dst, 0);
                    return;
                }
                this->setReg(frame, instr.dst, l % r);
                return;
            }

            long long res = 0;
            switch (instr.op) {
                case Op::ADD: res = static_cast<long long>(l) + r; break;
                case Op::SUB: res = static_cast<long long>(l) - r; break;
                case Op::MUL: res = static_cast<long long>(l) * r; break;
                default: break;
            }

            if (res < std::numeric_limits<int>::min() || res > std::numeric_limits<int>::max()) {
                this->diagnostics.addError(this->currentLoc,
                    instr.op == Op::ADD ? "Integer overflow in addition"
                    : instr.op == Op::SUB ? "Integer overflow in subtraction"
                    : "Integer overflow in multiplication");
                this->setReg(frame, instr.dst, 0);
                return;
            }

            this->setReg(frame, instr.dst, static_cast<int>(res));
            return;
        }

        auto result = VM::applyArithmetic(lv, rv, instr.op, this->diagnostics, this->currentLoc);
        if (auto* text = std::get_if<std::string>(&result)) {
            if (const auto violation = this->mBudget->accountString(text->size());
                violation != sandbox::SandboxBudget::Violation::None) {
                this->failBudget(violation, "String size budget exhausted");
                return;
            }
        }

        this->setReg(frame, instr.dst, std::move(result));
    }

    void VM::execComparison(ExecArgs& s) {
        const auto& instr = s.instr;
        Frame& frame = s.frame;

        const ValueNode::ValueType& lv = this->regOf(frame, instr.src1);
        const ValueNode::ValueType& rv = this->regOf(frame, instr.src2);

        const auto* li = std::get_if<int>(&lv);
        const auto* ri = std::get_if<int>(&rv);

        bool fast = false;
        if (li && ri) {
            const int l = *li;
            const int r = *ri;

            switch (instr.op) {
                case Op::CMP_EQ: fast = l == r; break;
                case Op::CMP_NE: fast = l != r; break;
                case Op::CMP_GT: fast = l > r; break;
                case Op::CMP_LT: fast = l < r; break;
                case Op::CMP_GE: fast = l >= r; break;
                case Op::CMP_LE: fast = l <= r; break;
                default: break;
            }

            this->setReg(frame, instr.dst, fast);
            return;
        }

        this->setReg(frame, instr.dst,
            VM::applyComparison(lv, rv, instr.op, this->diagnostics, this->currentLoc));
    }

    void VM::execLogic(ExecArgs& s) {
        const auto& instr = s.instr;
        Frame& frame = s.frame;

        switch (instr.op) {
            case Op::LOGIC_AND: {
                this->setReg(frame, instr.dst,
                    VM::valueToBool(this->regOf(frame, instr.src1)) &&
                    VM::valueToBool(this->regOf(frame, instr.src2)));
            } break;
            case Op::LOGIC_OR: {
                this->setReg(frame, instr.dst,
                    VM::valueToBool(this->regOf(frame, instr.src1)) ||
                    VM::valueToBool(this->regOf(frame, instr.src2)));
            } break;
            case Op::NEG: {
                const ValueNode::ValueType& v = this->regOf(frame, instr.src1);

                if (auto* iv = std::get_if<int>(&v)) {
                    if (*iv == std::numeric_limits<int>::min()) {
                        this->diagnostics.addError(this->currentLoc, "Integer overflow in unary negation");
                        this->setReg(frame, instr.dst, 0);
                        break;
                    }

                    this->setReg(frame, instr.dst, -*iv);
                    break;
                }

                this->setReg(frame, instr.dst,
                    VM::applyUnary(v, Op::NEG, this->diagnostics, this->currentLoc));
            } break;
            case Op::NOT: {
                this->setReg(frame, instr.dst, !VM::valueToBool(this->regOf(frame, instr.src1)));
            } break;
            default: break;
        }
    }

}
