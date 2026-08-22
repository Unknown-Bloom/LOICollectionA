#include <any>
#include <format>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <variant>
#include <charconv>
#include <optional>

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

#include "LOICollectionA/frontend/builtin/ui/form/PaginatedFormClass.h"

using namespace LOICollection::frontend;
using namespace CustomFormOptionsClass;

namespace PaginatedFormClass {
    void refreshPage(const std::shared_ptr<PaginatedFormHandle>& form) {
        const int begin = (form->page - 1) * form->pageSize;
        const int end = std::min(begin + form->pageSize, static_cast<int>(form->elements.size()));

        for (int i = 0; i < form->pageSize; ++i) {
            if (begin + i < end) {
                form->labels[static_cast<size_t>(i)]->setData(form->elements[begin + static_cast<size_t>(i)]);
                form->visible[static_cast<size_t>(i)]->setData(true);
            } else {
                form->labels[static_cast<size_t>(i)]->setData("");
                form->visible[static_cast<size_t>(i)]->setData(false);
            }
        }

        form->pageIndicator->setData(std::format("{} / {}", form->page, form->pageCount));

        form->previousVisible->setData(form->page > 1);
        form->nextVisible->setData(form->page < form->pageCount);
        form->chooseVisible->setData(form->pageCount > 2);
    }

    ll::Expected<ObjectRef> makePaginatedForm(const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        auto title = toTextValue(args[1]);
        if (!title)
            return ll::Unexpected(title.error());

        auto elementsRef = std::get<ArrayRef>(args[2]);
        if (!std::ranges::all_of(elementsRef->elements, [](auto&& e) {
            return std::holds_alternative<std::string>(e);
        })) {
            return ll::makeStringError("makePaginatedForm's ArrayRef argument must contain only strings");
        }

        std::reference_wrapper<Player> player = std::any_cast<std::reference_wrapper<Player>>(placeholders.at(0));

        auto handle = std::make_shared<PaginatedFormHandle>();
        handle->guiId = std::get<std::string>(args[0]);
        handle->title = std::move(*title);
        handle->pageSize = args.size() >= 4
            ? std::get<int>(args[3]) : 10;
        handle->pageSize = std::max(1, handle->pageSize);

        handle->elements = elementsRef->elements
            | std::views::transform([](auto&& e) { return std::get<std::string>(e); })
            | std::ranges::to<std::vector>();

        handle->pageCount = (static_cast<int>(handle->elements.size()) + handle->pageSize - 1) / handle->pageSize;
        handle->pageCount = std::max(1, handle->pageCount);

        handle->labels.reserve(handle->pageSize);
        handle->visible.reserve(handle->pageSize);
        for (int i = 0; i < handle->pageSize; ++i) {
            handle->labels.push_back(std::make_shared<ll::ui::ObservableString>(""));
            handle->visible.push_back(std::make_shared<ll::ui::ObservableBoolean>(false));
        }

        handle->pageIndicator = std::make_shared<ll::ui::ObservableString>("");
        handle->input = std::make_shared<ll::ui::ObservableString>("", ll::ui::ObservableOptions{ true });
        handle->previousVisible = std::make_shared<ll::ui::ObservableBoolean>(false);
        handle->nextVisible = std::make_shared<ll::ui::ObservableBoolean>(false);
        handle->chooseVisible = std::make_shared<ll::ui::ObservableBoolean>(false);

        handle->base = std::make_unique<ll::ui::CustomForm>(player, handle->title);
        handle->base->label(*handle->pageIndicator);

        for (int i = 0; i < handle->pageSize; ++i) {
            handle->base->button(
                *handle->labels[static_cast<size_t>(i)],
                [handle, i]() -> void {
                    auto result = handle->base->close();

                    if (!result.has_value()) {
                        ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA")
                            ->error("PaginatedForm::button callback: {}", result.error().message());
                    }

                    const int begin = (handle->page - 1) * handle->pageSize;
                    const int index = begin + i;
                    if (index >= 0 && index < static_cast<int>(handle->elements.size())) {
                        handle->selection = handle->elements[static_cast<size_t>(index)];
                        handle->selectionIndex = index;
                        handle->selectionPage = handle->page;
                    }
                },
                ll::ui::ButtonOptions{ std::nullopt, std::nullopt, *handle->visible[static_cast<size_t>(i)] }
            );
        }

        refreshPage(handle);

        LOICollection::form::GUIManager::getInstance().registerPaginatedFormUI(handle->guiId, handle, player);

        auto obj = std::make_shared<Object>();
        obj->className = "PaginatedForm";
        obj->classIndex = -1;
        obj->native = handle;

        return obj;
    }

