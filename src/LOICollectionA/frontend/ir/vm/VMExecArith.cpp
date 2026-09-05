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

    static MirOp genericOp(MirOp op) {
        switch (op) {
            case MirOp::ADD_I: return MirOp::ADD;
            case MirOp::SUB_I: return MirOp::SUB;
            case MirOp::MUL_I: return MirOp::MUL;
            case MirOp::MOD_I: return MirOp::MOD;
            case MirOp::CMP_EQ_I: return MirOp::CMP_EQ;
            case MirOp::CMP_NE_I: return MirOp::CMP_NE;
            case MirOp::CMP_GT_I: return MirOp::CMP_GT;
            case MirOp::CMP_LT_I: return MirOp::CMP_LT;
            case MirOp::CMP_GE_I: return MirOp::CMP_GE;
            case MirOp::CMP_LE_I: return MirOp::CMP_LE;
            case MirOp::NEG_I: return MirOp::NEG;
            default: return op;
        }
    }

    static std::string_view opToken(MirOp op) {
        op = genericOp(op);
        switch (op) {
            case MirOp::ADD: return "+";
            case MirOp::SUB: return "-";
            case MirOp::MUL: return "*";
            case MirOp::DIV: return "/";
            case MirOp::MOD: return "%";
            case MirOp::POW: return "^";
            case MirOp::CMP_EQ: return "==";
            case MirOp::CMP_NE: return "!=";
            case MirOp::CMP_GT: return ">";
            case MirOp::CMP_LT: return "<";
            case MirOp::CMP_GE: return ">=";
            case MirOp::CMP_LE: return "<=";
            case MirOp::NEG: return "-";
            case MirOp::NOT: return "!";
            default: return "?";
        }
    }

    static std::optional<MirOp> arithmeticOp(std::string_view op) {
        if (op == "+") return MirOp::ADD;
        if (op == "-") return MirOp::SUB;
        if (op == "*") return MirOp::MUL;
        if (op == "/") return MirOp::DIV;
        if (op == "%") return MirOp::MOD;
        if (op == "^") return MirOp::POW;
        return std::nullopt;
    }

    static std::optional<MirOp> comparisonOp(std::string_view op) {
        if (op == "==") return MirOp::CMP_EQ;
        if (op == "!=") return MirOp::CMP_NE;
        if (op == ">") return MirOp::CMP_GT;
        if (op == "<") return MirOp::CMP_LT;
        if (op == ">=") return MirOp::CMP_GE;
        if (op == "<=") return MirOp::CMP_LE;
        return std::nullopt;
    }

    ValueNode::ValueType VM::applyArithmetic(const ValueNode::ValueType& left, const ValueNode::ValueType& right, MirOp op, DiagnosticEngine& diagnostics, const SourceLocation& loc) {
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
                    case MirOp::ADD:
                        if constexpr (std::is_integral_v<T> && std::is_integral_v<U>) {
                            long long result = static_cast<long long>(l) + static_cast<long long>(r);
                            if (result < std::numeric_limits<int>::min() || result > std::numeric_limits<int>::max()) {
                                diagnostics.addError(loc, "Integer overflow in addition");
                                return 0;
                            }
                            return static_cast<int>(result);
                        }
                        return static_cast<float>(dl + dr);
                    case MirOp::SUB:
                        if constexpr (std::is_integral_v<T> && std::is_integral_v<U>) {
                            long long result = static_cast<long long>(l) - static_cast<long long>(r);
                            if (result < std::numeric_limits<int>::min() || result > std::numeric_limits<int>::max()) {
                                diagnostics.addError(loc, "Integer overflow in subtraction");
                                return 0;
                            }
                            return static_cast<int>(result);
                        }
                        return static_cast<float>(dl - dr);
                    case MirOp::MUL:
                        if constexpr (std::is_integral_v<T> && std::is_integral_v<U>) {
                            long long result = static_cast<long long>(l) * static_cast<long long>(r);
                            if (result < std::numeric_limits<int>::min() || result > std::numeric_limits<int>::max()) {
                                diagnostics.addError(loc, "Integer overflow in multiplication");
                                return 0;
                            }
                            return static_cast<int>(result);
                        }
                        return static_cast<float>(dl * dr);
                    case MirOp::DIV: return static_cast<float>(dl / dr);
                    case MirOp::POW: return static_cast<float>(MathUtils::pow(dl, dr));
                    case MirOp::MOD:
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
                if (op == MirOp::ADD) return VM::valueToString(l) + VM::valueToString(r);

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

    ValueNode::ValueType VM::applyUnary(const ValueNode::ValueType& operand, MirOp op, DiagnosticEngine& diagnostics, const SourceLocation& loc) {
        op = genericOp(op);
        return std::visit([&diagnostics, &loc, op](auto&& arg) -> ValueNode::ValueType {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_arithmetic_v<T>) {
                switch (op) {
                    case MirOp::NEG:
                        if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
                            if (arg == std::numeric_limits<int>::min()) {
                                diagnostics.addError(loc, "Integer overflow in unary negation");
                                return 0;
                            }
                        }
                        return -arg;
                    case MirOp::NOT:
                        return !VM::valueToBool(arg);
                    default: break;
                }
            }

            if (op == MirOp::NOT) return !VM::valueToBool(arg);

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

        if (op == "-") return applyUnary(operand, MirOp::NEG, diagnostics, loc);
        if (op == "!") return applyUnary(operand, MirOp::NOT, diagnostics, loc);

        diagnostics.addError(loc, "Unknown unary op: " + op);
        return 0;
    }

    bool VM::applyComparison(const ValueNode::ValueType& left, const ValueNode::ValueType& right, MirOp op, DiagnosticEngine& diagnostics, const SourceLocation& loc) {
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
                    if (op == MirOp::CMP_EQ) return true;
                    if (op == MirOp::CMP_NE) return false;
                }

                diagnostics.addError(loc, "Cannot compare an empty optional value");
                return false;
            } else if constexpr (std::is_arithmetic_v<T> && std::is_arithmetic_v<U>) {
                auto cmp = static_cast<double>(l) <=> static_cast<double>(r);

                switch (op) {
                    case MirOp::CMP_EQ: return cmp == 0;
                    case MirOp::CMP_NE: return cmp != 0;
                    case MirOp::CMP_GT: return cmp > 0;
                    case MirOp::CMP_LT: return cmp < 0;
                    case MirOp::CMP_GE: return cmp >= 0;
                    case MirOp::CMP_LE: return cmp <= 0;
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
                    case MirOp::CMP_EQ: return l == r;
                    case MirOp::CMP_NE: return l != r;
                    default: break;
                }

                diagnostics.addError(loc, "Unknown comparison op: " + std::string(token));
                return false;
            } else if constexpr (std::is_same_v<T, U>) {
                auto cmp = l <=> r;

                switch (op) {
                    case MirOp::CMP_EQ: return cmp == 0;
                    case MirOp::CMP_NE: return cmp != 0;
                    case MirOp::CMP_GT: return cmp > 0;
                    case MirOp::CMP_LT: return cmp < 0;
                    case MirOp::CMP_GE: return cmp >= 0;
                    case MirOp::CMP_LE: return cmp <= 0;
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

        if (li && ri) {
            const int l = *li;
            const int r = *ri;

            if (instr.op == MirOp::MOD || instr.op == MirOp::MOD_I) {
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
                case MirOp::ADD: case MirOp::ADD_I: res = static_cast<long long>(l) + r; break;
                case MirOp::SUB: case MirOp::SUB_I: res = static_cast<long long>(l) - r; break;
                case MirOp::MUL: case MirOp::MUL_I: res = static_cast<long long>(l) * r; break;
                default: break;
            }

            if (res < std::numeric_limits<int>::min() || res > std::numeric_limits<int>::max()) {
                this->diagnostics.addError(this->currentLoc,
                    instr.op == MirOp::ADD || instr.op == MirOp::ADD_I ? "Integer overflow in addition"
                    : instr.op == MirOp::SUB || instr.op == MirOp::SUB_I ? "Integer overflow in subtraction"
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
                case MirOp::CMP_EQ: case MirOp::CMP_EQ_I: fast = l == r; break;
                case MirOp::CMP_NE: case MirOp::CMP_NE_I: fast = l != r; break;
                case MirOp::CMP_GT: case MirOp::CMP_GT_I: fast = l > r; break;
                case MirOp::CMP_LT: case MirOp::CMP_LT_I: fast = l < r; break;
                case MirOp::CMP_GE: case MirOp::CMP_GE_I: fast = l >= r; break;
                case MirOp::CMP_LE: case MirOp::CMP_LE_I: fast = l <= r; break;
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
            case MirOp::LOGIC_AND: {
                this->setReg(frame, instr.dst,
                    VM::valueToBool(this->regOf(frame, instr.src1)) &&
                    VM::valueToBool(this->regOf(frame, instr.src2)));
            } break;
            case MirOp::LOGIC_OR: {
                this->setReg(frame, instr.dst,
                    VM::valueToBool(this->regOf(frame, instr.src1)) ||
                    VM::valueToBool(this->regOf(frame, instr.src2)));
            } break;
            case MirOp::NEG: case MirOp::NEG_I: {
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
                    VM::applyUnary(v, MirOp::NEG, this->diagnostics, this->currentLoc));
            } break;
            case MirOp::NOT: {
                this->setReg(frame, instr.dst, !VM::valueToBool(this->regOf(frame, instr.src1)));
            } break;
            default: break;
        }
    }

}
