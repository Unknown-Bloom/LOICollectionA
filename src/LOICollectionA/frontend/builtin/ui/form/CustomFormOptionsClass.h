#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include <ll/api/Expected.h>
#include <ll/api/ui/form/CustomForm.h>

#include "LOICollectionA/frontend/Callback.h"

namespace CustomFormOptionsClass {
    ll::Expected<ll::ui::TextValue> toTextValue(const LOICollection::frontend::TypedValue& value);
    ll::Expected<ll::ui::BooleanValue> toBooleanValue(const LOICollection::frontend::TypedValue& value);
    ll::Expected<ll::ui::NumberValue> toNumberValue(const LOICollection::frontend::TypedValue& value);

    ll::Expected<ll::ui::ButtonOptions> toButtonOptions(const LOICollection::frontend::ObjectRef& options);
    ll::Expected<ll::ui::SliderOptions> toSliderOptions(const LOICollection::frontend::ObjectRef& options);
    ll::Expected<ll::ui::DropdownItemData> toDropdownItem(const LOICollection::frontend::ObjectRef& item);

    template <typename Handle, typename T>
    ll::Expected<std::reference_wrapper<T>> getObservable(
        const LOICollection::frontend::ObjectRef& obj, const std::string& className
    ) {
        if (obj->className != className)
            return ll::makeStringError("expected " + className);

        return std::ref(*static_cast<Handle*>(obj->native.get())->base);
    }

    template <typename T, typename Converter>
    ll::Expected<std::optional<T>> readOptional(
        const LOICollection::frontend::ObjectRef& options, const std::string& name, Converter&& convert
    ) {
        const auto* field = options->find(name);
        if (!field || std::holds_alternative<std::monostate>(*field))
            return std::optional<T>{};

        auto converted = convert(*field);
        if (!converted.has_value())
            return ll::Unexpected(converted.error());

        return std::move(*converted);
    }

    template <typename Options>
    ll::Expected<Options> readVisibleOptions(
        const LOICollection::frontend::ObjectRef& options, const std::string& className
    ) {
        if (options->className != className)
            return ll::makeStringError("expected " + className);

        auto visible = readOptional<ll::ui::BooleanValue>(options, "visible", toBooleanValue);
        if (!visible)
            return ll::Unexpected(visible.error());

        return Options{ *visible };
    }

    template <typename Options>
    ll::Expected<Options> readDescriptionOptions(
        const LOICollection::frontend::ObjectRef& options, const std::string& className
    ) {
        if (options->className != className)
            return ll::makeStringError("expected " + className);

        auto description = readOptional<ll::ui::TextValue>(options, "description", toTextValue);
        if (!description)
            return ll::Unexpected(description.error());
        auto disabled = readOptional<ll::ui::BooleanValue>(options, "disabled", toBooleanValue);
        if (!disabled)
            return ll::Unexpected(disabled.error());
        auto visible = readOptional<ll::ui::BooleanValue>(options, "visible", toBooleanValue);
        if (!visible)
            return ll::Unexpected(visible.error());

        return Options{ *description, *disabled, *visible };
    }

    void registerClasses(const std::string& name);
}