    ll::Expected<TypedValue> previous(const ObjectRef& self, const CallbackTypeValues&) {
        auto form = std::static_pointer_cast<PaginatedFormHandle>(self->native);
        if (form->page > 1)
            form->page -= 1;

        refreshPage(form);
        return self;
    }

    ll::Expected<TypedValue> next(const ObjectRef& self, const CallbackTypeValues&) {
        auto form = std::static_pointer_cast<PaginatedFormHandle>(self->native);
        if (form->page < form->pageCount)
            form->page += 1;

        refreshPage(form);
        return self;
    }

    ll::Expected<TypedValue> choose(const ObjectRef& self, const CallbackTypeValues& args) {
        auto form = std::static_pointer_cast<PaginatedFormHandle>(self->native);

        int page = std::get<int>(args[0]);
        if (page >= 1 && page <= form->pageCount)
            form->page = page;

        refreshPage(form);
        return self;
    }

    ll::Expected<TypedValue> previousButton(const ObjectRef& self, const CallbackTypeValues& args) {
        auto form = std::static_pointer_cast<PaginatedFormHandle>(self->native);
        if (form->previousAdded)
            return ll::makeStringError("previousButton has already been added");
        if (form->base->isShowing())
            return ll::makeStringError("PaginatedForm is already showing");

        auto text = toTextValue(args[0]);
        if (!text)
            return ll::Unexpected(text.error());

        form->previousText = *text;
        form->previousAdded = true;
        form->base->button(*form->previousText, [form]() -> void {
            if (form->page > 1)
                form->page -= 1;

            refreshPage(form);
        }, ll::ui::ButtonOptions{ std::nullopt, std::nullopt, *form->previousVisible });

        return self;
    }

    ll::Expected<TypedValue> nextButton(const ObjectRef& self, const CallbackTypeValues& args) {
        auto form = std::static_pointer_cast<PaginatedFormHandle>(self->native);
        if (form->nextAdded)
            return ll::makeStringError("nextButton has already been added");
        if (form->base->isShowing())
            return ll::makeStringError("PaginatedForm is already showing");

        auto text = toTextValue(args[0]);
        if (!text)
            return ll::Unexpected(text.error());

        form->nextText = *text;
        form->nextAdded = true;
        form->base->button(*form->nextText, [form]() -> void {
            if (form->page < form->pageCount)
                form->page += 1;

            refreshPage(form);
        }, ll::ui::ButtonOptions{ std::nullopt, std::nullopt, *form->nextVisible });

        return self;
    }

