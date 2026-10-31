#include <algorithm>
#include <string>
#include <variant>
#include <charconv>
#include <optional>

#include <ll/api/Expected.h>

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/Unicode.h"

#include "LOICollectionA/frontend/ir/VM.h"

#include "LOICollectionA/frontend/stdlib/StringClass.h"

using namespace LOICollection::frontend;

namespace StringClass::detail {
    std::optional<std::string> stringOperand(const TypedValue& value) {
        if (const auto* text = std::get_if<std::string>(&value))
            return *text;

        return std::nullopt;
    }

    bool isSpace(char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    }

    std::size_t stringLimit() {
        const ir::VM* vm = ir::VM::current();
        return vm ? vm->budget().maxStringBytes : (1 << 20);
    }

    std::size_t arrayLimit() {
        const ir::VM* vm = ir::VM::current();
        return vm ? vm->budget().maxArrayElements : 100'000;
    }
}

namespace StringClass {
    using namespace detail;

    ll::Expected<TypedValue> length(const TypedValue& self, const CallbackTypeValues&) {
        return static_cast<int>(codepointCount(std::get<std::string>(self)));
    }

    ll::Expected<TypedValue> split(const TypedValue& self, const CallbackTypeValues& args) {
        const std::string& text = std::get<std::string>(self);
        const std::string& separator = std::get<std::string>(args[0]);
        const std::size_t maxElements = arrayLimit();

        auto result = std::make_shared<ArrayValue>();
        if (separator.empty()) {
            result->elements.reserve(std::min(text.size(), maxElements));

            for (size_t i = 0; i < text.size(); i += codepointWidth(text[i])) {
                if (result->elements.size() >= maxElements)
                    return ll::makeStringError("String::split exceeds the array element budget");

                result->elements.emplace_back(text.substr(i, codepointWidth(text[i])));
            }

            return result;
        }

        size_t start = 0;
        while (true) {
            if (result->elements.size() >= maxElements)
                return ll::makeStringError("String::split exceeds the array element budget");

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

    ll::Expected<TypedValue> charAt(const TypedValue& self, const CallbackTypeValues& args) {
        const std::string& text = std::get<std::string>(self);

        const int index = std::get<int>(args[0]);
        if (index < 0)
            return std::string{};

        auto character = codepointAt(text, static_cast<size_t>(index));
        if (!character)
            return std::string{};

        return std::move(*character);
    }

    ll::Expected<TypedValue> upper(const TypedValue& self, const CallbackTypeValues&) {
        std::string result = std::get<std::string>(self);
        for (char& c : result) {
            if (c >= 'a' && c <= 'z')
                c = static_cast<char>(c - 'a' + 'A');
        }

        return result;
    }

    ll::Expected<TypedValue> lower(const TypedValue& self, const CallbackTypeValues&) {
        std::string result = std::get<std::string>(self);
        for (char& c : result) {
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        }

        return result;
    }

    ll::Expected<TypedValue> trim(const TypedValue& self, const CallbackTypeValues&) {
        const std::string& text = std::get<std::string>(self);

        size_t start = 0;
        size_t end = text.size();
        while (start < end && isSpace(text[start]))
            ++start;
        while (end > start && isSpace(text[end - 1]))
            --end;

        return text.substr(start, end - start);
    }

    ll::Expected<TypedValue> trimStart(const TypedValue& self, const CallbackTypeValues&) {
        const std::string& text = std::get<std::string>(self);

        size_t start = 0;
        while (start < text.size() && isSpace(text[start]))
            ++start;

        return text.substr(start);
    }

    ll::Expected<TypedValue> trimEnd(const TypedValue& self, const CallbackTypeValues&) {
        const std::string& text = std::get<std::string>(self);

        size_t end = text.size();
        while (end > 0 && isSpace(text[end - 1]))
            --end;

        return text.substr(0, end);
    }

    ll::Expected<TypedValue> replace(const TypedValue& self, const CallbackTypeValues& args) {
        const std::string& text = std::get<std::string>(self);
        const std::string& from = std::get<std::string>(args[0]);
        const std::string& to = std::get<std::string>(args[1]);

        if (from.empty())
            return text;

        const std::size_t limit = stringLimit();
        std::string result;
        result.reserve(std::min(text.size(), limit));

        size_t start = 0;
        while (true) {
            const size_t pos = text.find(from, start);
            const size_t chunk = pos == std::string::npos ? text.size() - start : pos - start;
            const size_t inserted = pos == std::string::npos ? 0 : to.size();

            if (result.size() + chunk + inserted > limit)
                return ll::makeStringError("String::replace exceeds the string budget");

            result.append(text, start, chunk);
            if (pos == std::string::npos)
                break;

            result.append(to);
            start = pos + from.size();
        }

        return result;
    }

    ll::Expected<TypedValue> substring(const TypedValue& self, const CallbackTypeValues& args) {
        const std::string& text = std::get<std::string>(self);

        int start = std::get<int>(args[0]);
        int end = std::get<int>(args[1]);
        const int count = static_cast<int>(codepointCount(text));

        if (start < 0)
            start = 0;
        if (end > count)
            end = count;
        if (start >= end)
            return std::string{};

        const size_t begin = codepointOffset(text, static_cast<size_t>(start));
        size_t stop = codepointOffset(text, static_cast<size_t>(end));
        if (stop == std::string::npos)
            stop = text.size();

        return text.substr(begin, stop - begin);
    }

    ll::Expected<TypedValue> indexOf(const TypedValue& self, const CallbackTypeValues& args) {
        const std::string& text = std::get<std::string>(self);

        size_t pos = text.find(std::get<std::string>(args[0]));

        return pos == std::string::npos ? -1 : static_cast<int>(codepointDistance(text, pos));
    }

    ll::Expected<TypedValue> lastIndexOf(const TypedValue& self, const CallbackTypeValues& args) {
        const std::string& text = std::get<std::string>(self);

        size_t pos = text.rfind(std::get<std::string>(args[0]));

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
        classes.registerValueMethod("String", "charAt", charAt, { ParamType::INT });
        classes.registerValueMethod("String", "substring", substring, { ParamType::INT, ParamType::INT });
        classes.registerValueMethod("String", "upper", upper, {});
        classes.registerValueMethod("String", "lower", lower, {});
        classes.registerValueMethod("String", "trim", trim, {});
        classes.registerValueMethod("String", "trimStart", trimStart, {});
        classes.registerValueMethod("String", "trimEnd", trimEnd, {});
        classes.registerValueMethod("String", "replace", replace, { ParamType::STRING, ParamType::STRING });
        classes.registerValueMethod("String", "split", split, { ParamType::STRING });
        classes.registerValueMethod("String", "contains", contains, { ParamType::STRING });
        classes.registerValueMethod("String", "startsWith", startsWith, { ParamType::STRING });
        classes.registerValueMethod("String", "endsWith", endsWith, { ParamType::STRING });
        classes.registerValueMethod("String", "indexOf", indexOf, { ParamType::STRING });
        classes.registerValueMethod("String", "lastIndexOf", lastIndexOf, { ParamType::STRING });
        classes.registerValueMethod("String", "toInt", toInt, {});
        classes.registerValueMethod("String", "toFloat", toFloat, {});

        classes.registerOperator("String", "+", [](const TypedValue& left, const TypedValue& right) -> ll::Expected<TypedValue> {
            auto l = stringOperand(left);
            if (!l)
                return ll::makeStringError("String '+' requires a string left operand");

            const std::optional<std::string> text = stringOperand(right);
            const std::string rendered = text ? *text : ir::VM::valueToString(right);
            if (l->size() + rendered.size() > stringLimit()) {
                if (ir::VM* vm = ir::VM::current())
                    vm->reportBudgetViolation(
                        sandbox::SandboxBudget::Violation::StringByteLimit, "String '+' exceeds the string budget"
                    );

                return ll::makeStringError("String '+' exceeds the string budget");
            }

            return *l + rendered;
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
