#include <string>
#include <variant>
#include <algorithm>

#include "LOICollectionA/frontend/Callback.h"

#include "LOICollectionA/frontend/stdlib/StringBuiltin.h"

using namespace LOICollection::frontend;

namespace StringBuiltin {
    std::string length(const CallbackTypeValues& args) {
        return std::visit([](auto&& arg) -> std::string {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, std::string>)
                return std::to_string(arg.length());

            return {};
        }, args[0]);
    }

    std::string upper(const CallbackTypeValues& args) {
        return std::visit([](auto&& arg) -> std::string {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, std::string>) {
                std::string result = arg;
                std::transform(result.begin(), result.end(), result.begin(),
                    [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
                return result;
            }
            
            return {};
        }, args[0]);
    }

    std::string lower(const CallbackTypeValues& args) {
        return std::visit([](auto&& arg) -> std::string {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, std::string>) {
                std::string result = arg;
                std::transform(result.begin(), result.end(), result.begin(),
                    [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
                return result;
            }
                
            return {};
        }, args[0]);
    }

    std::string substr(const CallbackTypeValues& args) {
        return std::visit([](auto&& arg1, auto&& arg2, auto&& arg3) -> std::string {
            using T1 = std::decay_t<decltype(arg1)>;
            using T2 = std::decay_t<decltype(arg2)>;
            using T3 = std::decay_t<decltype(arg3)>;

            if constexpr (std::is_same_v<T1, std::string> && std::is_same_v<T2, int> && std::is_same_v<T3, int>) {
                if (arg2 < 0 || static_cast<size_t>(arg2) >= arg1.size())
                    return std::string{};

                size_t count = (arg3 < 0) ? 0 : static_cast<size_t>(arg3);
                return arg1.substr(arg2, count);
            }

            return {};
        }, args[0], args[1], args[2]);
    }

    std::string trim(const CallbackTypeValues& args) {
        return std::visit([](auto&& arg) -> std::string {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::string>) {
                auto first = arg.find_first_not_of(" \t\n\r");
                if (first == std::string::npos)
                    return std::string{};

                auto last = arg.find_last_not_of(" \t\n\r");
                return arg.substr(first, last - first + 1);
            }
            
            return {};
        }, args[0]);
    }

    std::string replace(const CallbackTypeValues& args) {
        return std::visit([](auto&& arg1, auto&& arg2, auto&& arg3) -> std::string {
            using T1 = std::decay_t<decltype(arg1)>;
            using T2 = std::decay_t<decltype(arg2)>;
            using T3 = std::decay_t<decltype(arg3)>;

            if constexpr (std::is_same_v<T1, std::string> && std::is_same_v<T2, std::string> && std::is_same_v<T3, std::string>) {
                if (arg2.empty())
                    return arg1;

                std::string result = arg1;
                size_t pos = 0;
                while ((pos = result.find(arg2, pos)) != std::string::npos) {
                    result.replace(pos, arg2.length(), arg3);

                    pos += arg3.length();
                }

                return result;
            }
            
            return {};
        }, args[0], args[1], args[2]);
    }

    void registerFunctions(const std::string& namespaces) {
        FunctionCall& functions = FunctionCall::getInstance();
        
        functions.registerFunction(namespaces, "length", length, { ParamType::STRING });
        functions.registerFunction(namespaces, "upper", upper, { ParamType::STRING });
        functions.registerFunction(namespaces, "lower", lower, { ParamType::STRING });
        functions.registerFunction(namespaces, "substr", substr, { ParamType::STRING, ParamType::INT, ParamType::INT });
        functions.registerFunction(namespaces, "trim", trim, { ParamType::STRING });
        functions.registerFunction(namespaces, "replace", replace, { ParamType::STRING, ParamType::STRING, ParamType::STRING });
    }
}

REGISTER_CALLBACK(string, StringBuiltin::registerFunctions);