    ll::Expected<TypedValue> chooseButton(const ObjectRef& self, const CallbackTypeValues& args) {
        auto form = std::static_pointer_cast<PaginatedFormHandle>(self->native);
        if (form->chooseAdded)
            return ll::makeStringError("chooseButton has already been added");
        if (form->base->isShowing())
            return ll::makeStringError("PaginatedForm is already showing");

        auto text = toTextValue(args[0]);
        if (!text)
            return ll::Unexpected(text.error());

        auto placeholder = toTextValue(args[1]);
        if (!placeholder)
            return ll::Unexpected(placeholder.error());

        form->chooseText = *text;
        form->choosePlaceholder = *placeholder;
        form->chooseAdded = true;

        form->base->textField("", *form->input, ll::ui::TextFieldOptions{ std::nullopt, std::nullopt, *form->chooseVisible });
        form->base->button(*form->chooseText, [form]() -> void {
            const std::string& text = form->input->getData();
            int page = 0;

            const auto result = std::from_chars(text.data(), text.data() + text.size(), page);
            if (result.ec == std::errc{} && result.ptr == text.data() + text.size() && page >= 1 && page <= form->pageCount) {
                form->page = page;
                refreshPage(form);

                if (form->choosePlaceholder) {
                    if (const auto* placeholder = std::get_if<std::string>(&*form->choosePlaceholder))
                        form->input->setData(*placeholder);
                }
            }
        }, ll::ui::ButtonOptions{ std::nullopt, std::nullopt, *form->chooseVisible });

        return self;
    }

    ll::Expected<TypedValue> button(const ObjectRef& self, const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        auto form = std::static_pointer_cast<PaginatedFormHandle>(self->native);
        if (form->base->isShowing())
            return ll::makeStringError("PaginatedForm is already showing");

        auto label = toTextValue(args[0]);
        if (!label)
            return ll::Unexpected(label.error());

        auto callback = std::get<FunctionRefPtr>(args[1]);
        if (callback->argCount != 0)
            return ll::makeStringError("button callback must not take any arguments");

        auto options = toButtonOptions(std::get<ObjectRef>(args[2]));
        if (!options)
            return ll::Unexpected(options.error());

        form->base->button(*label, [callback, placeholders]() -> void {
            DiagnosticEngine diagnostics;

            [[maybe_unused]] auto result = ir::VM::callFunctionRef(callback, {}, placeholders, diagnostics);

            if (diagnostics.hasErrors()) {
                ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA")
                    ->error("PaginatedForm::button callback: {}", diagnostics.getErrorMessage());
            }
        }, *options);

        return self;
    }

    ll::Expected<TypedValue> closeButton(const ObjectRef& self, const CallbackTypeValues&) {
        auto form = std::static_pointer_cast<PaginatedFormHandle>(self->native);
        if (form->closeButtonAdded)
            return ll::makeStringError("closeButton has already been added");
        if (form->base->isShowing())
            return ll::makeStringError("PaginatedForm is already showing");

        form->closeButtonAdded = true;
        form->base->closeButton();

        return self;
    }

    ll::Expected<TypedValue> divider(const ObjectRef& self, const CallbackTypeValues& args) {
        auto form = std::static_pointer_cast<PaginatedFormHandle>(self->native);
        if (form->base->isShowing())
            return ll::makeStringError("PaginatedForm is already showing");

        auto options = readVisibleOptions<ll::ui::DividerOptions>(std::get<ObjectRef>(args[0]), "DividerOptions");
        if (!options)
            return ll::Unexpected(options.error());

        form->base->divider(*options);
        return self;
    }

