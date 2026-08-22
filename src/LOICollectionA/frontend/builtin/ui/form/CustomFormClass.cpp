#include <any>
#include <memory>
#include <string>
#include <vector>
#include <utility>

#include <ll/api/Expected.h>
#include <ll/api/io/Logger.h>
#include <ll/api/io/LoggerRegistry.h>
#include <ll/api/ui/form/CustomForm.h>

#include <mc/world/actor/player/Player.h>

#include "LOICollectionA/include/form/GUIManager.h"

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"

#include "LOICollectionA/frontend/ir/VM.h"

#include "LOICollectionA/frontend/stdlib/ObservableBooleanClass.h"
#include "LOICollectionA/frontend/stdlib/ObservableNumberClass.h"
#include "LOICollectionA/frontend/stdlib/ObservableStringClass.h"

#include "LOICollectionA/frontend/builtin/ui/form/CustomFormOptionsClass.h"

#include "LOICollectionA/frontend/builtin/ui/form/CustomFormClass.h"

using namespace LOICollection::frontend;
using namespace CustomFormOptionsClass;

namespace CustomFormClass {
    ll::Expected<ObjectRef> makeCustomForm(const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        auto title = toTextValue(args[1]);
        if (!title)
            return ll::Unexpected(title.error());

        std::reference_wrapper<Player> player = std::any_cast<std::reference_wrapper<Player>>(placeholders.at(0));

        auto handle = std::make_shared<CustomFormHandle>();
        handle->base = std::make_unique<ll::ui::CustomForm>(player, *title);

        LOICollection::form::GUIManager::getInstance().registerCustomFormUI(std::get<std::string>(args[0]), handle, player);

        auto obj = std::make_shared<Object>();
        obj->className = "CustomForm";
        obj->classIndex = -1;
        obj->native = handle;

        return obj;
    }

    ll::Expected<TypedValue> button(
        const ObjectRef& self, const CallbackTypeValues& args, const CallbackTypePlaces& placeholders
    ) {
        auto* form = static_cast<CustomFormHandle*>(self->native.get())->base.get();

        auto label = toTextValue(args[0]);
        if (!label)
            return ll::Unexpected(label.error());

        auto callback = std::get<FunctionRefPtr>(args[1]);
        if (callback->argCount != 0)
            return ll::makeStringError("button callback must not take any arguments");

        auto options = toButtonOptions(std::get<ObjectRef>(args[2]));
        if (!options)
            return ll::Unexpected(options.error());

        form->button(*label, [callback, placeholders]() -> void {
            DiagnosticEngine diagnostics;

            [[maybe_unused]] auto result = ir::VM::callFunctionRef(callback, {}, placeholders, diagnostics);

            if (diagnostics.hasErrors()) {
                ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA")
                    ->error("CustomForm::button callback: {}", diagnostics.getErrorMessage());
            }
        }, *options);

        return self;
    }

    ll::Expected<TypedValue> closeButton(const ObjectRef& self, const CallbackTypeValues&) {
        static_cast<CustomFormHandle*>(self->native.get())->base->closeButton();

        return self;
    }

    ll::Expected<TypedValue> divider(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* form = static_cast<CustomFormHandle*>(self->native.get())->base.get();

        auto options = readVisibleOptions<ll::ui::DividerOptions>(
            std::get<ObjectRef>(args[0]), "DividerOptions");
        if (!options)
            return ll::Unexpected(options.error());

        form->divider(*options);
        return self;
    }

    ll::Expected<TypedValue> dropdown(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* form = static_cast<CustomFormHandle*>(self->native.get())->base.get();

        auto label = toTextValue(args[0]);
        if (!label)
            return ll::Unexpected(label.error());

        auto value = getObservable<
            ObservableNumberClass::ObservableNumberHandle, ll::ui::ObservableNumber>(
                std::get<ObjectRef>(args[1]), "ObservableNumber");
        if (!value)
            return ll::Unexpected(value.error());

        auto itemsValue = std::get<ArrayRef>(args[2]);

        std::vector<ll::ui::DropdownItemData> items;
        items.reserve(itemsValue->elements.size());
        
        for (const auto& element : itemsValue->elements) {
            if (!std::holds_alternative<ObjectRef>(element))
                return ll::makeStringError("dropdown items must be DropdownItem objects");

            auto item = toDropdownItem(std::get<ObjectRef>(element));
            if (!item)
                return ll::Unexpected(item.error());

            items.push_back(std::move(*item));
        }

        auto options = readDescriptionOptions<ll::ui::DropdownOptions>(
            std::get<ObjectRef>(args[3]), "DropdownOptions");
        if (!options)
            return ll::Unexpected(options.error());

        form->dropdown(*label, value->get(), std::move(items), *options);
        return self;
    }

