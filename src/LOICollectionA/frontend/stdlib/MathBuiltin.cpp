#include <cmath>
#include <limits>
#include <mutex>
#include <random>
#include <string>
#include <variant>

#include "LOICollectionA/frontend/Callback.h"

#include "LOICollectionA/utils/core/MathUtils.h"

#include "LOICollectionA/frontend/stdlib/MathBuiltin.h"

using namespace LOICollection::frontend;

namespace MathBuiltin {
    ll::Expected<TypedValue> abs(const CallbackTypeValues& args) {
        return std::visit([](auto&& arg) -> ll::Expected<TypedValue> {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, int> || std::is_same_v<T, float>) {
                if constexpr (std::is_same_v<T, int>) {
                    if (arg == std::numeric_limits<int>::min())
                        return ll::makeStringError("math::abs: INT_MIN is out of range");
                }

                return std::abs(arg);
            }

            return ll::makeStringError("math::abs: requires a numeric argument");
        }, args[0]);
    }

    TypedValue min(const CallbackTypeValues& args) {
        return std::visit([](auto&& arg1, auto&& arg2) -> TypedValue {
            using T1 = std::decay_t<decltype(arg1)>;
            using T2 = std::decay_t<decltype(arg2)>;

            if constexpr (
                (std::is_same_v<T1, int> && std::is_same_v<T2, int>) ||
                (std::is_same_v<T1, float> && std::is_same_v<T2, float>)
            ) return std::min(arg1, arg2);

            return {};
        }, args[0], args[1]);
    }

    TypedValue max(const CallbackTypeValues& args) {
        return std::visit([](auto&& arg1, auto&& arg2) -> TypedValue {
            using T1 = std::decay_t<decltype(arg1)>;
            using T2 = std::decay_t<decltype(arg2)>;

            if constexpr (
                (std::is_same_v<T1, int> && std::is_same_v<T2, int>) ||
                (std::is_same_v<T1, float> && std::is_same_v<T2, float>)
            ) return std::max(arg1, arg2);

            return {};
        }, args[0], args[1]);
    }

    TypedValue sqrt(const CallbackTypeValues& args) {
        return std::visit([](auto&& arg) -> TypedValue {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, int> || std::is_same_v<T, float>)
                return static_cast<float>(std::sqrt(arg));

            return {};
        }, args[0]);
    }

    TypedValue pow(const CallbackTypeValues& args) {
        return std::visit([](auto&& arg1, auto&& arg2) -> TypedValue {
            using T1 = std::decay_t<decltype(arg1)>;
            using T2 = std::decay_t<decltype(arg2)>;

            if constexpr ((std::is_same_v<T1, int> && std::is_same_v<T2, int>))
                return static_cast<float>(MathUtils::pow(arg1, arg2));
            else if constexpr ((std::is_same_v<T1, float> && std::is_same_v<T2, float>))
                return std::pow(arg1, arg2);

            return {};
        }, args[0], args[1]);
    }

    TypedValue log(const CallbackTypeValues& args) {
        return std::visit([](auto&& arg) -> TypedValue {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, int> || std::is_same_v<T, float>)
                return static_cast<float>(std::log(arg));

            return {};
        }, args[0]);
    }

    TypedValue sin(const CallbackTypeValues& args) {
        return std::visit([](auto&& arg) -> TypedValue {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, int> || std::is_same_v<T, float>)
                return static_cast<float>(std::sin(arg));

            return {};
        }, args[0]);
    }

    TypedValue cos(const CallbackTypeValues& args) {
        return std::visit([](auto&& arg) -> TypedValue {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, int> || std::is_same_v<T, float>)
                return static_cast<float>(std::cos(arg));

            return {};
        }, args[0]);
    }

    ll::Expected<TypedValue> random(const CallbackTypeValues& args) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::mutex rngMutex;

        return std::visit([](auto&& arg1, auto&& arg2) -> ll::Expected<TypedValue> {
            using T1 = std::decay_t<decltype(arg1)>;
            using T2 = std::decay_t<decltype(arg2)>;

            if constexpr (std::is_same_v<T1, int> && std::is_same_v<T2, int>) {
                if (arg1 > arg2)
                    return ll::makeStringError("math::random: min must not exceed max");

                std::lock_guard<std::mutex> lock(rngMutex);
                std::uniform_int_distribution<> dis(arg1, arg2);
                return dis(gen);
            } else if constexpr (std::is_same_v<T1, float> && std::is_same_v<T2, float>) {
                if (arg1 > arg2)
                    return ll::makeStringError("math::random: min must not exceed max");

                std::lock_guard<std::mutex> lock(rngMutex);
                std::uniform_real_distribution<> dis(arg1, arg2);
                return static_cast<float>(dis(gen));
            }
                
            return ll::makeStringError("math::random: requires two numeric arguments of the same type");
        }, args[0], args[1]);
    }

    void registerFunctions(const std::string& namespaces) {
        FunctionCall& functions = FunctionCall::getInstance();

        functions.registerFunction(namespaces, "abs", abs, { ParamType::INT });
        functions.registerFunction(namespaces, "abs", abs, { ParamType::FLOAT });
        functions.registerFunction(namespaces, "min", min, { ParamType::INT, ParamType::INT });
        functions.registerFunction(namespaces, "min", min, { ParamType::FLOAT, ParamType::FLOAT });
        functions.registerFunction(namespaces, "max", max, { ParamType::INT, ParamType::INT });
        functions.registerFunction(namespaces, "max", max, { ParamType::FLOAT, ParamType::FLOAT });
        functions.registerFunction(namespaces, "sqrt", sqrt, { ParamType::INT });
        functions.registerFunction(namespaces, "sqrt", sqrt, { ParamType::FLOAT });
        functions.registerFunction(namespaces, "pow", pow, { ParamType::INT, ParamType::INT });
        functions.registerFunction(namespaces, "pow", pow, { ParamType::FLOAT, ParamType::FLOAT });
        functions.registerFunction(namespaces, "log", log, { ParamType::INT });
        functions.registerFunction(namespaces, "log", log, { ParamType::FLOAT });
        functions.registerFunction(namespaces, "sin", sin, { ParamType::INT });
        functions.registerFunction(namespaces, "sin", sin, { ParamType::FLOAT });
        functions.registerFunction(namespaces, "cos", cos, { ParamType::INT });
        functions.registerFunction(namespaces, "cos", cos, { ParamType::FLOAT });
        functions.registerFunction(namespaces, "random", random, { ParamType::INT, ParamType::INT });
        functions.registerFunction(namespaces, "random", random, { ParamType::FLOAT, ParamType::FLOAT });
    }
}

REGISTER_CALLBACK(math, MathBuiltin::registerFunctions)