    ll::Expected<TypedValue> dropdown(const ObjectRef& self, const CallbackTypeValues& args) {
        auto form = std::static_pointer_cast<PaginatedFormHandle>(self->native);
        if (form->base->isShowing())
            return ll::makeStringError("PaginatedForm is already showing");

        auto label = toTextValue(args[0]);
        if (!label)
            return ll::Unexpected(label.error());

        auto value = getObservable<ObservableNumberClass::ObservableNumberHandle, ll::ui::ObservableNumber>(
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

        auto options = readDescriptionOptions<ll::ui::DropdownOptions>(std::get<ObjectRef>(args[3]), "DropdownOptions");
        if (!options)
            return ll::Unexpected(options.error());

        form->base->dropdown(*label, value->get(), std::move(items), *options);
        return self;
    }

    ll::Expected<TypedValue> header(const ObjectRef& self, const CallbackTypeValues& args) {
        auto form = std::static_pointer_cast<PaginatedFormHandle>(self->native);
        if (form->base->isShowing())
            return ll::makeStringError("PaginatedForm is already showing");

        auto text = toTextValue(args[0]);
        if (!text)
            return ll::Unexpected(text.error());

        auto options = readVisibleOptions<ll::ui::TextOptions>(std::get<ObjectRef>(args[1]), "TextOptions");
        if (!options)
            return ll::Unexpected(options.error());

        form->base->header(*text, *options);
        return self;
    }

    ll::Expected<TypedValue> label(const ObjectRef& self, const CallbackTypeValues& args) {
        auto form = std::static_pointer_cast<PaginatedFormHandle>(self->native);
        if (form->base->isShowing())
            return ll::makeStringError("PaginatedForm is already showing");

        auto text = toTextValue(args[0]);
        if (!text)
            return ll::Unexpected(text.error());

        auto options = readVisibleOptions<ll::ui::TextOptions>(std::get<ObjectRef>(args[1]), "TextOptions");
        if (!options)
            return ll::Unexpected(options.error());

        form->base->label(*text, *options);
        return self;
    }

    ll::Expected<TypedValue> slider(const ObjectRef& self, const CallbackTypeValues& args) {
        auto form = std::static_pointer_cast<PaginatedFormHandle>(self->native);
        if (form->base->isShowing())
            return ll::makeStringError("PaginatedForm is already showing");

        auto label = toTextValue(args[0]);
        if (!label)
            return ll::Unexpected(label.error());

        auto value = getObservable<ObservableNumberClass::ObservableNumberHandle, ll::ui::ObservableNumber>(
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

        form->base->slider(*label, value->get(), *min, *max, *options);
        return self;
    }

    ll::Expected<TypedValue> spacer(const ObjectRef& self, const CallbackTypeValues& args) {
        auto form = std::static_pointer_cast<PaginatedFormHandle>(self->native);
        if (form->base->isShowing())
            return ll::makeStringError("PaginatedForm is already showing");

        auto options = readVisibleOptions<ll::ui::SpacingOptions>(std::get<ObjectRef>(args[0]), "SpacingOptions");
        if (!options)
            return ll::Unexpected(options.error());

        form->base->spacer(*options);
        return self;
    }

    ll::Expected<TypedValue> textField(const ObjectRef& self, const CallbackTypeValues& args) {
        auto form = std::static_pointer_cast<PaginatedFormHandle>(self->native);
        if (form->base->isShowing())
            return ll::makeStringError("PaginatedForm is already showing");

        auto label = toTextValue(args[0]);
        if (!label)
            return ll::Unexpected(label.error());

        auto text = getObservable<ObservableStringClass::ObservableStringHandle, ll::ui::ObservableString>(
            std::get<ObjectRef>(args[1]), "ObservableString");
        if (!text)
            return ll::Unexpected(text.error());

        auto options = readDescriptionOptions<ll::ui::TextFieldOptions>(std::get<ObjectRef>(args[2]), "TextFieldOptions");
        if (!options)
            return ll::Unexpected(options.error());

        form->base->textField(*label, text->get(), *options);
        return self;
    }

    ll::Expected<TypedValue> toggle(const ObjectRef& self, const CallbackTypeValues& args) {
        auto form = std::static_pointer_cast<PaginatedFormHandle>(self->native);
        if (form->base->isShowing())
            return ll::makeStringError("PaginatedForm is already showing");

        auto label = toTextValue(args[0]);
        if (!label)
            return ll::Unexpected(label.error());

        auto toggled = getObservable<ObservableBooleanClass::ObservableBooleanHandle, ll::ui::ObservableBoolean>(
            std::get<ObjectRef>(args[1]), "ObservableBoolean");
        if (!toggled)
            return ll::Unexpected(toggled.error());

        auto options = readDescriptionOptions<ll::ui::ToggleOptions>(std::get<ObjectRef>(args[2]), "ToggleOptions");
        if (!options)
            return ll::Unexpected(options.error());

        form->base->toggle(*label, toggled->get(), *options);
        return self;
    }

    ll::Expected<TypedValue> show(const ObjectRef& self, const CallbackTypeValues& args) {
        if (args.empty())
            return self;

        auto callback = std::get<FunctionRefPtr>(args[0]);
        if (callback->argCount != 1)
            return ll::makeStringError("show callback must take exactly one parameter");

        static_cast<PaginatedFormHandle*>(self->native.get())->show = callback;

        return self;
    }

    ll::Expected<TypedValue> close(const ObjectRef& self, const CallbackTypeValues&) {
        auto result = static_cast<PaginatedFormHandle*>(self->native.get())->base->close();
        if (!result)
            return ll::Unexpected(result.error());

        return self;
    }

    ll::Expected<TypedValue> isShowing(const ObjectRef& self, const CallbackTypeValues&) {
        return static_cast<PaginatedFormHandle*>(self->native.get())->base->isShowing();
    }

    void registerClasses(const std::string&) {
        ClassCall& classes = ClassCall::getInstance();

        classes.registerClass("PaginatedFormResult", { "closeReason", "selection", "selectionIndex", "page" });
        classes.registerField("PaginatedFormResult", "closeReason", 0);
        classes.registerField("PaginatedFormResult", "selection", "");
        classes.registerField("PaginatedFormResult", "selectionIndex", 0);
        classes.registerField("PaginatedFormResult", "page", 0);

        classes.registerClass("PaginatedForm", {});
        classes.registerConstructor("PaginatedForm", makePaginatedForm,
            { ParamType::STRING, ParamType::STRING, ParamType::ARRAY, ParamType::INT });
        classes.registerConstructor("PaginatedForm", makePaginatedForm,
            { ParamType::STRING, ParamType::STRING, ParamType::ARRAY });
        classes.registerConstructor("PaginatedForm", makePaginatedForm,
            { ParamType::STRING, ParamType::OBJECT, ParamType::ARRAY, ParamType::INT });
        classes.registerConstructor("PaginatedForm", makePaginatedForm,
            { ParamType::STRING, ParamType::OBJECT, ParamType::ARRAY });

        classes.registerMethod("PaginatedForm", "previous", previous, {});
        classes.registerMethod("PaginatedForm", "next", next, {});
        classes.registerMethod("PaginatedForm", "choose", choose, { ParamType::INT });

        classes.registerMethod("PaginatedForm", "previousButton", previousButton, { ParamType::STRING });
        classes.registerMethod("PaginatedForm", "previousButton", previousButton, { ParamType::OBJECT });
        classes.registerMethod("PaginatedForm", "nextButton", nextButton, { ParamType::STRING });
        classes.registerMethod("PaginatedForm", "nextButton", nextButton, { ParamType::OBJECT });
        classes.registerMethod("PaginatedForm", "chooseButton", chooseButton,
            { ParamType::STRING, ParamType::STRING });
        classes.registerMethod("PaginatedForm", "chooseButton", chooseButton,
            { ParamType::STRING, ParamType::OBJECT });
        classes.registerMethod("PaginatedForm", "chooseButton", chooseButton,
            { ParamType::OBJECT, ParamType::STRING });
        classes.registerMethod("PaginatedForm", "chooseButton", chooseButton,
            { ParamType::OBJECT, ParamType::OBJECT });

        classes.registerMethod("PaginatedForm", "button", button,
            { ParamType::STRING, ParamType::FUNCTION, ParamType::OBJECT });
        classes.registerMethod("PaginatedForm", "button", button,
            { ParamType::OBJECT, ParamType::FUNCTION, ParamType::OBJECT });

        classes.registerMethod("PaginatedForm", "closeButton", closeButton, {});

        classes.registerMethod("PaginatedForm", "divider", divider, { ParamType::OBJECT });

        classes.registerMethod("PaginatedForm", "dropdown", dropdown,
            { ParamType::STRING, ParamType::OBJECT, ParamType::ARRAY, ParamType::OBJECT });
        classes.registerMethod("PaginatedForm", "dropdown", dropdown,
            { ParamType::OBJECT, ParamType::OBJECT, ParamType::ARRAY, ParamType::OBJECT });

        classes.registerMethod("PaginatedForm", "header", header,
            { ParamType::STRING, ParamType::OBJECT });
        classes.registerMethod("PaginatedForm", "header", header,
            { ParamType::OBJECT, ParamType::OBJECT });

        classes.registerMethod("PaginatedForm", "label", label,
            { ParamType::STRING, ParamType::OBJECT });
        classes.registerMethod("PaginatedForm", "label", label,
            { ParamType::OBJECT, ParamType::OBJECT });

        classes.registerMethod("PaginatedForm", "slider", slider,
            { ParamType::STRING, ParamType::OBJECT, ParamType::FLOAT, ParamType::FLOAT, ParamType::OBJECT });
        classes.registerMethod("PaginatedForm", "slider", slider,
            { ParamType::STRING, ParamType::OBJECT, ParamType::INT, ParamType::INT, ParamType::OBJECT });
        classes.registerMethod("PaginatedForm", "slider", slider,
            { ParamType::STRING, ParamType::OBJECT, ParamType::INT, ParamType::FLOAT, ParamType::OBJECT });
        classes.registerMethod("PaginatedForm", "slider", slider,
            { ParamType::STRING, ParamType::OBJECT, ParamType::FLOAT, ParamType::INT, ParamType::OBJECT });
        classes.registerMethod("PaginatedForm", "slider", slider,
            { ParamType::STRING, ParamType::OBJECT, ParamType::OBJECT, ParamType::OBJECT, ParamType::OBJECT });
        classes.registerMethod("PaginatedForm", "slider", slider,
            { ParamType::OBJECT, ParamType::OBJECT, ParamType::FLOAT, ParamType::FLOAT, ParamType::OBJECT });
        classes.registerMethod("PaginatedForm", "slider", slider,
            { ParamType::OBJECT, ParamType::OBJECT, ParamType::INT, ParamType::INT, ParamType::OBJECT });
        classes.registerMethod("PaginatedForm", "slider", slider,
            { ParamType::OBJECT, ParamType::OBJECT, ParamType::INT, ParamType::FLOAT, ParamType::OBJECT });
        classes.registerMethod("PaginatedForm", "slider", slider,
            { ParamType::OBJECT, ParamType::OBJECT, ParamType::FLOAT, ParamType::INT, ParamType::OBJECT });
        classes.registerMethod("PaginatedForm", "slider", slider,
            { ParamType::OBJECT, ParamType::OBJECT, ParamType::OBJECT, ParamType::OBJECT, ParamType::OBJECT });

        classes.registerMethod("PaginatedForm", "spacer", spacer, { ParamType::OBJECT });

        classes.registerMethod("PaginatedForm", "textField", textField,
            { ParamType::STRING, ParamType::OBJECT, ParamType::OBJECT });
        classes.registerMethod("PaginatedForm", "textField", textField,
            { ParamType::OBJECT, ParamType::OBJECT, ParamType::OBJECT });

        classes.registerMethod("PaginatedForm", "toggle", toggle,
            { ParamType::STRING, ParamType::OBJECT, ParamType::OBJECT });
        classes.registerMethod("PaginatedForm", "toggle", toggle,
            { ParamType::OBJECT, ParamType::OBJECT, ParamType::OBJECT });

        classes.registerMethod("PaginatedForm", "show", show, {});
        classes.registerMethod("PaginatedForm", "show", show, { ParamType::FUNCTION });

        classes.registerMethod("PaginatedForm", "close", close, {});
        classes.registerMethod("PaginatedForm", "isShowing", isShowing, {});
    }
}

REGISTER_CALLBACK(PaginatedForm, PaginatedFormClass::registerClasses)