    ll::Expected<TypedValue> header(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* form = static_cast<CustomFormHandle*>(self->native.get())->base.get();

        auto text = toTextValue(args[0]);
        if (!text)
            return ll::Unexpected(text.error());

        auto options = readVisibleOptions<ll::ui::TextOptions>(
            std::get<ObjectRef>(args[1]), "TextOptions");
        if (!options)
            return ll::Unexpected(options.error());

        form->header(*text, *options);
        return self;
    }

    ll::Expected<TypedValue> label(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* form = static_cast<CustomFormHandle*>(self->native.get())->base.get();

        auto text = toTextValue(args[0]);
        if (!text)
            return ll::Unexpected(text.error());

        auto options = readVisibleOptions<ll::ui::TextOptions>(
            std::get<ObjectRef>(args[1]), "TextOptions");
        if (!options)
            return ll::Unexpected(options.error());

        form->label(*text, *options);
        return self;
    }

    ll::Expected<TypedValue> slider(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* form = static_cast<CustomFormHandle*>(self->native.get())->base.get();

        auto label = toTextValue(args[0]);
        if (!label)
            return ll::Unexpected(label.error());

        auto value = getObservable<
            ObservableNumberClass::ObservableNumberHandle, ll::ui::ObservableNumber>(
                std::get<ObjectRef>(args[1]), "ObservableNumber");
        if (!value)
            return ll::Unexpected(value.error());

        auto min = toNumberValue(args[2]);
        if (!min)
            return ll::Unexpected(min.error());
        auto max = toNumberValue(args[3]);
        if (!max)
            return ll::Unexpected(max.error());

        auto options = toSliderOptions(std::get<ObjectRef>(args[4]));
        if (!options)
            return ll::Unexpected(options.error());

        form->slider(*label, value->get(), *min, *max, *options);
        return self;
    }

    ll::Expected<TypedValue> spacer(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* form = static_cast<CustomFormHandle*>(self->native.get())->base.get();

        auto options = readVisibleOptions<ll::ui::SpacingOptions>(
            std::get<ObjectRef>(args[0]), "SpacingOptions");
        if (!options)
            return ll::Unexpected(options.error());

        form->spacer(*options);
        return self;
    }

    ll::Expected<TypedValue> textField(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* form = static_cast<CustomFormHandle*>(self->native.get())->base.get();

        auto label = toTextValue(args[0]);
        if (!label)
            return ll::Unexpected(label.error());

        auto text = getObservable<
            ObservableStringClass::ObservableStringHandle, ll::ui::ObservableString>(
                std::get<ObjectRef>(args[1]), "ObservableString");
        if (!text)
            return ll::Unexpected(text.error());

        auto options = readDescriptionOptions<ll::ui::TextFieldOptions>(
            std::get<ObjectRef>(args[2]), "TextFieldOptions");
        if (!options)
            return ll::Unexpected(options.error());

        form->textField(*label, text->get(), *options);
        return self;
    }

    ll::Expected<TypedValue> toggle(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* form = static_cast<CustomFormHandle*>(self->native.get())->base.get();

        auto label = toTextValue(args[0]);
        if (!label)
            return ll::Unexpected(label.error());

        auto toggled = getObservable<
            ObservableBooleanClass::ObservableBooleanHandle, ll::ui::ObservableBoolean>(
                std::get<ObjectRef>(args[1]), "ObservableBoolean");
        if (!toggled)
            return ll::Unexpected(toggled.error());

        auto options = readDescriptionOptions<ll::ui::ToggleOptions>(
            std::get<ObjectRef>(args[2]), "ToggleOptions");
        if (!options)
            return ll::Unexpected(options.error());

        form->toggle(*label, toggled->get(), *options);
        return self;
    }

    ll::Expected<TypedValue> show(const ObjectRef& self, const CallbackTypeValues& args) {
        if (args.empty())
            return self;

        auto callback = std::get<FunctionRefPtr>(args[0]);
        if (callback->argCount != 1)
            return ll::makeStringError("show callback must take exactly one parameter");

        static_cast<CustomFormHandle*>(self->native.get())->show = callback;

        return self;
    }

    ll::Expected<TypedValue> close(const ObjectRef& self, const CallbackTypeValues&) {
        auto result = static_cast<CustomFormHandle*>(self->native.get())->base->close();
        if (!result)
            return ll::Unexpected(result.error());

        return self;
    }

    ll::Expected<TypedValue> isShowing(const ObjectRef& self, const CallbackTypeValues&) {
        return static_cast<CustomFormHandle*>(self->native.get())->base->isShowing();
    }

