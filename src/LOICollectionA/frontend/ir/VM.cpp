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

#include "LOICollectionA/utils/core/MathUtils.h"

#include "LOICollectionA/frontend/ir/VM.h"

namespace LOICollection::frontend::ir {
    namespace {
        thread_local std::shared_ptr<sandbox::SandboxBudget> tlsBudget;

        struct BudgetScope {
            std::shared_ptr<sandbox::SandboxBudget> previous;

            explicit BudgetScope(std::shared_ptr<sandbox::SandboxBudget>& budget) : previous(tlsBudget) {
                if (!previous)
                    budget->reset();
                else
                    budget = previous;

                tlsBudget = budget;
            }

            ~BudgetScope() { tlsBudget = std::move(previous); }
        };

        bool splitStaticMemberName(const std::string& name, std::string& className, std::string& fieldName) {
            auto pos = name.find("::");
            if (pos == std::string::npos || pos == 0 || pos + 2 >= name.size())
                return false;

            className = name.substr(0, pos);
            fieldName = name.substr(pos + 2);
            return !className.empty() && !fieldName.empty();
        }

        std::string valueClassNameOf(const ValueNode::ValueType& val) {
            if (std::holds_alternative<ArrayRef>(val))
                return "Array";
            if (std::holds_alternative<std::string>(val))
                return "String";

            return {};
        }
    }

