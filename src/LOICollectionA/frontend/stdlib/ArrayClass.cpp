#include <string>
#include <variant>
#include <algorithm>
#include <functional>

#include <ll/api/Expected.h>

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"

#include "LOICollectionA/frontend/ir/VM.h"

#include "LOICollectionA/frontend/stdlib/ArrayClass.h"

using namespace LOICollection::frontend;

namespace ArrayClass {
    namespace {
        bool valuesEqual(const TypedValue& left, const TypedValue& right) {
            if (auto li = std::get_if<int>(&left)) {
                if (auto ri = std::get_if<int>(&right)) return *li == *ri;
                if (auto rf = std::get_if<float>(&right)) return *li == *rf;
                return false;
            }
            if (auto lf = std::get_if<float>(&left)) {
                if (auto ri = std::get_if<int>(&right)) return *lf == *ri;
                if (auto rf = std::get_if<float>(&right)) return *lf == *rf;
                return false;
            }
            if (auto ls = std::get_if<std::string>(&left)) {
                if (auto rs = std::get_if<std::string>(&right)) return *ls == *rs;
                return false;
            }
            if (auto lb = std::get_if<bool>(&left)) {
                if (auto rb = std::get_if<bool>(&right)) return *lb == *rb;
                return false;
            }
            if (auto lo = std::get_if<ObjectRef>(&left)) {
                if (auto ro = std::get_if<ObjectRef>(&right)) return *lo == *ro;
                return false;
            }
            if (auto la = std::get_if<ArrayRef>(&left)) {
                if (auto ra = std::get_if<ArrayRef>(&right)) return *la == *ra;
                return false;
            }

            return false;
        }

        ll::Expected<TypedValue> sortByComparator(const ArrayRef& arr, const FunctionRefPtr& comparator) {
            DiagnosticEngine diagnostics;
            bool failed = false;

            /* Script comparators are not trusted to satisfy the strict weak
             * ordering required by std::sort (violating it is UB). A bottom-up
             * merge sort keeps every access in bounds even for inconsistent
             * comparators; on failure the array is left in a valid state. */
            auto less = [&](const TypedValue& left, const TypedValue& right) -> bool {
                if (failed)
                    return false;

                auto result = ir::VM::callFunctionRef(comparator, { left, right }, {}, diagnostics);
                if (diagnostics.hasErrors())
                    failed = true;

                if (auto ri = std::get_if<int>(&result))
                    return *ri < 0;
                if (auto rf = std::get_if<float>(&result))
                    return *rf < 0;

                failed = true;
                return false;
            };

            std::vector<TypedValue>& elements = arr->elements;
            const size_t count = elements.size();
            std::vector<TypedValue> buffer(count);

            for (size_t width = 1; width < count && !failed; width *= 2) {
                for (size_t lo = 0; lo < count; lo += 2 * width) {
                    size_t mid = std::min(lo + width, count);
                    size_t hi = std::min(lo + 2 * width, count);
                    size_t i = lo, j = mid, k = lo;

                    while (i < mid && j < hi) {
                        if (less(elements[j], elements[i]))
                            buffer[k++] = std::move(elements[j++]);
                        else
                            buffer[k++] = std::move(elements[i++]);
                    }
                    while (i < mid)
                        buffer[k++] = std::move(elements[i++]);
                    while (j < hi)
                        buffer[k++] = std::move(elements[j++]);
                }

                std::swap_ranges(elements.begin(), elements.begin() + static_cast<std::ptrdiff_t>(count), buffer.begin());
            }

            if (failed)
                return ll::makeStringError("Comparator failed or returned a non-numeric value");

            return arr;
        }
    }

    ll::Expected<TypedValue> length(const TypedValue& self, const CallbackTypeValues&) {
        return static_cast<int>(std::get<ArrayRef>(self)->elements.size());
    }

    ll::Expected<TypedValue> push(const TypedValue& self, const CallbackTypeValues& args) {
        auto arr = std::get<ArrayRef>(self);
        arr->elements.push_back(args[0]);

        return static_cast<int>(arr->elements.size());
    }

    ll::Expected<TypedValue> pop(const TypedValue& self, const CallbackTypeValues&) {
        auto arr = std::get<ArrayRef>(self);
        if (arr->elements.empty())
            return ll::makeStringError("Cannot pop from an empty array");

        TypedValue last = arr->elements.back();
        arr->elements.pop_back();

        return last;
    }

