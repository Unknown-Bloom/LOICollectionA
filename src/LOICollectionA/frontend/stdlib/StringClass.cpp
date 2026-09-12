#include <string>
#include <variant>
#include <charconv>

#include <ll/api/Expected.h>

#include <optional>

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/Unicode.h"

#include "LOICollectionA/frontend/ir/VM.h"

#include "LOICollectionA/frontend/stdlib/StringClass.h"

using namespace LOICollection::frontend;

namespace StringClass {
    namespace {
        std::optional<std::string> stringOperand(const TypedValue& value) {
            if (const auto* text = std::get_if<std::string>(&value))
                return *text;

            return std::nullopt;
        }
    }
    ll::Expected<TypedValue> length(const TypedValue& self, const CallbackTypeValues&) {
        return static_cast<int>(codepointCount(std::get<std::string>(self)));
    }

    ll::Expected<TypedValue> split(const TypedValue& self, const CallbackTypeValues& args) {
        const std::string& text = std::get<std::string>(self);
        const std::string& separator = std::get<std::string>(args[0]);

        auto result = std::make_shared<ArrayValue>();
        if (separator.empty()) {
            for (size_t i = 0; i < text.size(); i += codepointWidth(text[i]))
                result->elements.emplace_back(text.substr(i, codepointWidth(text[i])));

            return result;
        }

        size_t start = 0;
        while (true) {
            size_t pos = text.find(separator, start);
            if (pos == std::string::npos) {
                result->elements.emplace_back(text.substr(start));
                break;
            }

            result->elements.emplace_back(text.substr(start, pos - start));
            start = pos + separator.size();
        }

        return result;
    }

    ll::Expected<TypedValue> contains(const TypedValue& self, const CallbackTypeValues& args) {
        const std::string& text = std::get<std::string>(self);

        return text.find(std::get<std::string>(args[0])) != std::string::npos;
    }

    ll::Expected<TypedValue> startsWith(const TypedValue& self, const CallbackTypeValues& args) {
        const std::string& text = std::get<std::string>(self);
        const std::string& prefix = std::get<std::string>(args[0]);

        return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
    }

    ll::Expected<TypedValue> endsWith(const TypedValue& self, const CallbackTypeValues& args) {
        const std::string& text = std::get<std::string>(self);
        const std::string& suffix = std::get<std::string>(args[0]);

        return text.size() >= suffix.size() &&
               text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    ll::Expected<TypedValue> indexOf(const TypedValue& self, const CallbackTypeValues& args) {
        const std::string& text = std::get<std::string>(self);

        size_t pos = text.find(std::get<std::string>(args[0]));

        return pos == std::string::npos ? -1 : static_cast<int>(codepointDistance(text, pos));
    }

    ll::Expected<TypedValue> toInt(const TypedValue& self, const CallbackTypeValues&) {
        const std::string& text = std::get<std::string>(self);

        int value = 0;
        auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (ec != std::errc{} || ptr != text.data() + text.size())
            return TypedValue{ std::monostate{} };

        return value;
    }

    ll::Expected<TypedValue> toFloat(const TypedValue& self, const CallbackTypeValues&) {
        const std::string& text = std::get<std::string>(self);

        float value = 0.0f;
        auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (ec != std::errc{} || ptr != text.data() + text.size())
            return TypedValue{ std::monostate{} };

        return value;
    }

    void registerClasses(const std::string&) {
        ClassCall& classes = ClassCall::getInstance();

        classes.registerClass("String", {});

        classes.registerValueMethod("String", "length", length, {});
        classes.registerValueMethod("String", "split", split, { ParamType::STRING });
        classes.registerValueMethod("String", "contains", contains, { ParamType::STRING });
        classes.registerValueMethod("String", "startsWith", startsWith, { ParamType::STRING });
        classes.registerValueMethod("String", "endsWith", endsWith, { ParamType::STRING });
        classes.registerValueMethod("String", "indexOf", indexOf, { ParamType::STRING });
        classes.registerValueMethod("String", "toInt", toInt, {});
        classes.registerValueMethod("String", "toFloat", toFloat, {});

        classes.registerOperator("String", "+", [](const TypedValue& left, const TypedValue& right) -> ll::Expected<TypedValue> {
            auto l = stringOperand(left);
            if (!l)
                return ll::makeStringError("String '+' requires a string left operand");

            if (auto r = stringOperand(right))
                return *l + *r;
            return *l + ir::VM::valueToString(right);
        });

        for (const std::string op : { "==", "!=", ">", "<", ">=", "<=" })
            classes.registerOperator("String", op, [op](const TypedValue& left, const TypedValue& right) -> ll::Expected<TypedValue> {
                auto l = stringOperand(left);
                auto r = stringOperand(right);
                if (!l || !r)
                    return ll::makeStringError("String '" + op + "' requires string operands");

                auto cmp = *l <=> *r;
                if (op == "==") return cmp == 0;
                if (op == "!=") return cmp != 0;
                if (op == ">") return cmp > 0;
                if (op == "<") return cmp < 0;
                if (op == ">=") return cmp >= 0;

                return cmp <= 0;
            });
    }
}

REGISTER_CALLBACK(String, StringClass::registerClasses)
