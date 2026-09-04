#include <cmath>
#include <limits>

#include "LOICollectionA/utils/core/MathUtils.h"

#include "LOICollectionA/frontend/ir/opt/analysis/FoldPredicates.h"

namespace LOICollection::frontend::ir::opt {
    StackEntry popEntry(std::vector<StackEntry>& stack) {
        if (stack.size() > 1) {
            StackEntry top = std::move(stack.back());
            stack.pop_back();
            return top;
        }

        return std::monostate{};
    }

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
            case OpCode::ADD: 
                return keptIsInt && identityIntZero;
            case OpCode::SUB: 
                return keptIsInt && identityIntZero;
            case OpCode::MUL: 
                return (keptIsInt && identityIntOne) || (keptIsFloat && (identityIntOne || identityFloatOne));
            case OpCode::ADD_I:
                return keptIsInt && identityIntZero;
            case OpCode::SUB_I:
                return keptIsInt && identityIntZero;
            case OpCode::MUL_I:
                return keptIsInt && identityIntOne;
            case OpCode::DIV: 
                return keptIsFloat && (identityIntOne || identityFloatOne);
            case OpCode::POW: 
                return keptIsFloat && (identityIntOne || identityFloatOne);
            default:
                return false;
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
        BytecodeChunk& chunk,
        std::vector<Instruction>& out,
        const ValueNode::ValueType& value,
        const SourceLocation& loc
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
}
