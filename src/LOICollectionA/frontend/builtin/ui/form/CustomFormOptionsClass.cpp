#include <string>
#include <variant>

#include <ll/api/Expected.h>

#include "LOICollectionA/frontend/Callback.h"

#include "LOICollectionA/frontend/stdlib/UIRawMessageClass.h"
#include "LOICollectionA/frontend/stdlib/ObservableBooleanClass.h"
#include "LOICollectionA/frontend/stdlib/ObservableNumberClass.h"
#include "LOICollectionA/frontend/stdlib/ObservableStringClass.h"
#include "LOICollectionA/frontend/stdlib/ObservableUIRawMessageClass.h"

#include "LOICollectionA/frontend/builtin/ui/form/CustomFormOptionsClass.h"

using namespace LOICollection::frontend;

namespace CustomFormOptionsClass {
    ll::Expected<ll::ui::TextValue> toTextValue(const TypedValue& value) {
        return std::visit([](auto&& arg) -> ll::Expected<ll::ui::TextValue> {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, std::string>) {
                return ll::ui::TextValue(arg);
            } else if constexpr (std::is_same_v<T, ObjectRef>) {
                if (arg->className == "UIRawMessage")
                    return ll::ui::TextValue(
                        static_cast<UIRawMessageClass::UIRawMessageHandle*>(arg->native.get())->base);
                if (arg->className == "ObservableString")
                    return ll::ui::TextValue(
                        *static_cast<ObservableStringClass::ObservableStringHandle*>(arg->native.get())->base);
                if (arg->className == "ObservableUIRawMessage")
                    return ll::ui::TextValue(
                        *static_cast<ObservableUIRawMessageClass::ObservableUIRawMessageHandle*>(arg->native.get())->base);
            }

            return ll::makeStringError(
                "expected a string, UIRawMessage, ObservableString or ObservableUIRawMessage");
        }, value);
    }

    ll::Expected<ll::ui::BooleanValue> toBooleanValue(const TypedValue& value) {
        return std::visit([](auto&& arg) -> ll::Expected<ll::ui::BooleanValue> {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, bool>) {
                return ll::ui::BooleanValue(arg);
            } else if constexpr (std::is_same_v<T, ObjectRef>) {
                if (arg->className == "ObservableBoolean")
                    return ll::ui::BooleanValue(
                        *static_cast<ObservableBooleanClass::ObservableBooleanHandle*>(arg->native.get())->base);
            }

            return ll::makeStringError("expected a bool or ObservableBoolean");
        }, value);
    }

    ll::Expected<ll::ui::NumberValue> toNumberValue(const TypedValue& value) {
        return std::visit([](auto&& arg) -> ll::Expected<ll::ui::NumberValue> {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, int> || std::is_same_v<T, float>) {
                return ll::ui::NumberValue(static_cast<double>(arg));
            } else if constexpr (std::is_same_v<T, ObjectRef>) {
                if (arg->className == "ObservableNumber")
                    return ll::ui::NumberValue(
                        *static_cast<ObservableNumberClass::ObservableNumberHandle*>(arg->native.get())->base);
            }

            return ll::makeStringError("expected a number or ObservableNumber");
        }, value);
    }

    namespace {
        ll::Expected<double> toDouble(const TypedValue& value) {
            return std::visit([](auto&& arg) -> ll::Expected<double> {
                using T = std::decay_t<decltype(arg)>;

                if constexpr (std::is_same_v<T, int> || std::is_same_v<T, float>)
                    return static_cast<double>(arg);

                return ll::makeStringError("expected a number");
            }, value);
        }
    }

    ll::Expected<ll::ui::ButtonOptions> toButtonOptions(const ObjectRef& options) {
        if (options->className != "ButtonOptions")
            return ll::makeStringError("expected ButtonOptions");

        auto disabled = readOptional<ll::ui::BooleanValue>(options, "disabled", toBooleanValue);
        if (!disabled)
            return ll::Unexpected(disabled.error());
        auto tooltip = readOptional<ll::ui::TextValue>(options, "tooltip", toTextValue);
        if (!tooltip)
            return ll::Unexpected(tooltip.error());
        auto visible = readOptional<ll::ui::BooleanValue>(options, "visible", toBooleanValue);
        if (!visible)
            return ll::Unexpected(visible.error());

        return ll::ui::ButtonOptions{ *disabled, *tooltip, *visible };
    }

    ll::Expected<ll::ui::SliderOptions> toSliderOptions(const ObjectRef& options) {
        if (options->className != "SliderOptions")
            return ll::makeStringError("expected SliderOptions");

        auto description = readOptional<ll::ui::TextValue>(options, "description", toTextValue);
        if (!description)
            return ll::Unexpected(description.error());
        auto disabled = readOptional<ll::ui::BooleanValue>(options, "disabled", toBooleanValue);
        if (!disabled)
            return ll::Unexpected(disabled.error());
        auto step = readOptional<ll::ui::NumberValue>(options, "step", toNumberValue);
        if (!step)
            return ll::Unexpected(step.error());
        auto visible = readOptional<ll::ui::BooleanValue>(options, "visible", toBooleanValue);
        if (!visible)
            return ll::Unexpected(visible.error());

        return ll::ui::SliderOptions{ *description, *disabled, *step, *visible };
    }

    ll::Expected<ll::ui::DropdownItemData> toDropdownItem(const ObjectRef& item) {
        if (item->className != "DropdownItem")
            return ll::makeStringError("expected DropdownItem");

        auto labelIt = item->fields.find("label");
        if (labelIt == item->fields.end())
            return ll::makeStringError("DropdownItem is missing 'label'");
        auto label = toTextValue(labelIt->second);
        if (!label)
            return ll::Unexpected(label.error());

        auto valueIt = item->fields.find("value");
        if (valueIt == item->fields.end())
            return ll::makeStringError("DropdownItem is missing 'value'");
        auto value = toDouble(valueIt->second);
        if (!value)
            return ll::Unexpected(value.error());

        auto description = readOptional<ll::ui::TextValue>(item, "description", toTextValue);
        if (!description)
            return ll::Unexpected(description.error());

        return ll::ui::DropdownItemData{ *label, *value, *description };
    }

    void registerClasses(const std::string&) {
        ClassCall& classes = ClassCall::getInstance();

        classes.registerClass("ButtonOptions", { "disabled", "tooltip", "visible" });
        classes.registerField("ButtonOptions", "disabled", std::monostate{});
        classes.registerField("ButtonOptions", "tooltip", std::monostate{});
        classes.registerField("ButtonOptions", "visible", std::monostate{});

        classes.registerClass("DividerOptions", { "visible" });
        classes.registerField("DividerOptions", "visible", std::monostate{});

        classes.registerClass("DropdownOptions", { "description", "disabled", "visible" });
        classes.registerField("DropdownOptions", "description", std::monostate{});
        classes.registerField("DropdownOptions", "disabled", std::monostate{});
        classes.registerField("DropdownOptions", "visible", std::monostate{});

        classes.registerClass("DropdownItem", { "label", "value", "description" });
        classes.registerField("DropdownItem", "label", std::string(""));
        classes.registerField("DropdownItem", "value", 0);
        classes.registerField("DropdownItem", "description", std::monostate{});

        classes.registerClass("SliderOptions", { "description", "disabled", "step", "visible" });
        classes.registerField("SliderOptions", "description", std::monostate{});
        classes.registerField("SliderOptions", "disabled", std::monostate{});
        classes.registerField("SliderOptions", "step", std::monostate{});
        classes.registerField("SliderOptions", "visible", std::monostate{});

        classes.registerClass("SpacingOptions", { "visible" });
        classes.registerField("SpacingOptions", "visible", std::monostate{});

        classes.registerClass("TextFieldOptions", { "description", "disabled", "visible" });
        classes.registerField("TextFieldOptions", "description", std::monostate{});
        classes.registerField("TextFieldOptions", "disabled", std::monostate{});
        classes.registerField("TextFieldOptions", "visible", std::monostate{});

        classes.registerClass("TextOptions", { "visible" });
        classes.registerField("TextOptions", "visible", std::monostate{});

        classes.registerClass("ToggleOptions", { "description", "disabled", "visible" });
        classes.registerField("ToggleOptions", "description", std::monostate{});
        classes.registerField("ToggleOptions", "disabled", std::monostate{});
        classes.registerField("ToggleOptions", "visible", std::monostate{});
    }
}

REGISTER_CALLBACK(CustomFormOptions, CustomFormOptionsClass::registerClasses)