    std::string VM::valueToString(const ValueNode::ValueType& val) {
        return std::visit([](auto&& arg) -> std::string {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<std::remove_cv_t<T>, int>)
                return std::to_string(arg);
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, float>) {
                std::string result = std::to_string(arg);

                result.erase(result.find_last_not_of('0') + 1, std::string::npos);
                if (result.back() == '.')
                    result.pop_back();
                
                return result;
            }
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, std::string>)
                return arg;
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, bool>)
                return arg ? "true" : "false";
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, ObjectRef>)
                return "instance of " + arg->className;
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, FunctionRefPtr>)
                return "function";
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, ArrayRef>) {
                std::string result = "[";
                for (size_t i = 0; i < arg->elements.size(); ++i) {
                    if (i != 0)
                        result += ", ";
                    result += VM::valueToString(arg->elements[i]);
                }
                result += "]";
                return result;
            }
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, std::monostate>)
                return "None";
        }, val);
    }

    std::string VM::typeNameOf(const ValueNode::ValueType& val) {
        return std::visit([](auto&& arg) -> std::string {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<std::remove_cv_t<T>, int>)
                return "int";
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, float>)
                return "float";
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, std::string>)
                return "string";
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, bool>)
                return "bool";
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, ObjectRef>)
                return arg->className;
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, FunctionRefPtr>)
                return "function";
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, ArrayRef>)
                return "array";
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, std::monostate>)
                return "none";
        }, val);
    }

    ValueNode::ValueType VM::cloneValue(const ValueNode::ValueType& val) {
        if (!std::holds_alternative<ArrayRef>(val))
            return val;

        auto& source = std::get<ArrayRef>(val);
        auto copy = std::make_shared<ArrayValue>();
        copy->elements.reserve(source->elements.size());

        for (const auto& element : source->elements)
            copy->elements.push_back(VM::cloneValue(element));

        return copy;
    }

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

    bool VM::valueToBool(const ValueNode::ValueType& val) {
        return std::visit([](auto&& arg) -> bool {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<std::remove_cv_t<T>, int>)
                return arg != 0;
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, float>)
                return std::abs(arg) > std::numeric_limits<float>::epsilon();
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, std::string>) {
                std::string lower;
                for (char c : arg)
                    lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                if (lower == "false") return false;
                if (lower == "true") return true;

                return !arg.empty();
            }
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, bool>)
                return arg;
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, ObjectRef> || std::is_same_v<std::remove_cv_t<T>, FunctionRefPtr>)
                return true;
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, ArrayRef>)
                return !arg->elements.empty();
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, std::monostate>)
                return false;
        }, val);
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

    void VM::push(const ValueNode::ValueType& v) {
        this->stack.push_back(v);
    }

    ValueNode::ValueType VM::pop() {
        if (this->stack.empty()) {
            this->diagnostics.addError(this->currentLoc, "Stack underflow");
            return ValueNode::ValueType{};
        }

        auto v = this->stack.back();
        this->stack.pop_back();

        return v;
    }

    void VM::storeVariable(
        const BytecodeChunk& chunk,
        Frame& frame,
        const std::string& name,
        const ValueNode::ValueType& val
    ) {
        if (frame.hasThis) {
            auto obj = std::get<ObjectRef>(frame.thisObj);

            if (obj->classIndex >= 0) {
                const auto& cls = chunk.classes[obj->classIndex];
                bool isField = std::ranges::find(cls.fieldNames, name) != cls.fieldNames.end();

                if (isField) {
                    obj->assign(name, val);
                    return;
                }
            }

            if (auto* existing = obj->find(name)) {
                *existing = val;
                return;
            }
        }

        std::string className;
        std::string fieldName;
        if (splitStaticMemberName(name, className, fieldName) &&
            std::ranges::none_of(chunk.classes, [&className](const auto& cls) {
                return cls.name == className;
            }) &&
            ClassCall::getInstance().isRegistered(className) &&
            ClassCall::getInstance().hasStaticField(className, fieldName)) {
            ClassCall::getInstance().setStaticField(className, fieldName, val);
            return;
        }

        (*this->globals)[name] = val;
    }

    bool VM::pushFrame(Frame&& frame) {
        if (this->frames.size() >= this->mBudget->maxFrames) {
            this->diagnostics.addError(this->currentLoc, "Call stack depth limit exceeded");
            return false;
        }

        this->frames.push_back(std::move(frame));
        return true;
    }

    bool VM::isDerived(const BytecodeChunk& chunk, int derivedClassIndex, int baseClassIndex) const {
        if (derivedClassIndex == baseClassIndex) return true;
        if (derivedClassIndex < 0 || derivedClassIndex >= static_cast<int>(chunk.classes.size())) return false;

        const auto& cls = chunk.classes[derivedClassIndex];

        return std::ranges::any_of(cls.ancestorIndices, [baseClassIndex](int idx) -> bool {
            return idx == baseClassIndex;
        });
    }

    const FieldLayoutPtr& VM::classLayout(const BytecodeChunk& chunk, int classIndex) {
        auto it = this->mClassLayouts.find(classIndex);
        if (it != this->mClassLayouts.end())
            return it->second;

        const auto& cls = chunk.classes[static_cast<size_t>(classIndex)];
        return this->mClassLayouts
            .emplace(classIndex, std::make_shared<const FieldLayout>(cls.fieldNames))
            .first->second;
    }

    ValueNode::ValueType VM::run(
        const std::shared_ptr<const BytecodeChunk>& chunk,
        const Context& ctx
    ) {
        if (!chunk) {
            this->diagnostics.addError({ 0, 0, 0 }, "Cannot run a null bytecode chunk");
            return ValueNode::ValueType{};
        }

        this->stack.clear();
        this->frames.clear();
        this->globals->clear();
        this->mReport = sandbox::SandboxReport{};

        for (const auto& cls : chunk->classes) {
            for (size_t i = 0; i < cls.staticFieldNames.size(); ++i) {
                (*this->globals)[cls.name + "::" + cls.staticFieldNames[i]] =
                    cls.staticHasDefault[i]
                        ? VM::cloneValue(cls.staticDefaults[i])
                        : ValueNode::ValueType{};
            }
        }

        this->frames.emplace_back(*chunk);

        CallbackTypePlaces placeholders = ctx.params;
        if (!ctx.scriptId.empty())
            placeholders[Context::kScriptIdKey] = ctx.scriptId;

        const auto startedAt = std::chrono::steady_clock::now();
        ValueNode::ValueType result = this->execute(chunk, placeholders);

        this->mReport.executedInstructions = this->mBudget->executedInstructions;
        this->mReport.nativeCallCount = this->mBudget->nativeCallCount;
        this->mReport.objectCount = this->mBudget->objectCount;
        this->mReport.allocatedBytes = this->mBudget->allocatedBytes;
        this->mReport.wallTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt
        );
        this->mReport.hasErrors = this->diagnostics.hasErrors();
        if (this->mReport.hasErrors)
            this->mReport.errorMessage = this->diagnostics.getErrorMessage();

        return result;
    }

    ValueNode::ValueType VM::execute(
        const std::shared_ptr<const BytecodeChunk>& owner,
        const CallbackTypePlaces& placeholders
    ) {
        if (!owner) {
            this->diagnostics.addError({ 0, 0, 0 }, "Cannot execute a null bytecode chunk");
            return ValueNode::ValueType{};
        }

        const BytecodeChunk& chunk = *owner;

        BudgetScope budgetScope(this->mBudget);

        const auto fail = [this](sandbox::SandboxBudget::Violation violation, const std::string& message) {
            this->mReport.violation = violation;
            this->diagnostics.addError(this->currentLoc, message);
        };

        while (true) {
            if (this->diagnostics.hasErrors())
                return std::string("");

            if (const auto violation = this->mBudget->tickInstruction();
                violation != sandbox::SandboxBudget::Violation::None) {
                fail(violation, violation == sandbox::SandboxBudget::Violation::WallTimeLimit
                    ? "Execution timeout"
                    : "Execution budget exhausted");
                return ValueNode::ValueType{};
            }

            Frame& frame = this->frames.back();
            const BytecodeChunk& cur = frame.chunk.get();

            if (frame.ip >= cur.code.size()) {
                this->diagnostics.addError(this->currentLoc, "Invalid instruction pointer");
                return ValueNode::ValueType{};
            }

            const auto& instr = cur.code[frame.ip++];
            this->currentLoc = instr.loc;
            switch (instr.op) {
                case OpCode::PUSH_INT:
                case OpCode::PUSH_FLOAT:
                case OpCode::PUSH_BOOL:
                case OpCode::PUSH_NONE:
                    this->push(VM::cloneValue(cur.constants[instr.operand]));
                    break;
                case OpCode::PUSH_STR: {
                    const auto& value = std::get<std::string>(cur.constants[instr.operand]);
                    if (const auto violation = this->mBudget->accountString(value.size());
                        violation != sandbox::SandboxBudget::Violation::None) {
                        fail(violation, "String size budget exhausted");
                        break;
                    }

                    this->push(value);
                    break;
                }
                case OpCode::POP:
                    this->pop();
                    break;

                case OpCode::DUP: {
                    if (this->stack.empty()) {
                        this->diagnostics.addError(this->currentLoc, "Stack underflow during DUP");
                        break;
                    }

                    this->stack.push_back(this->stack.back());
                    break;
                }

                case OpCode::DUP2: {
                    if (this->stack.size() < 2) {
                        this->diagnostics.addError(this->currentLoc, "Stack underflow during DUP2");
                        break;
                    }

                    auto second = this->stack[this->stack.size() - 2];
                    this->stack.push_back(second);
                    this->stack.push_back(this->stack[this->stack.size() - 2]);
                    break;
                }

                case OpCode::ROT3: {
                    if (this->stack.size() < 3) {
                        this->diagnostics.addError(this->currentLoc, "Stack underflow during ROT3");
                        break;
                    }

                    auto bottom = std::move(this->stack[this->stack.size() - 3]);
                    this->stack.erase(this->stack.end() - 3);
                    this->stack.push_back(std::move(bottom));
                    break;
                }

                case OpCode::SWAP2: {
                    if (this->stack.size() < 4) {
                        this->diagnostics.addError(this->currentLoc, "Stack underflow during SWAP2");
                        break;
                    }

                    std::swap(this->stack[this->stack.size() - 4], this->stack[this->stack.size() - 2]);
                    std::swap(this->stack[this->stack.size() - 3], this->stack[this->stack.size() - 1]);
                    break;
                }

                case OpCode::IS_NONE: {
                    auto value = this->pop();
                    this->push(std::holds_alternative<std::monostate>(value));
                    break;
                }

                case OpCode::UNWRAP: {
                    auto value = this->pop();
                    if (std::holds_alternative<std::monostate>(value)) {
                        this->diagnostics.addError(this->currentLoc, "Optional value is empty");
                        break;
                    }

                    this->push(value);
                    break;
                }

                case OpCode::TYPE_OF: {
                    auto value = this->pop();
                    this->push(VM::typeNameOf(value));
                    break;
                }

                case OpCode::HAS_VALUE: {
                    auto value = this->pop();
                    this->push(!std::holds_alternative<std::monostate>(value));
                    break;
                }

                case OpCode::LOAD_SLOT: {
                    if (instr.operand < 0 || static_cast<size_t>(instr.operand) >= frame.locals.size()) {
                        this->diagnostics.addError(this->currentLoc, "Slot index out of range");
                        break;
                    }

                    this->push(frame.locals[instr.operand]);
                    break;
                }

                case OpCode::STORE_SLOT: {
                    if (instr.operand < 0 || static_cast<size_t>(instr.operand) >= frame.locals.size()) {
                        this->diagnostics.addError(this->currentLoc, "Slot index out of range");
                        break;
                    }

                    frame.locals[instr.operand] = this->pop();
                    break;
                }

                case OpCode::LOAD_VAR: {
                    const auto& name = std::get<std::string>(cur.constants[instr.operand]);

                    if (frame.hasThis) {
                        auto obj = std::get<ObjectRef>(frame.thisObj);

                        if (obj->classIndex >= 0) {
                            const auto& cls = chunk.classes[obj->classIndex];
                            bool isField = std::ranges::find(cls.fieldNames, name) != cls.fieldNames.end();

                            if (isField) {
                                const auto* field = obj->find(name);
                                if (!field) {
                                    this->diagnostics.addError(this->currentLoc, "Object has no field: " + name);
                                    break;
                                }

                                this->push(*field);
                                break;
                            }
                        }

                        if (const auto* existing = obj->find(name)) {
                            this->push(*existing);
                            break;
                        }
                    }

                    std::string className;
                    std::string fieldName;
                    if (splitStaticMemberName(name, className, fieldName) &&
                        std::ranges::none_of(chunk.classes, [&className](const auto& cls) {
                            return cls.name == className;
                        }) &&
                        ClassCall::getInstance().isRegistered(className) &&
                        ClassCall::getInstance().hasStaticField(className, fieldName)) {
                        auto result = ClassCall::getInstance().getStaticField(className, fieldName);
                        if (!result.has_value()) {
                            this->diagnostics.addError(this->currentLoc,
                                "Failed to load native static field '" + name + "': " + result.error().message());
                            break;
                        }

                        this->push(result.value());
                        break;
                    }

                    auto globalIt = this->globals->find(name);
                    if (globalIt == this->globals->end()) {
                        this->diagnostics.addError(this->currentLoc, "Undefined variable: " + name);
                        break;
                    }

                    this->push(globalIt->second);
                    break;
                }
                case OpCode::STORE_VAR: {
                    const auto& name = std::get<std::string>(cur.constants[instr.operand]);

                    auto val = this->pop();

                    this->storeVariable(chunk, frame, name, val);
                    break;
                }

                case OpCode::DUP_STORE_SLOT: {
                    if (this->stack.empty()) {
                        this->diagnostics.addError(this->currentLoc, "Stack underflow during DUP_STORE_SLOT");
                        break;
                    }

                    if (instr.operand < 0 || static_cast<size_t>(instr.operand) >= frame.locals.size()) {
                        this->diagnostics.addError(this->currentLoc, "Slot index out of range");
                        break;
                    }

                    frame.locals[instr.operand] = this->stack.back();
                    break;
                }

                case OpCode::DUP_STORE: {
                    if (this->stack.empty()) {
                        this->diagnostics.addError(this->currentLoc, "Stack underflow during DUP_STORE");
                        break;
                    }

                    const auto& name = std::get<std::string>(cur.constants[instr.operand]);
                    this->storeVariable(chunk, frame, name, this->stack.back());
                    break;
                }

                case OpCode::DUP_IS_NONE: {
                    if (this->stack.empty()) {
                        this->diagnostics.addError(this->currentLoc, "Stack underflow during DUP_IS_NONE");
                        break;
                    }

                    this->push(ValueNode::ValueType{
                        std::holds_alternative<std::monostate>(this->stack.back())
                    });
                    break;
                }

                case OpCode::LOAD_FIELD: {
                    const auto& name = std::get<std::string>(cur.constants[instr.operand]);
                    auto objValue = this->pop();

                    if (std::holds_alternative<ArrayRef>(objValue)) {
                        this->diagnostics.addError(this->currentLoc, "Array has no field: " + name);
                        break;
                    }

                    if (std::holds_alternative<std::string>(objValue)) {
                        this->diagnostics.addError(this->currentLoc, "String has no field: " + name);
                        break;
                    }

                    if (!std::holds_alternative<ObjectRef>(objValue)) {
                        this->diagnostics.addError(this->currentLoc, "Cannot load field '" + name + "' from a non-object value");
                        break;
                    }

                    auto obj = std::get<ObjectRef>(objValue);
                    const auto* field = obj->find(name);
                    if (!field) {
                        this->diagnostics.addError(this->currentLoc, "Object has no field: " + name);
                        break;
                    }

                    this->push(*field);
                    break;
                }
                case OpCode::LOAD_LEN: {
                    auto iterable = this->pop();

                    if (std::holds_alternative<ArrayRef>(iterable)) {
                        this->push(static_cast<int>(std::get<ArrayRef>(iterable)->elements.size()));
                        break;
                    }

                    if (std::holds_alternative<std::string>(iterable)) {
                        this->push(static_cast<int>(std::get<std::string>(iterable).size()));
                        break;
                    }

                    this->diagnostics.addError(this->currentLoc, "Cannot take length of a non-iterable value");
                    break;
                }
                case OpCode::BIND_THIS: {
                    if (instr.operand <= 0 || this->stack.empty())
                        break;

                    const size_t depth = static_cast<size_t>(instr.operand);
                    if (depth >= this->stack.size())
                        break;

                    const auto* self = std::get_if<ObjectRef>(&this->stack.back());
                    if (!self || !*self)
                        break;

                    auto& slot = this->stack[this->stack.size() - 1 - depth];
                    auto* func = std::get_if<FunctionRefPtr>(&slot);
                    if (!func || !*func)
                        break;

                    (*func)->hasThis = true;
                    (*func)->thisObj = *self;

                    for (auto& captured : (*func)->captures) {
                        if (const auto* held = std::get_if<ObjectRef>(&captured); held && *held == *self)
                            captured = std::monostate{};
                    }
                    break;
                }
                case OpCode::STORE_FIELD: {
                    const auto& name = std::get<std::string>(cur.constants[instr.operand]);
                    auto objValue = this->pop();
                    auto val = this->pop();

                    if (!std::holds_alternative<ObjectRef>(objValue)) {
                        this->diagnostics.addError(this->currentLoc, "Cannot store field '" + name + "' on a non-object value");
                        break;
                    }

                    std::get<ObjectRef>(objValue)->assign(name, val);
                    break;
                }
                case OpCode::LOAD_FIELD_SLOT: {
                    auto objValue = this->pop();

                    if (!std::holds_alternative<ObjectRef>(objValue)) {
                        this->diagnostics.addError(this->currentLoc, "Cannot load a field from a non-object value");
                        break;
                    }

                    auto obj = std::get<ObjectRef>(objValue);
                    if (instr.operand >= 0 && static_cast<size_t>(instr.operand) >= obj->slots.size())
                        obj->adoptLayout();

                    if (instr.operand < 0 || static_cast<size_t>(instr.operand) >= obj->slots.size()) {
                        this->diagnostics.addError(this->currentLoc, "Field slot index out of range");
                        break;
                    }

                    this->push(obj->slots[static_cast<size_t>(instr.operand)]);
                    break;
                }
                case OpCode::STORE_FIELD_SLOT: {
                    auto objValue = this->pop();
                    auto val = this->pop();

                    if (!std::holds_alternative<ObjectRef>(objValue)) {
                        this->diagnostics.addError(this->currentLoc, "Cannot store a field on a non-object value");
                        break;
                    }

                    auto obj = std::get<ObjectRef>(objValue);
                    if (instr.operand >= 0 && static_cast<size_t>(instr.operand) >= obj->slots.size())
                        obj->adoptLayout();

                    if (instr.operand < 0 || static_cast<size_t>(instr.operand) >= obj->slots.size()) {
                        this->diagnostics.addError(this->currentLoc, "Field slot index out of range");
                        break;
                    }

                    obj->slots[static_cast<size_t>(instr.operand)] = std::move(val);
                    break;
                }
                case OpCode::MAKE_ARRAY: {
                    int count = instr.operand;
                    if (count < 0 || count > static_cast<int>(this->stack.size())) {
                        this->diagnostics.addError(this->currentLoc, "Invalid array literal size");
                        break;
                    }

                    if (const auto violation = this->mBudget->accountArray(static_cast<std::size_t>(count));
                        violation != sandbox::SandboxBudget::Violation::None) {
                        fail(violation, "Array size budget exhausted");
                        break;
                    }

                    auto arr = std::make_shared<ArrayValue>();
                    arr->elements.resize(count);
                    for (int i = count - 1; i >= 0; --i)
                        arr->elements[i] = this->pop();

                    this->push(arr);
                    break;
                }
                case OpCode::LOAD_INDEX: {
                    auto indexValue = this->pop();
                    auto targetValue = this->pop();

                    if (!std::holds_alternative<ArrayRef>(targetValue)) {
                        this->diagnostics.addError(this->currentLoc, "Cannot index a non-array value");
                        break;
                    }
                    if (!std::holds_alternative<int>(indexValue)) {
                        this->diagnostics.addError(this->currentLoc, "Array index must be an int");
                        break;
                    }

                    auto arr = std::get<ArrayRef>(targetValue);
                    int index = std::get<int>(indexValue);
                    if (index < 0 || index >= static_cast<int>(arr->elements.size())) {
                        this->diagnostics.addError(this->currentLoc, "Array index out of range");
                        break;
                    }

                    this->push(arr->elements[index]);
                    break;
                }
                case OpCode::STORE_INDEX: {
                    auto indexValue = this->pop();
                    auto targetValue = this->pop();
                    auto value = this->pop();

                    if (!std::holds_alternative<ArrayRef>(targetValue)) {
                        this->diagnostics.addError(this->currentLoc, "Cannot index a non-array value");
                        break;
                    }
                    if (!std::holds_alternative<int>(indexValue)) {
                        this->diagnostics.addError(this->currentLoc, "Array index must be an int");
                        break;
                    }

                    auto arr = std::get<ArrayRef>(targetValue);
                    int index = std::get<int>(indexValue);
                    if (index < 0 || index > static_cast<int>(arr->elements.size())) {
                        this->diagnostics.addError(this->currentLoc, "Array index out of range");
                        break;
                    }

                    if (auto* nested = std::get_if<ArrayRef>(&value); nested && nested->get() == arr.get()) {
                        this->diagnostics.addError(this->currentLoc,
                            "Circular reference: an array cannot contain itself");
                        break;
                    }

                    if (index == static_cast<int>(arr->elements.size())) {
                        if (static_cast<std::size_t>(index + 1) > this->mBudget->maxArrayElements) {
                            fail(sandbox::SandboxBudget::Violation::ArrayElementLimit, "Array size budget exhausted");
                            break;
                        }

                        arr->elements.push_back(value);
                    } else {
                        arr->elements[index] = value;
                    }

                    break;
                }
                case OpCode::LOAD_THIS: {
                    if (!frame.hasThis) {
                        this->diagnostics.addError(this->currentLoc, "'this' is not available in the current context");
                        break;
                    }

                    this->push(frame.thisObj);
                    break;
                }
                case OpCode::MAKE_LAMBDA: {
                    const auto& meta = cur.lambdas[instr.operand];

                    auto func = std::make_shared<FunctionRef>();
                    func->owner = owner;
                    func->bodyIndex = meta.bodyIndex;
                    func->argCount = meta.argCount;
                    func->hasThis = frame.hasThis;
                    if (frame.hasThis)
                        func->thisObj = std::get<ObjectRef>(frame.thisObj);
                    func->captures.assign(
                        frame.locals.begin(),
                        frame.locals.begin() + std::min(static_cast<size_t>(meta.captureCount), frame.locals.size())
                    );
                    func->globals = this->globals;

                    this->push(func);
                    break;
                }
                case OpCode::INSTANCEOF: {
                    const auto& name = std::get<std::string>(cur.constants[instr.operand]);
                    auto value = this->pop();

                    if (!std::holds_alternative<ObjectRef>(value)) {
                        this->push(false);
                        break;
                    }

                    auto obj = std::get<ObjectRef>(value);
                    bool result = (obj->className == name);

                    if (!result && obj->classIndex >= 0) {
                        int targetIdx = -1;
                        for (size_t i = 0; i < chunk.classes.size(); ++i) {
                            if (chunk.classes[i].name == name) {
                                targetIdx = static_cast<int>(i);
                                break;
                            }
                        }

                        if (targetIdx >= 0)
                            result = this->isDerived(chunk, obj->classIndex, targetIdx);
                    }

                    this->push(result);
                    break;
                }

                case OpCode::NEW: {
                    if (const auto violation = this->mBudget->accountObject();
                        violation != sandbox::SandboxBudget::Violation::None) {
                        fail(violation, "Object count budget exhausted");
                        break;
                    }

                    const auto& cls = chunk.classes[instr.operand];

                    std::vector<ValueNode::ValueType> args;

                    if (cls.constructorIndex != -1) {
                        const auto& ctor = chunk.methods[cls.constructorIndex];
                        args.resize(ctor.argCount);

                        for (int i = 0; i < ctor.argCount; ++i)
                            args[ctor.argCount - 1 - i] = this->pop();
                    }

                    auto obj = std::make_shared<Object>();
                    obj->className = cls.name;
                    obj->classIndex = instr.operand;
                    obj->layout = this->classLayout(chunk, instr.operand);
                    obj->resize(cls.fieldNames.size());

                    for (size_t i = 0; i < cls.fieldNames.size(); ++i) {
                        if (cls.hasDefault[i])
                            obj->slots[i] = VM::cloneValue(cls.defaults[i]);
                    }

                    if (cls.constructorIndex != -1) {
                        const auto& ctor = chunk.methods[cls.constructorIndex];

                        Frame callee(*chunk.methodBodies[ctor.bodyIndex]);
                        callee.hasThis = true;
                        callee.thisObj = obj;
                        callee.hasPending = true;
                        callee.pendingPush = obj;

                        for (int i = 0; i < ctor.argCount; ++i)
                            callee.locals[i] = args[i];

                        if (!this->pushFrame(std::move(callee)))
                            break;
                    } else {
                        this->push(obj);
                    }
                    break;
                }

                case OpCode::NEW_NATIVE: {
                    if (const auto violation = this->mBudget->accountObject();
                        violation != sandbox::SandboxBudget::Violation::None) {
                        fail(violation, "Object count budget exhausted");
                        break;
                    }

                    const auto& meta = chunk.nativeCalls[instr.operand];

                    std::vector<ValueNode::ValueType> args(meta.argCount);
                    for (int i = 0; i < meta.argCount; ++i)
                        args[meta.argCount - 1 - i] = this->pop();

                    auto result = ClassCall::getInstance().createCached(
                        meta.className, args, placeholders,
                        this->mNativeConstructorSlots[instr.operand], this->diagnostics, this->currentLoc
                    );

                    if (!result.has_value()) {
                        this->diagnostics.addError(this->currentLoc,
                            "Failed to create native class '" + meta.className + "': " + result.error().message());
                        break;
                    }

                    this->push(result.value());
                    break;
                }

                case OpCode::CALL_METHOD: {
                    const auto& meta = chunk.methods[instr.operand];

                    auto receiver = this->pop();
                    if (!std::holds_alternative<ObjectRef>(receiver)) {
                        this->diagnostics.addError(this->currentLoc, "Method call target is not an object");
                        break;
                    }

                    auto obj = std::get<ObjectRef>(receiver);
                    if (!this->isDerived(chunk, obj->classIndex, meta.classIndex)) {
                        this->diagnostics.addError(this->currentLoc,
                            "Method '" + meta.name + "' does not belong to this object");
                        break;
                    }

                    std::vector<ValueNode::ValueType> args(meta.argCount);
                    for (int i = 0; i < meta.argCount; ++i)
                        args[meta.argCount - 1 - i] = this->pop();

                    Frame callee(*chunk.methodBodies[meta.bodyIndex]);
                    callee.hasThis = true;
                    callee.thisObj = receiver;

                    for (int i = 0; i < meta.argCount; ++i)
                        callee.locals[i] = args[i];

                    if (!this->pushFrame(std::move(callee)))
                        break;
                    break;
                }

                case OpCode::CALL_METHOD_VIRTUAL: {
                    const auto& meta = cur.virtualCalls[instr.operand];

                    auto receiver = this->pop();
                    if (!std::holds_alternative<ObjectRef>(receiver)) {
                        this->diagnostics.addError(this->currentLoc, "Method call target is not an object");
                        break;
                    }

                    auto obj = std::get<ObjectRef>(receiver);
                    if (obj->classIndex < 0 || obj->classIndex >= static_cast<int>(chunk.classes.size())) {
                        this->diagnostics.addError(this->currentLoc, "Invalid object class index");
                        break;
                    }

                    if (!this->isDerived(chunk, obj->classIndex, meta.classIndex)) {
                        this->diagnostics.addError(this->currentLoc, "Method call target is not an instance of the expected class");
                        break;
                    }

                    const auto& actualClass = chunk.classes[obj->classIndex];
                    if (meta.ordinal < 0 || meta.ordinal >= static_cast<int>(actualClass.methods.size())) {
                        this->diagnostics.addError(this->currentLoc, "Invalid method ordinal");
                        break;
                    }

                    const auto& method = chunk.methods[actualClass.methods[meta.ordinal]];
                    if (method.argCount != meta.argCount) {
                        this->diagnostics.addError(this->currentLoc,
                            "Method '" + method.name + "' expects " + std::to_string(method.argCount) +
                            " argument(s), got " + std::to_string(meta.argCount));
                        break;
                    }

                    std::vector<ValueNode::ValueType> args(method.argCount);
                    for (int i = 0; i < method.argCount; ++i)
                        args[method.argCount - 1 - i] = this->pop();

                    Frame callee(*chunk.methodBodies[method.bodyIndex]);
                    callee.hasThis = true;
                    callee.thisObj = receiver;

                    for (int i = 0; i < method.argCount; ++i)
                        callee.locals[i] = args[i];

                    if (!this->pushFrame(std::move(callee)))
                        break;
                    break;
                }

                case OpCode::CALL_SUPER_CTOR: {
                    const auto& meta = cur.superCalls[instr.operand];

                    auto receiver = this->pop();
                    if (!std::holds_alternative<ObjectRef>(receiver)) {
                        this->diagnostics.addError(this->currentLoc, "Super constructor call target is not an object");
                        break;
                    }

                    if (meta.constructorIndex < 0) {
                        if (meta.argCount != 0) {
                            this->diagnostics.addError(this->currentLoc, "Base class has no constructor");
                        } else {
                            this->push(std::string(""));
                        }
                        break;
                    }

                    const auto& ctor = chunk.methods[meta.constructorIndex];
                    if (ctor.argCount != meta.argCount) {
                        this->diagnostics.addError(this->currentLoc,
                            "Base constructor expects " + std::to_string(ctor.argCount) +
                            " argument(s), got " + std::to_string(meta.argCount));
                        break;
                    }

                    std::vector<ValueNode::ValueType> args(ctor.argCount);
                    for (int i = 0; i < ctor.argCount; ++i)
                        args[ctor.argCount - 1 - i] = this->pop();

                    Frame callee(*chunk.methodBodies[ctor.bodyIndex]);
                    callee.hasThis = true;
                    callee.thisObj = receiver;

                    for (int i = 0; i < ctor.argCount; ++i)
                        callee.locals[i] = args[i];

                    if (!this->pushFrame(std::move(callee)))
                        break;
                    break;
                }

                case OpCode::CALL_NATIVE_METHOD: {
                    if (const auto violation = this->mBudget->accountNativeCall();
                        violation != sandbox::SandboxBudget::Violation::None) {
                        fail(violation, "Native call budget exhausted");
                        break;
                    }

                    const auto& meta = chunk.nativeCalls[instr.operand];

                    if (meta.isStatic) {
                        std::vector<ValueNode::ValueType> args(meta.argCount);
                        for (int i = 0; i < meta.argCount; ++i)
                            args[meta.argCount - 1 - i] = this->pop();

                        auto result = ClassCall::getInstance().callStaticMethodCached(
                            meta.className, meta.name, args, placeholders,
                            this->mNativeStaticMethodSlots[instr.operand], this->diagnostics, this->currentLoc
                        );

                        if (!result.has_value()) {
                            this->diagnostics.addError(this->currentLoc,
                                "Native static method call failed '" + meta.className + "::" + meta.name +
                                "': " + result.error().message());
                            break;
                        }

                        this->push(result.value());
                        break;
                    }

                    auto receiver = this->pop();
                    if (!std::holds_alternative<ObjectRef>(receiver)) {
                        std::string valueClassName = valueClassNameOf(receiver);
                        if (valueClassName.empty()) {
                            this->diagnostics.addError(this->currentLoc, "Native method call target is not an object");
                            break;
                        }

                        std::vector<ValueNode::ValueType> args(meta.argCount);
                        for (int i = 0; i < meta.argCount; ++i)
                            args[meta.argCount - 1 - i] = this->pop();

                        auto result = ClassCall::getInstance().callValueMethodCached(
                            valueClassName, meta.name, receiver, args,
                            this->mNativeValueMethodSlots[instr.operand], this->diagnostics, this->currentLoc
                        );

                        if (!result.has_value()) {
                            this->diagnostics.addError(this->currentLoc,
                                "Native value method call failed '" + valueClassName + "::" + meta.name +
                                "': " + result.error().message());
                            break;
                        }

                        this->push(result.value());
                        break;
                    }

                    auto obj = std::get<ObjectRef>(receiver);

                    std::vector<ValueNode::ValueType> args(meta.argCount);
                    for (int i = 0; i < meta.argCount; ++i)
                        args[meta.argCount - 1 - i] = this->pop();

                    auto result = ClassCall::getInstance().callMethodCached(
                        obj->className, meta.name, args, obj, placeholders,
                        this->mNativeMethodSlots[instr.operand], this->diagnostics, this->currentLoc
                    );

                    if (!result.has_value()) {
                        this->diagnostics.addError(this->currentLoc,
                            "Native method call failed '" + meta.className + "::" + meta.name +
                            "': " + result.error().message());
                        break;
                    }

                    this->push(result.value());
                    break;
                }

                case OpCode::CALL_FUNC: {
                    const auto& meta = chunk.methods[instr.operand];

                    std::vector<ValueNode::ValueType> args(meta.argCount);
                    for (int i = 0; i < meta.argCount; ++i)
                        args[meta.argCount - 1 - i] = this->pop();

                    Frame callee(*chunk.methodBodies[meta.bodyIndex]);
                    callee.hasThis = false;

                    for (int i = 0; i < meta.argCount; ++i)
                        callee.locals[i] = args[i];

                    if (!this->pushFrame(std::move(callee)))
                        break;
                    break;
                }

                case OpCode::CALL_LAMBDA: {
                    auto funcValue = this->pop();
                    if (!std::holds_alternative<FunctionRefPtr>(funcValue)) {
                        this->diagnostics.addError(this->currentLoc, "Attempted to call a non-function value");
                        break;
                    }

                    auto func = std::get<FunctionRefPtr>(funcValue);
                    if (instr.operand != func->argCount) {
                        this->diagnostics.addError(this->currentLoc,
                            "Function expects " + std::to_string(func->argCount) +
                            " argument(s), got " + std::to_string(instr.operand));
                        break;
                    }

                    if (!func->owner ||
                        func->bodyIndex < 0 ||
                        func->bodyIndex >= static_cast<int>(func->owner->methodBodies.size())) {
                        this->diagnostics.addError(this->currentLoc, "Invalid function body index");
                        break;
                    }

                    std::vector<ValueNode::ValueType> args(func->argCount);
                    for (int i = 0; i < func->argCount; ++i)
                        args[func->argCount - 1 - i] = this->pop();

                    Frame callee(*func->owner->methodBodies[func->bodyIndex]);
                    callee.hasThis = func->hasThis;
                    if (func->hasThis) {
                        auto self = func->thisObj.lock();
                        if (!self) {
                            this->diagnostics.addError(this->currentLoc, "Method receiver has been released");
                            break;
                        }

                        callee.thisObj = self;
                    }

                    const size_t paramBase = func->captures.size();
                    if (paramBase + static_cast<size_t>(func->argCount) > callee.locals.size()) {
                        this->diagnostics.addError(this->currentLoc, "Lambda frame is too small for its parameters");
                        break;
                    }

                    std::copy_n(func->captures.begin(), paramBase, callee.locals.begin());

                    for (int i = 0; i < func->argCount; ++i)
                        callee.locals[paramBase + i] = args[i];

                    if (!this->pushFrame(std::move(callee)))
                        break;
                    break;
                }

                case OpCode::RETURN: {
                    auto result = this->pop();

                    Frame finished = std::move(this->frames.back());
                    this->frames.pop_back();

                    if (this->frames.empty())
                        return result;

                    if (finished.hasPending)
                        this->push(finished.pendingPush);
                    else
                        this->push(result);

                    break;
                }

                case OpCode::ADD: {
                    auto r = this->pop();
                    auto l = this->pop();

                    auto result = VM::applyArithmetic(l, r, "+", this->diagnostics, this->currentLoc);

                    if (std::holds_alternative<std::string>(result)) {
                        if (const auto violation = this->mBudget->accountString(std::get<std::string>(result).size());
                            violation != sandbox::SandboxBudget::Violation::None) {
                            fail(violation, "String size budget exhausted");
                            break;
                        }
                    }

                    this->push(std::move(result));
                    break;
                }
                case OpCode::SUB: {
                    auto r = this->pop();
                    auto l = this->pop();

                    this->push(VM::applyArithmetic(l, r, "-", this->diagnostics, this->currentLoc));
                    break;
                }
                case OpCode::MUL: {
                    auto r = this->pop();
                    auto l = this->pop();

                    this->push(VM::applyArithmetic(l, r, "*", this->diagnostics, this->currentLoc));
                    break;
                }
                case OpCode::DIV: {
                    auto r = this->pop();
                    auto l = this->pop();

                    this->push(VM::applyArithmetic(l, r, "/", this->diagnostics, this->currentLoc));
                    break;
                }
                case OpCode::MOD: {
                    auto r = this->pop();
                    auto l = this->pop();

                    this->push(VM::applyArithmetic(l, r, "%", this->diagnostics, this->currentLoc));
                    break;
                }
                case OpCode::POW: {
                    auto r = this->pop();
                    auto l = this->pop();

                    this->push(VM::applyArithmetic(l, r, "^", this->diagnostics, this->currentLoc));
                    break;
                }

                case OpCode::CMP_EQ: {
                    auto r = this->pop();
                    auto l = this->pop();

                    this->push(VM::applyComparison(l, r, "==", this->diagnostics, this->currentLoc));
                    break;
                }
                case OpCode::CMP_NE: {
                    auto r = this->pop();
                    auto l = this->pop();

                    this->push(VM::applyComparison(l, r, "!=", this->diagnostics, this->currentLoc));
                    break;
                }
                case OpCode::CMP_GT: {
                    auto r = this->pop();
                    auto l = this->pop();

                    this->push(VM::applyComparison(l, r, ">", this->diagnostics, this->currentLoc));
                    break;
                }
                case OpCode::CMP_LT: {
                    auto r = this->pop();
                    auto l = this->pop();

                    this->push(VM::applyComparison(l, r, "<", this->diagnostics, this->currentLoc));
                    break;
                }
                case OpCode::CMP_GE: {
                    auto r = this->pop();
                    auto l = this->pop();

                    this->push(VM::applyComparison(l, r, ">=", this->diagnostics, this->currentLoc));
                    break;
                }
                case OpCode::CMP_LE: {
                    auto r = this->pop();
                    auto l = this->pop();

                    this->push(VM::applyComparison(l, r, "<=", this->diagnostics, this->currentLoc));
                    break;
                }

                case OpCode::LOGIC_AND: {
                    auto r = this->pop();
                    auto l = this->pop();

                    this->push(VM::valueToBool(l) && VM::valueToBool(r));
                    break;
                }
                case OpCode::LOGIC_OR: {
                    auto r = this->pop();
                    auto l = this->pop();

                    this->push(VM::valueToBool(l) || VM::valueToBool(r));
                    break;
                }

                case OpCode::NEG: {
                    auto v = this->pop();

                    this->push(VM::applyUnary(v, "-", this->diagnostics, this->currentLoc));
                    break;
                }
                case OpCode::NOT: {
                    auto v = this->pop();

                    this->push(!VM::valueToBool(v));
                    break;
                }

                case OpCode::CALL: {
                    if (const auto violation = this->mBudget->accountNativeCall();
                        violation != sandbox::SandboxBudget::Violation::None) {
                        fail(violation, "Native call budget exhausted");
                        break;
                    }

                    const auto& meta = cur.functions[instr.operand];

                    CallbackTypeValues args;
                    args.reserve(meta.argCount);
                    for (int i = 0; i < meta.argCount; ++i)
                        args.push_back(this->pop());

                    std::ranges::reverse(args);

                    auto ns = meta.name.substr(0, meta.name.find("::"));
                    auto func = meta.name.substr(meta.name.find("::") + 2);
                    auto result = FunctionCall::getInstance().callFunctionCached(
                        ns, func, args, placeholders,
                        this->mFunctionCallSlots[instr.operand], this->diagnostics, this->currentLoc
                    );

                    if (!result.has_value()) {
                        this->diagnostics.addError(this->currentLoc, result.error().message());
                        this->push(ValueNode::ValueType{});
                        break;
                    }

                    this->push(result.value());
                    break;
                }
                case OpCode::CALL_MACRO: {
                    if (const auto violation = this->mBudget->accountNativeCall();
                        violation != sandbox::SandboxBudget::Violation::None) {
                        fail(violation, "Native call budget exhausted");
                        break;
                    }

                    const auto& meta = cur.macros[instr.operand];

                    CallbackTypeValues args;
                    args.reserve(meta.argCount);
                    for (int i = 0; i < meta.argCount; ++i)
                        args.push_back(this->pop());

                    std::ranges::reverse(args);
                    
                    auto result = MacroCall::getInstance().callMacro(
                        meta.name, args, placeholders, this->diagnostics, this->currentLoc
                    );

                    if (!result.has_value()) {
                        this->diagnostics.addError(this->currentLoc, result.error().message());
                        this->push(ValueNode::ValueType{});
                        break;
                    }

                    this->push(result.value());
                    break;
                }

                case OpCode::JMP_IF_FALSE: {
                    auto cond = this->pop();
                    if (!VM::valueToBool(cond))
                        frame.ip += instr.operand;

                    break;
                }
                case OpCode::JMP_IF_TRUE: {
                    auto cond = this->pop();
                    if (VM::valueToBool(cond))
                        frame.ip += instr.operand;
                    
                    break;
                }
                case OpCode::JMP:
                    frame.ip += instr.operand;
                    break;

                case OpCode::HALT: {
                    if (this->stack.empty()) {
                        return std::string("");
                    }

                    return this->stack.back();
                }

                default:
                    this->diagnostics.addError(this->currentLoc, "Unknown opcode");
                    break;
            }
        }
    }

    ValueNode::ValueType VM::callFunctionRef(
        const FunctionRefPtr& func,
        const CallbackTypeValues& args,
        const CallbackTypePlaces& placeholders,
        DiagnosticEngine& diagnostics
    ) {
        if (!func) {
            diagnostics.addError({ 0, 0, 0 }, "Cannot call a null function reference");
            return ValueNode::ValueType{};
        }

        if (!func->owner) {
            diagnostics.addError({ 0, 0, 0 }, "Function reference has no owning bytecode chunk");
            return ValueNode::ValueType{};
        }

        if (func->bodyIndex < 0 ||
            func->bodyIndex >= static_cast<int>(func->owner->methodBodies.size())) {
            diagnostics.addError({ 0, 0, 0 }, "Invalid function body index");
            return ValueNode::ValueType{};
        }

        if (static_cast<int>(args.size()) != func->argCount) {
            diagnostics.addError({ 0, 0, 0 },
                "Function expects " + std::to_string(func->argCount) +
                " argument(s), got " + std::to_string(args.size()));
            return ValueNode::ValueType{};
        }

        static thread_local size_t nativeCallDepth = 0;
        if (nativeCallDepth >= 64) {
            diagnostics.addError({ 0, 0, 0 }, "Nested native script call limit exceeded");
            return ValueNode::ValueType{};
        }

        ++nativeCallDepth;
        struct CallDepthGuard {
            size_t& depth;
            ~CallDepthGuard() { --depth; }
        } depthGuard{ nativeCallDepth };

        auto snapshot = func->globals.lock();
        VM vm(diagnostics, snapshot
            ? std::make_shared<GlobalsTable>(*snapshot)
            : std::make_shared<GlobalsTable>());

        vm.stack.clear();
        vm.frames.clear();

        Frame callee(*func->owner->methodBodies[func->bodyIndex]);
        callee.hasThis = func->hasThis;
        if (func->hasThis) {
            auto self = func->thisObj.lock();
            if (!self) {
                diagnostics.addError({}, "Method receiver has been released");
                return std::monostate{};
            }

            callee.thisObj = self;
        }

        const size_t paramBase = func->captures.size();
        if (paramBase + static_cast<size_t>(func->argCount) > callee.locals.size()) {
            diagnostics.addError({}, "Lambda frame is too small for its parameters");
            return std::monostate{};
        }

        std::copy_n(func->captures.begin(), paramBase, callee.locals.begin());

        for (int i = 0; i < func->argCount; ++i)
            callee.locals[paramBase + i] = args[i];

        vm.frames.push_back(std::move(callee));
        return vm.execute(func->owner, placeholders);
    }
}
