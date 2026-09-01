#include <cmath>
#include <cctype>
#include <chrono>
#include <limits>
#include <ranges>
#include <string>
#include <vector>
#include <algorithm>

#include <ll/api/Expected.h>

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/Unicode.h"

#include "LOICollectionA/utils/core/MathUtils.h"

#include "LOICollectionA/frontend/ir/VM.h"

namespace LOICollection::frontend::ir {

    ValueNode::ValueType VM::applyArithmetic(const ValueNode::ValueType& left, const ValueNode::ValueType& right, const std::string& op, DiagnosticEngine& diagnostics, const SourceLocation& loc) {
        if (auto leftObj = std::get_if<ObjectRef>(&left)) {
            if (ClassCall::getInstance().hasOperator((*leftObj)->className, op)) {
                auto result = ClassCall::getInstance().callOperator(
                    (*leftObj)->className, op, left, right, diagnostics, loc
                );

                if (!result.has_value()) {
                    diagnostics.addError(loc,
                        "Operator '" + op + "' failed for class '" + (*leftObj)->className +
                        "': " + result.error().message());
                    return 0;
                }

                return result.value();
            }
        }

        

        if (auto rightObj = std::get_if<ObjectRef>(&right)) {
            if (ClassCall::getInstance().hasOperator((*rightObj)->className, op)) {
                auto result = ClassCall::getInstance().callOperator(
                    (*rightObj)->className, op, left, right, diagnostics, loc
                );

                if (!result.has_value()) {
                    diagnostics.addError(loc,
                        "Operator '" + op + "' failed for class '" + (*rightObj)->className +
                        "': " + result.error().message());
                    return 0;
                }

                return result.value();
            }
        }

        return std::visit([&op, &diagnostics, &loc](auto&& l, auto&& r) -> ValueNode::ValueType {
            using T = std::decay_t<decltype(l)>;
            using U = std::decay_t<decltype(r)>;

            if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<U, std::monostate>) {
                diagnostics.addError(loc,
                    "Cannot perform arithmetic on an empty optional value");
                return 0;
            } else if constexpr (std::is_arithmetic_v<T> && std::is_arithmetic_v<U>) {
                auto dl = static_cast<double>(l);
                auto dr = static_cast<double>(r);

                if (op == "+") {
                    if constexpr (std::is_integral_v<T> && std::is_integral_v<U>) {
                        long long result = static_cast<long long>(l) + static_cast<long long>(r);
                        if (result < std::numeric_limits<int>::min() || result > std::numeric_limits<int>::max()) {
                            diagnostics.addError(loc, "Integer overflow in addition");
                            return 0;
                        }
                        return static_cast<int>(result);
                    }
                    return static_cast<float>(dl + dr);
                }
                if (op == "-") {
                    if constexpr (std::is_integral_v<T> && std::is_integral_v<U>) {
                        long long result = static_cast<long long>(l) - static_cast<long long>(r);
                        if (result < std::numeric_limits<int>::min() || result > std::numeric_limits<int>::max()) {
                            diagnostics.addError(loc, "Integer overflow in subtraction");
                            return 0;
                        }
                        return static_cast<int>(result);
                    }
                    return static_cast<float>(dl - dr);
                }
                if (op == "*") {
                    if constexpr (std::is_integral_v<T> && std::is_integral_v<U>) {
                        long long result = static_cast<long long>(l) * static_cast<long long>(r);
                        if (result < std::numeric_limits<int>::min() || result > std::numeric_limits<int>::max()) {
                            diagnostics.addError(loc, "Integer overflow in multiplication");
                            return 0;
                        }
                        return static_cast<int>(result);
                    }
                    return static_cast<float>(dl * dr);
                }
                if (op == "/") return static_cast<float>(dl / dr);
                if (op == "^") return static_cast<float>(MathUtils::pow(dl, dr));
                if (op == "%") {
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
                }

                diagnostics.addError(loc, "Unknown arithmetic op: " + op);
                return 0;
            } else {
                if (op == "+") return VM::valueToString(l) + VM::valueToString(r);

                diagnostics.addError(loc, "Type mismatch in arithmetic");
                return 0;
            }
        }, left, right);
    }

    ValueNode::ValueType VM::applyUnary(const ValueNode::ValueType& operand, const std::string& op, DiagnosticEngine& diagnostics, const SourceLocation& loc) {
        return std::visit([&op, &diagnostics, &loc](auto&& arg) -> ValueNode::ValueType {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_arithmetic_v<T>) {
                if (op == "+") return arg;
                if (op == "-") {
                    if constexpr (std::is_integral_v<T>) {
                        if (arg == std::numeric_limits<int>::min()) {
                            diagnostics.addError(loc, "Integer overflow in unary negation");
                            return 0;
                        }
                    }
                    return -arg;
                }
            }
            if (op == "!") return !VM::valueToBool(arg);

            diagnostics.addError(loc, "Unknown unary op: " + op);
            return 0;
        }, operand);
    }

    bool VM::applyComparison(const ValueNode::ValueType& left, const ValueNode::ValueType& right, const std::string& op, DiagnosticEngine& diagnostics, const SourceLocation& loc) {
        if (auto leftObj = std::get_if<ObjectRef>(&left)) {
            if (ClassCall::getInstance().hasOperator((*leftObj)->className, op)) {
                auto result = ClassCall::getInstance().callOperator(
                    (*leftObj)->className, op, left, right, diagnostics, loc
                );

                if (!result.has_value()) {
                    diagnostics.addError(loc,
                        "Operator '" + op + "' failed for class '" + (*leftObj)->className +
                        "': " + result.error().message());
                    return false;
                }

                return VM::valueToBool(result.value());
            }
        }

        

        if (auto rightObj = std::get_if<ObjectRef>(&right)) {
            if (ClassCall::getInstance().hasOperator((*rightObj)->className, op)) {
                auto result = ClassCall::getInstance().callOperator(
                    (*rightObj)->className, op, left, right, diagnostics, loc
                );

                if (!result.has_value()) {
                    diagnostics.addError(loc,
                        "Operator '" + op + "' failed for class '" + (*rightObj)->className +
                        "': " + result.error().message());
                    return false;
                }

                return VM::valueToBool(result.value());
            }
        }

        return std::visit([&op, &diagnostics, &loc](auto&& l, auto&& r) -> bool {
            using T = std::decay_t<decltype(l)>;
            using U = std::decay_t<decltype(r)>;

            if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<U, std::monostate>) {
                if constexpr (std::is_same_v<T, std::monostate> && std::is_same_v<U, std::monostate>) {
                    if (op == "==") return true;
                    if (op == "!=") return false;
                }

                diagnostics.addError(loc, "Cannot compare an empty optional value");
                return false;
            } else if constexpr (std::is_arithmetic_v<T> && std::is_arithmetic_v<U>) {
                auto cmp = static_cast<double>(l) <=> static_cast<double>(r);

                if (op == "==") return cmp == 0;
                if (op == "!=") return cmp != 0;
                if (op == ">") return cmp > 0;
                if (op == "<") return cmp < 0;
                if (op == ">=") return cmp >= 0;
                if (op == "<=") return cmp <= 0;

                diagnostics.addError(loc, "Unknown comparison op: " + op);
                return false;
            } else if constexpr (
                (std::is_same_v<T, ObjectRef> && std::is_same_v<U, ObjectRef>) ||
                (std::is_same_v<T, FunctionRefPtr> && std::is_same_v<U, FunctionRefPtr>) ||
                (std::is_same_v<T, ArrayRef> && std::is_same_v<U, ArrayRef>)
            ) {
                if (op == "==") return l == r;
                if (op == "!=") return l != r;

                diagnostics.addError(loc, "Unknown comparison op: " + op);
                return false;
            } else if constexpr (std::is_same_v<T, U>) {
                auto cmp = l <=> r;

                if (op == "==") return cmp == 0;
                if (op == "!=") return cmp != 0;
                if (op == ">") return cmp > 0;
                if (op == "<") return cmp < 0;
                if (op == ">=") return cmp >= 0;
                if (op == "<=") return cmp <= 0;

                diagnostics.addError(loc, "Unknown comparison op: " + op);
                return false;
            } else {
                diagnostics.addError(loc, "Type mismatch in comparison");
                return false;
            }
        }, left, right);
    }

    void VM::execArithmetic(ExecArgs& s) {
        const auto& instr = s.instr;
        switch (instr.op) {
            case OpCode::ADD: {
                    auto r = this->pop();
                    auto l = this->pop();

                    auto result = VM::applyArithmetic(l, r, "+", this->diagnostics, this->currentLoc);

                    if (std::holds_alternative<std::string>(result)) {
                        if (const auto violation = this->mBudget->accountString(std::get<std::string>(result).size());
                            violation != sandbox::SandboxBudget::Violation::None) {
                            this->failBudget(violation, "String size budget exhausted");
                            break;
                        }
                    }

                    this->push(std::move(result));
            } break;
            case OpCode::SUB: {
                    auto r = this->pop();
                    auto l = this->pop();

                    this->push(VM::applyArithmetic(l, r, "-", this->diagnostics, this->currentLoc));
            } break;
            case OpCode::MUL: {
                    auto r = this->pop();
                    auto l = this->pop();

                    this->push(VM::applyArithmetic(l, r, "*", this->diagnostics, this->currentLoc));
            } break;
            case OpCode::DIV: {
                    auto r = this->pop();
                    auto l = this->pop();

                    this->push(VM::applyArithmetic(l, r, "/", this->diagnostics, this->currentLoc));
            } break;
            case OpCode::MOD: {
                    auto r = this->pop();
                    auto l = this->pop();

                    this->push(VM::applyArithmetic(l, r, "%", this->diagnostics, this->currentLoc));
            } break;
            case OpCode::POW: {
                    auto r = this->pop();
                    auto l = this->pop();

                    this->push(VM::applyArithmetic(l, r, "^", this->diagnostics, this->currentLoc));
            } break;
            default: break;
        }
    }

    void VM::execComparison(ExecArgs& s) {
        const auto& instr = s.instr;
        switch (instr.op) {
            case OpCode::CMP_EQ: {
                    auto r = this->pop();
                    auto l = this->pop();

                    this->push(VM::applyComparison(l, r, "==", this->diagnostics, this->currentLoc));
            } break;
            case OpCode::CMP_NE: {
                    auto r = this->pop();
                    auto l = this->pop();

                    this->push(VM::applyComparison(l, r, "!=", this->diagnostics, this->currentLoc));
            } break;
            case OpCode::CMP_GT: {
                    auto r = this->pop();
                    auto l = this->pop();

                    this->push(VM::applyComparison(l, r, ">", this->diagnostics, this->currentLoc));
            } break;
            case OpCode::CMP_LT: {
                    auto r = this->pop();
                    auto l = this->pop();

                    this->push(VM::applyComparison(l, r, "<", this->diagnostics, this->currentLoc));
            } break;
            case OpCode::CMP_GE: {
                    auto r = this->pop();
                    auto l = this->pop();

                    this->push(VM::applyComparison(l, r, ">=", this->diagnostics, this->currentLoc));
            } break;
            case OpCode::CMP_LE: {
                    auto r = this->pop();
                    auto l = this->pop();

                    this->push(VM::applyComparison(l, r, "<=", this->diagnostics, this->currentLoc));
            } break;
            default: break;
        }
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
            case OpCode::NEG: {
                    auto v = this->pop();

                    this->push(VM::applyUnary(v, "-", this->diagnostics, this->currentLoc));
            } break;
            case OpCode::NOT: {
                    auto v = this->pop();

                    this->push(!VM::valueToBool(v));
            } break;
            default: break;
        }
    }

}
