#include <any>
#include <memory>
#include <string>
#include <vector>

#include <ll/api/Expected.h>
#include <ll/api/io/LoggerRegistry.h>
#include <ll/api/ui/form/CustomForm.h>

#include <mc/world/actor/player/Player.h>

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/ir/VM.h"

#include "LOICollectionA/frontend/builtin/ui/form/ScriptFormClass.h"
#include "LOICollectionA/frontend/builtin/ui/form/CustomFormOptionsClass.h"
#include "LOICollectionA/frontend/stdlib/ObservableBooleanClass.h"
#include "LOICollectionA/frontend/stdlib/ObservableNumberClass.h"
#include "LOICollectionA/frontend/stdlib/ObservableStringClass.h"

#include "LOICollectionA/include/form/GUIManager.h"
#include "LOICollectionA/include/server/Plugins/form/MenuData.h"

#include "LOICollectionA/utils/mc-server/ScoreboardUtils.h"

using namespace LOICollection::frontend;

namespace LOICollection::server::Plugins {
    struct MenuFormHandle : ScriptFormClass::ScriptFormHandle {
        ObjectRef action;
        int actionIndex = -1;
        int nextActionIndex = 0;
        int closeReason = 0;
        bool closeButtonAdded = false;
    };

    ObjectRef makeMenuFormResult(const std::shared_ptr<MenuFormHandle>& handle) {
        auto obj = std::make_shared<Object>();
        obj->className = "MenuFormResult";
        obj->classIndex = -1;
        obj->fields["closeReason"] = handle->closeReason;
        obj->fields["actionIndex"] = handle->actionIndex;
        obj->fields["action"] = handle->action ? TypedValue(handle->action) : TypedValue{};
        return obj;
    }

    ll::Expected<ObjectRef> makeMenuForm(const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        auto id = std::get<std::string>(args[0]);
        auto title = CustomFormOptionsClass::toTextValue(args[1]);
        if (!title.has_value())
            return ll::Unexpected(title.error());

        auto& player = std::any_cast<std::reference_wrapper<Player>>(placeholders.at(0)).get();

        auto handle = std::make_shared<MenuFormHandle>();
        handle->base = std::make_unique<ll::ui::CustomForm>(player, *title);
        handle->makeResult = [handle]() -> ObjectRef {
            return makeMenuFormResult(handle);
        };

        form::GUIManager::getInstance().registerScriptFormUI(id, handle, player);

        auto obj = std::make_shared<Object>();
        obj->className = "MenuForm";
        obj->classIndex = -1;
        obj->native = handle;
        return obj;
    }

    ll::Expected<TypedValue> menuFormShow(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* handle = static_cast<MenuFormHandle*>(self->native.get());
        if (!args.empty())
            handle->show = std::get<FunctionRefPtr>(args[0]);
        return self;
    }

    ll::Expected<TypedValue> menuFormHeader(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* handle = static_cast<MenuFormHandle*>(self->native.get());
        auto text = CustomFormOptionsClass::toTextValue(args[0]);
        auto options = CustomFormOptionsClass::readVisibleOptions<ll::ui::TextOptions>(
            std::get<ObjectRef>(args[1]), "TextOptions");
        if (!text.has_value())
            return ll::Unexpected(text.error());
        if (!options.has_value())
            return ll::Unexpected(options.error());
        handle->base->header(*text, *options);
        return self;
    }

    ll::Expected<TypedValue> menuFormLabel(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* handle = static_cast<MenuFormHandle*>(self->native.get());
        auto text = CustomFormOptionsClass::toTextValue(args[0]);
        auto options = CustomFormOptionsClass::readVisibleOptions<ll::ui::TextOptions>(
            std::get<ObjectRef>(args[1]), "TextOptions");
        if (!text.has_value())
            return ll::Unexpected(text.error());
        if (!options.has_value())
            return ll::Unexpected(options.error());
        handle->base->label(*text, *options);
        return self;
    }

    ll::Expected<TypedValue> menuFormDivider(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* handle = static_cast<MenuFormHandle*>(self->native.get());
        auto options = CustomFormOptionsClass::readVisibleOptions<ll::ui::DividerOptions>(
            std::get<ObjectRef>(args[0]), "DividerOptions");
        if (!options.has_value())
            return ll::Unexpected(options.error());
        handle->base->divider(*options);
        return self;
    }

    ll::Expected<TypedValue> menuFormSpacer(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* handle = static_cast<MenuFormHandle*>(self->native.get());
        auto options = CustomFormOptionsClass::readVisibleOptions<ll::ui::SpacingOptions>(
            std::get<ObjectRef>(args[0]), "SpacingOptions");
        if (!options.has_value())
            return ll::Unexpected(options.error());
        handle->base->spacer(*options);
        return self;
    }