    ll::Expected<TypedValue> contains(const TypedValue& self, const CallbackTypeValues& args) {
        auto arr = std::get<ArrayRef>(self);

        return std::ranges::any_of(arr->elements,
            [&args](const TypedValue& element) -> bool {
                return valuesEqual(element, args[0]);
            }
        );
    }

    ll::Expected<TypedValue> indexOf(const TypedValue& self, const CallbackTypeValues& args) {
        auto arr = std::get<ArrayRef>(self);

        for (size_t i = 0; i < arr->elements.size(); ++i) {
            if (valuesEqual(arr->elements[i], args[0]))
                return static_cast<int>(i);
        }

        return -1;
    }

    ll::Expected<TypedValue> join(const TypedValue& self, const CallbackTypeValues& args) {
        auto arr = std::get<ArrayRef>(self);
        const std::string& separator = std::get<std::string>(args[0]);

        std::string result;
        for (size_t i = 0; i < arr->elements.size(); ++i) {
            if (i != 0)
                result += separator;

            result += ir::VM::valueToString(arr->elements[i]);
        }

        return result;
    }

    ll::Expected<TypedValue> slice(const TypedValue& self, const CallbackTypeValues& args) {
        auto arr = std::get<ArrayRef>(self);
        int length = static_cast<int>(arr->elements.size());

        int start = std::get<int>(args[0]);
        int end = std::get<int>(args[1]);

        start = std::clamp(start, 0, length);
        end = std::clamp(end, 0, length);

        auto result = std::make_shared<ArrayValue>();
        if (start < end)
            result->elements.assign(arr->elements.begin() + start, arr->elements.begin() + end);

        return result;
    }

    ll::Expected<TypedValue> sort(const TypedValue& self, const CallbackTypeValues&) {
        auto arr = std::get<ArrayRef>(self);

        bool numeric = true;
        bool textual = true;
        bool boolean = true;
        for (const auto& element : arr->elements) {
            numeric = numeric && (std::holds_alternative<int>(element) || std::holds_alternative<float>(element));
            textual = textual && std::holds_alternative<std::string>(element);
            boolean = boolean && std::holds_alternative<bool>(element);
        }

        if (numeric) {
            std::sort(arr->elements.begin(), arr->elements.end(),
                [](const TypedValue& left, const TypedValue& right) -> bool {
                    auto l = std::holds_alternative<int>(left) ? std::get<int>(left) : std::get<float>(left);
                    auto r = std::holds_alternative<int>(right) ? std::get<int>(right) : std::get<float>(right);
                    return l < r;
                }
            );
        } else if (textual) {
            std::sort(arr->elements.begin(), arr->elements.end(),
                [](const TypedValue& left, const TypedValue& right) -> bool {
                    return std::get<std::string>(left) < std::get<std::string>(right);
                }
            );
        } else if (boolean) {
            std::sort(arr->elements.begin(), arr->elements.end(),
                [](const TypedValue& left, const TypedValue& right) -> bool {
                    return !std::get<bool>(left) && std::get<bool>(right);
                }
            );
        } else {
            return ll::makeStringError("Array elements of mixed types cannot be sorted without a comparator");
        }

        return arr;
    }

    ll::Expected<TypedValue> sortWithComparator(const TypedValue& self, const CallbackTypeValues& args) {
        return sortByComparator(std::get<ArrayRef>(self), std::get<FunctionRefPtr>(args[0]));
    }

    void registerClasses(const std::string&) {
        ClassCall& classes = ClassCall::getInstance();

        classes.registerClass("Array", {});

        for (ParamType type : { ParamType::INT, ParamType::FLOAT, ParamType::STRING, ParamType::BOOL,
                                ParamType::OBJECT, ParamType::FUNCTION, ParamType::ARRAY }) {
            classes.registerValueMethod("Array", "push", push, { type });
            classes.registerValueMethod("Array", "contains", contains, { type });
            classes.registerValueMethod("Array", "indexOf", indexOf, { type });
        }

        classes.registerValueMethod("Array", "pop", pop, {});
        classes.registerValueMethod("Array", "length", length, {});
        classes.registerValueMethod("Array", "join", join, { ParamType::STRING });
        classes.registerValueMethod("Array", "slice", slice, { ParamType::INT, ParamType::INT });
        classes.registerValueMethod("Array", "sort", sort, {});
        classes.registerValueMethod("Array", "sort", sortWithComparator, { ParamType::FUNCTION });
    }
}

REGISTER_CALLBACK(Array, ArrayClass::registerClasses)
