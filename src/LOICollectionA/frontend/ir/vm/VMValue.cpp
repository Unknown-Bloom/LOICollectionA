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

}