    ll::Expected<TypedValue> menuFormTextField(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* handle = static_cast<MenuFormHandle*>(self->native.get());
        auto label = CustomFormOptionsClass::toTextValue(args[0]);
        auto observable = CustomFormOptionsClass::getObservable<
            ObservableStringClass::ObservableStringHandle, ll::ui::ObservableString>(
                std::get<ObjectRef>(args[1]), "ObservableString");
        auto options = CustomFormOptionsClass::readDescriptionOptions<ll::ui::TextFieldOptions>(
            std::get<ObjectRef>(args[2]), "TextFieldOptions");
        if (!label.has_value())
            return ll::Unexpected(label.error());
        if (!observable.has_value())
            return ll::Unexpected(observable.error());
        if (!options.has_value())
            return ll::Unexpected(options.error());
        handle->base->textField(*label, observable->get(), *options);
        return self;
    }

    ll::Expected<TypedValue> menuFormDropdown(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* handle = static_cast<MenuFormHandle*>(self->native.get());
        auto label = CustomFormOptionsClass::toTextValue(args[0]);
        auto observable = CustomFormOptionsClass::getObservable<
            ObservableNumberClass::ObservableNumberHandle, ll::ui::ObservableNumber>(
                std::get<ObjectRef>(args[1]), "ObservableNumber");
        auto options = CustomFormOptionsClass::readDescriptionOptions<ll::ui::DropdownOptions>(
            std::get<ObjectRef>(args[3]), "DropdownOptions");
        if (!label.has_value())
            return ll::Unexpected(label.error());
        if (!observable.has_value())
            return ll::Unexpected(observable.error());
        if (!options.has_value())
            return ll::Unexpected(options.error());

        std::vector<ll::ui::DropdownItemData> items;
        for (const auto& element : std::get<ArrayRef>(args[2])->elements) {
            auto item = CustomFormOptionsClass::toDropdownItem(std::get<ObjectRef>(element));
            if (!item.has_value())
                return ll::Unexpected(item.error());
            items.push_back(std::move(*item));
        }

        handle->base->dropdown(*label, observable->get(), std::move(items), *options);
        return self;
    }

    ll::Expected<TypedValue> menuFormToggle(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* handle = static_cast<MenuFormHandle*>(self->native.get());
        auto label = CustomFormOptionsClass::toTextValue(args[0]);
        auto observable = CustomFormOptionsClass::getObservable<
            ObservableBooleanClass::ObservableBooleanHandle, ll::ui::ObservableBoolean>(
                std::get<ObjectRef>(args[1]), "ObservableBoolean");
        auto options = CustomFormOptionsClass::readDescriptionOptions<ll::ui::ToggleOptions>(
            std::get<ObjectRef>(args[2]), "ToggleOptions");
        if (!label.has_value())
            return ll::Unexpected(label.error());
        if (!observable.has_value())
            return ll::Unexpected(observable.error());
        if (!options.has_value())
            return ll::Unexpected(options.error());
        handle->base->toggle(*label, observable->get(), *options);
        return self;
    }

    ll::Expected<TypedValue> menuFormSlider(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* handle = static_cast<MenuFormHandle*>(self->native.get());
        auto label = CustomFormOptionsClass::toTextValue(args[0]);
        auto observable = CustomFormOptionsClass::getObservable<
            ObservableNumberClass::ObservableNumberHandle, ll::ui::ObservableNumber>(
                std::get<ObjectRef>(args[1]), "ObservableNumber");
        auto min = CustomFormOptionsClass::toNumberValue(args[2]);
        auto max = CustomFormOptionsClass::toNumberValue(args[3]);
        auto options = CustomFormOptionsClass::toSliderOptions(std::get<ObjectRef>(args[4]));
        if (!label.has_value())
            return ll::Unexpected(label.error());
        if (!observable.has_value())
            return ll::Unexpected(observable.error());
        if (!min.has_value())
            return ll::Unexpected(min.error());
        if (!max.has_value())
            return ll::Unexpected(max.error());
        if (!options.has_value())
            return ll::Unexpected(options.error());
        handle->base->slider(*label, observable->get(), *min, *max, *options);
        return self;
    }

    ll::Expected<TypedValue> menuFormButton(
        const ObjectRef& self,
        const CallbackTypeValues& args,
        const CallbackTypePlaces& placeholders
    ) {
        auto* handle = static_cast<MenuFormHandle*>(self->native.get());
        auto label = CustomFormOptionsClass::toTextValue(args[0]);
        auto callback = std::get<FunctionRefPtr>(args[1]);
        auto options = CustomFormOptionsClass::toButtonOptions(std::get<ObjectRef>(args[2]));
        if (!label.has_value())
            return ll::Unexpected(label.error());
        if (!options.has_value())
            return ll::Unexpected(options.error());

        handle->base->button(*label, [callback, placeholders]() -> void {
            frontend::DiagnosticEngine diagnostics;
            [[maybe_unused]] auto result = frontend::ir::VM::callFunctionRef(
                callback, {}, placeholders, diagnostics
            );
            if (diagnostics.hasErrors()) {
                ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA")
                    ->error("MenuForm::button callback: {}", diagnostics.getErrorMessage());
            }
        }, *options);

        return self;
    }