    void registerClasses(const std::string&) {
        ClassCall& classes = ClassCall::getInstance();

        classes.registerClass("CustomForm", {});
        classes.registerConstructor("CustomForm", makeCustomForm, { ParamType::STRING, ParamType::STRING });
        classes.registerConstructor("CustomForm", makeCustomForm, { ParamType::STRING, ParamType::OBJECT });

        classes.registerMethod("CustomForm", "button", button,
            { ParamType::STRING, ParamType::FUNCTION, ParamType::OBJECT });
        classes.registerMethod("CustomForm", "button", button,
            { ParamType::OBJECT, ParamType::FUNCTION, ParamType::OBJECT });

        classes.registerMethod("CustomForm", "closeButton", closeButton, {});

        classes.registerMethod("CustomForm", "divider", divider, { ParamType::OBJECT });

        classes.registerMethod("CustomForm", "dropdown", dropdown,
            { ParamType::STRING, ParamType::OBJECT, ParamType::ARRAY, ParamType::OBJECT });
        classes.registerMethod("CustomForm", "dropdown", dropdown,
            { ParamType::OBJECT, ParamType::OBJECT, ParamType::ARRAY, ParamType::OBJECT });

        classes.registerMethod("CustomForm", "header", header,
            { ParamType::STRING, ParamType::OBJECT });
        classes.registerMethod("CustomForm", "header", header,
            { ParamType::OBJECT, ParamType::OBJECT });

        classes.registerMethod("CustomForm", "label", label,
            { ParamType::STRING, ParamType::OBJECT });
        classes.registerMethod("CustomForm", "label", label,
            { ParamType::OBJECT, ParamType::OBJECT });

        classes.registerMethod("CustomForm", "slider", slider,
            { ParamType::STRING, ParamType::OBJECT, ParamType::FLOAT, ParamType::FLOAT, ParamType::OBJECT });
        classes.registerMethod("CustomForm", "slider", slider,
            { ParamType::STRING, ParamType::OBJECT, ParamType::INT, ParamType::INT, ParamType::OBJECT });
        classes.registerMethod("CustomForm", "slider", slider,
            { ParamType::STRING, ParamType::OBJECT, ParamType::INT, ParamType::FLOAT, ParamType::OBJECT });
        classes.registerMethod("CustomForm", "slider", slider,
            { ParamType::STRING, ParamType::OBJECT, ParamType::FLOAT, ParamType::INT, ParamType::OBJECT });
        classes.registerMethod("CustomForm", "slider", slider,
            { ParamType::STRING, ParamType::OBJECT, ParamType::OBJECT, ParamType::OBJECT, ParamType::OBJECT });
        classes.registerMethod("CustomForm", "slider", slider,
            { ParamType::OBJECT, ParamType::OBJECT, ParamType::FLOAT, ParamType::FLOAT, ParamType::OBJECT });
        classes.registerMethod("CustomForm", "slider", slider,
            { ParamType::OBJECT, ParamType::OBJECT, ParamType::INT, ParamType::INT, ParamType::OBJECT });
        classes.registerMethod("CustomForm", "slider", slider,
            { ParamType::OBJECT, ParamType::OBJECT, ParamType::INT, ParamType::FLOAT, ParamType::OBJECT });
        classes.registerMethod("CustomForm", "slider", slider,
            { ParamType::OBJECT, ParamType::OBJECT, ParamType::FLOAT, ParamType::INT, ParamType::OBJECT });
        classes.registerMethod("CustomForm", "slider", slider,
            { ParamType::OBJECT, ParamType::OBJECT, ParamType::OBJECT, ParamType::OBJECT, ParamType::OBJECT });

        classes.registerMethod("CustomForm", "spacer", spacer, { ParamType::OBJECT });

        classes.registerMethod("CustomForm", "textField", textField,
            { ParamType::STRING, ParamType::OBJECT, ParamType::OBJECT });
        classes.registerMethod("CustomForm", "textField", textField,
            { ParamType::OBJECT, ParamType::OBJECT, ParamType::OBJECT });

        classes.registerMethod("CustomForm", "toggle", toggle,
            { ParamType::STRING, ParamType::OBJECT, ParamType::OBJECT });
        classes.registerMethod("CustomForm", "toggle", toggle,
            { ParamType::OBJECT, ParamType::OBJECT, ParamType::OBJECT });

        classes.registerMethod("CustomForm", "show", show, {});
        classes.registerMethod("CustomForm", "show", show, { ParamType::FUNCTION });

        classes.registerMethod("CustomForm", "close", close, {});
        classes.registerMethod("CustomForm", "isShowing", isShowing, {});
    }
}

REGISTER_CALLBACK(CustomForm, CustomFormClass::registerClasses)
