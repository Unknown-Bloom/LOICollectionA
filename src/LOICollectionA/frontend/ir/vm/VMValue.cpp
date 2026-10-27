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

namespace LOICollection::frontend::ir::detail {
    constexpr std::size_t kMaxValueDepth = 64;
    constexpr std::size_t kMaxValueBytes = 1 << 20;

    void appendValueText(std::string& out, const ValueNode::ValueType& value, std::size_t depth) {
        if (out.size() >= kMaxValueBytes)
            return;

        std::visit([&out, depth](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<std::remove_cv_t<T>, int>)
                out += std::to_string(arg);
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, float>) {
                std::string result = std::to_string(arg);

                if (const auto last = result.find_last_not_of('0'); last != std::string::npos) {
                    result.erase(last + 1);

                    if (result.back() == '.')
                        result.pop_back();
                }

                out += result;
            }
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, std::string>)
                out.append(arg.data(), std::min(arg.size(), kMaxValueBytes - out.size()));
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, bool>)
                out += arg ? "true" : "false";
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, ObjectRef>)
                out += "instance of " + (arg ? arg->className : std::string("none"));
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, FunctionRefPtr>)
                out += "function";
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, ArrayRef>) {
                if (depth >= kMaxValueDepth) {
                    out += "[...]";
                    return;
                }

                out += '[';
                if (arg) {
                    for (size_t i = 0; i < arg->elements.size(); ++i) {
                        if (i != 0)
                            out += ", ";
                        appendValueText(out, arg->elements[i], depth + 1);
                        if (out.size() >= kMaxValueBytes)
                            break;
                    }
                }
                out += ']';
            }
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, std::monostate>)
                out += "None";
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, EnumValue>) {
                out += "enum#" + std::to_string(arg.enumId) + "[" + std::to_string(arg.tag) + "]";

                if (arg.payload && !arg.payload->items.empty()) {
                    if (depth >= kMaxValueDepth) {
                        out += "(...)";
                        return;
                    }

                    out += '(';
                    for (size_t i = 0; i < arg.payload->items.size(); ++i) {
                        if (i != 0)
                            out += ", ";
                        appendValueText(out, arg.payload->items[i], depth + 1);
                        if (out.size() >= kMaxValueBytes)
                            break;
                    }
                    out += ')';
                }
            }
        }, value);
    }
}

namespace LOICollection::frontend::ir {

    std::string VM::valueToString(const ValueNode::ValueType& val) {
        std::string result;
        result.reserve(64);
        detail::appendValueText(result, val, 0);
        return result;
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
                return arg ? arg->className : std::string("none");
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, FunctionRefPtr>)
                return "function";
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, ArrayRef>)
                return "array";
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, std::monostate>)
                return "none";
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, EnumValue>)
                return "enum";
        }, val);
    }

    ValueNode::ValueType VM::cloneValue(const ValueNode::ValueType& val, std::size_t depth) {
        if (!std::holds_alternative<ArrayRef>(val))
            return val;

        auto& source = std::get<ArrayRef>(val);
        if (!source || depth >= detail::kMaxValueDepth)
            return ValueNode::ValueType{};

        auto copy = std::make_shared<ArrayValue>();
        copy->elements.reserve(source->elements.size());

        for (const auto& element : source->elements)
            copy->elements.push_back(VM::cloneValue(element, depth + 1));

        return copy;
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
                return arg && !arg->elements.empty();
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, std::monostate>)
                return false;
            else if constexpr (std::is_same_v<std::remove_cv_t<T>, EnumValue>)
                return true;
        }, val);
    }

}
