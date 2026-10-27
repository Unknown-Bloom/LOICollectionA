#include <string>
#include <variant>
#include <iterator>
#include <exception>
#include <algorithm>

#include <fmt/args.h>
#include <fmt/format.h>

#include "LOICollectionA/frontend/Callback.h"

#include "LOICollectionA/frontend/ir/VM.h"

#include "LOICollectionA/frontend/stdlib/FormatBuiltin.h"

using namespace LOICollection::frontend;

namespace FormatBuiltin {
    ll::Expected<std::string> format(const CallbackTypeValues& args) {
        if (args.size() < 2)
            return ll::makeStringError("format requires a format string and an array argument");

        auto* formatStr = std::get_if<std::string>(&args[0]);
        auto* arrayRef  = std::get_if<ArrayRef>(&args[1]);
        if (!formatStr || !arrayRef || !*arrayRef)
            return ll::makeStringError("format arguments must be a string and a non-null array");

        auto& elements = (*arrayRef)->elements;
        if (elements.empty())
            return {};

        if (!std::ranges::all_of(elements, [](const auto& e) {
            return std::holds_alternative<int>(e) || std::holds_alternative<float>(e) ||
                std::holds_alternative<std::string>(e) || std::holds_alternative<bool>(e);
        })) {
            return ll::makeStringError("format's ArrayRef elements must be int, double, string, or bool");
        }

        fmt::dynamic_format_arg_store<fmt::format_context> store;
        store.reserve(elements.size(), 0);

        for (const auto& elem : elements) {
            std::visit([&store](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (fmt::is_formattable<T>())
                    store.push_back(value);
            }, elem);
        }

        ir::VM* vm = ir::VM::current();
        const std::size_t limit = vm ? vm->budget().maxStringBytes : (1 << 20);

        try {
            std::string result;
            result.reserve(std::min<std::size_t>(limit, 4096));

            const auto formatted = fmt::vformat_to_n(std::back_inserter(result), limit, *formatStr, store);
            if (formatted.size > limit)
                return ll::makeStringError("Format output exceeds the string budget");

            return result;
        } catch (const fmt::format_error& e) {
            return ll::makeStringError(std::string("format error: ") + e.what());
        } catch (const std::exception& e) {
            return ll::makeStringError(std::string("format failed: ") + e.what());
        }
    }

    void registerFunctions(const std::string& namespaces) {
        FunctionCall& functions = FunctionCall::getInstance();

        functions.registerFunction(namespaces, "format", format, { ParamType::STRING, ParamType::ARRAY });
    }
}

REGISTER_CALLBACK(std, FormatBuiltin::registerFunctions)