    ll::Expected<TypedValue> menuFormActionButton(
        const ObjectRef& self,
        const CallbackTypeValues& args,
        const CallbackTypePlaces& placeholders
    ) {
        auto* handle = static_cast<MenuFormHandle*>(self->native.get());
        auto label = CustomFormOptionsClass::toTextValue(args[0]);
        auto actionObject = std::get<ObjectRef>(args[1]);
        auto callback = std::get<FunctionRefPtr>(args[2]);
        auto options = CustomFormOptionsClass::toButtonOptions(std::get<ObjectRef>(args[3]));
        if (!label.has_value())
            return ll::Unexpected(label.error());
        if (!options.has_value())
            return ll::Unexpected(options.error());

        auto action = hydrateMenuItem(actionObject);
        int index = handle->nextActionIndex++;

        handle->base->button(*label, [handle, actionObject, action, index, callback, placeholders]() mutable -> void {
            auto& player = std::any_cast<std::reference_wrapper<Player>>(placeholders.at(0)).get();
            handle->action = actionObject;
            handle->actionIndex = index;

            if (static_cast<int>(player.getCommandPermissionLevel()) < action.permission) {
                handle->closeReason = 2;
                [[maybe_unused]] auto closeResult = handle->base->close();
                return;
            }

            for (const auto& score : action.scores) {
                if (score.value > ScoreboardUtils::getScore(player, score.objective)) {
                    handle->closeReason = 3;
                    [[maybe_unused]] auto closeResult = handle->base->close();
                    return;
                }
            }

            handle->closeReason = 1;

            frontend::DiagnosticEngine diagnostics;
            [[maybe_unused]] auto result = frontend::ir::VM::callFunctionRef(
                callback, {}, placeholders, diagnostics
            );
            if (diagnostics.hasErrors()) {
                ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA")
                    ->error("MenuForm::action callback: {}", diagnostics.getErrorMessage());
            }

            [[maybe_unused]] auto closeResult = handle->base->close();
        }, *options);

        return self;
    }

    ll::Expected<TypedValue> menuFormCloseButton(const ObjectRef& self, const CallbackTypeValues&) {
        auto* handle = static_cast<MenuFormHandle*>(self->native.get());
        if (!handle->closeButtonAdded) {
            handle->base->closeButton();
            handle->closeButtonAdded = true;
        }
        return self;
    }

    void registerMenuFormClasses(const std::string&) {
        ClassCall& classes = ClassCall::getInstance();

        classes.registerClass("MenuForm", {});
        classes.registerConstructor("MenuForm", makeMenuForm, { ParamType::STRING, ParamType::STRING });

        classes.registerMethod("MenuForm", "show", menuFormShow, { ParamType::FUNCTION });
        classes.registerMethod("MenuForm", "show", menuFormShow, {});
        classes.registerMethod("MenuForm", "header", menuFormHeader, { ParamType::STRING, ParamType::OBJECT });
        classes.registerMethod("MenuForm", "label", menuFormLabel, { ParamType::STRING, ParamType::OBJECT });
        classes.registerMethod("MenuForm", "divider", menuFormDivider, { ParamType::OBJECT });
        classes.registerMethod("MenuForm", "spacer", menuFormSpacer, { ParamType::OBJECT });
        classes.registerMethod("MenuForm", "textField", menuFormTextField, {
            ParamType::STRING, ParamType::OBJECT, ParamType::OBJECT
        });
        classes.registerMethod("MenuForm", "dropdown", menuFormDropdown, {
            ParamType::STRING, ParamType::OBJECT, ParamType::ARRAY, ParamType::OBJECT
        });
        classes.registerMethod("MenuForm", "toggle", menuFormToggle, {
            ParamType::STRING, ParamType::OBJECT, ParamType::OBJECT
        });
        classes.registerMethod("MenuForm", "slider", menuFormSlider, {
            ParamType::STRING, ParamType::OBJECT, ParamType::INT, ParamType::INT, ParamType::OBJECT
        });
        classes.registerMethod("MenuForm", "slider", menuFormSlider, {
            ParamType::STRING, ParamType::OBJECT, ParamType::FLOAT, ParamType::FLOAT, ParamType::OBJECT
        });
        classes.registerMethod("MenuForm", "button", menuFormButton, {
            ParamType::STRING, ParamType::FUNCTION, ParamType::OBJECT
        });
        classes.registerMethod("MenuForm", "button", menuFormActionButton, {
            ParamType::STRING, ParamType::OBJECT, ParamType::FUNCTION, ParamType::OBJECT
        });
        classes.registerMethod("MenuForm", "closeButton", menuFormCloseButton, {});
    }
}

REGISTER_CALLBACK(MenuForm, LOICollection::server::Plugins::registerMenuFormClasses)
