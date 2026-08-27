#include <format>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <algorithm>

#include <ll/api/Expected.h>
#include <ll/api/io/LoggerRegistry.h>
#include <ll/api/ui/base/Observable.h>
#include <ll/api/ui/form/CustomForm.h>
#include <ll/api/ui/form/MessageBox.h>

#include <mc/world/actor/player/Player.h>

#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"
#include "LOICollectionA/frontend/ir/VM.h"

#include "LOICollectionA/frontend/builtin/ui/form/ScriptFormClass.h"

#include "LOICollectionA/include/form/GUIManager.h"
#include "LOICollectionA/include/server/Plugins/form/ShopData.h"
#include "LOICollectionA/include/server/Plugins/ShopPlugin.h"
#include "LOICollectionA/include/server/Plugins/LanguagePlugin.h"

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

using namespace LOICollection::frontend;
using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    struct ShopFormHandle : ScriptFormClass::ScriptFormHandle {
        ShopData data;
        ObjectRef dataObject;

        int itemIndex = -1;
        int resultCode = 0;
        std::string fromId;
        ObjectRef item;
        bool completed = false;

        int page = 1;
        int pageCount = 1;
        int pageSize = 10;

        std::vector<std::shared_ptr<ll::ui::ObservableString>> labels;
        std::vector<std::shared_ptr<ll::ui::ObservableBoolean>> visible;
        std::shared_ptr<ll::ui::ObservableString> pageIndicator;
        std::shared_ptr<ll::ui::ObservableString> input;
        std::shared_ptr<ll::ui::ObservableBoolean> previousVisible;
        std::shared_ptr<ll::ui::ObservableBoolean> nextVisible;
        std::shared_ptr<ll::ui::ObservableBoolean> chooseVisible;

        std::optional<ll::ui::TextValue> previousText;
        std::optional<ll::ui::TextValue> nextText;
        std::optional<ll::ui::TextValue> chooseText;
        std::optional<ll::ui::TextValue> choosePlaceholder;

        bool previousAdded = false;
        bool nextAdded = false;
        bool chooseAdded = false;
        bool closeButtonAdded = false;
    };

    std::string shopFormGetLanguage(Player& player) {
        return LanguagePlugin::getShared()->getLanguage(player).value_or("zh_CN");
    }

    void refreshPage(ShopFormHandle* handle) {
        const int begin = (handle->page - 1) * handle->pageSize;
        const int end = std::min(begin + handle->pageSize, static_cast<int>(handle->data.items.size()));

        for (int i = 0; i < handle->pageSize; ++i) {
            if (begin + i < end) {
                handle->labels[static_cast<size_t>(i)]->setData(handle->data.items[begin + static_cast<size_t>(i)].title);
                handle->visible[static_cast<size_t>(i)]->setData(true);
            } else {
                handle->labels[static_cast<size_t>(i)]->setData("");
                handle->visible[static_cast<size_t>(i)]->setData(false);
            }
        }

        handle->pageIndicator->setData(std::format("{} / {}", handle->page, handle->pageCount));
        handle->previousVisible->setData(handle->page > 1);
        handle->nextVisible->setData(handle->page < handle->pageCount);
        handle->chooseVisible->setData(handle->pageCount > 2);
    }

    void finishShopSubflow(const std::shared_ptr<ShopFormHandle>& handle, Player& player) {
        if (!handle->show)
            return;

        frontend::DiagnosticEngine diagnostics;
        frontend::CallbackTypeValues values;
        auto resultObj = handle->makeResult ? handle->makeResult() : nullptr;
        values.emplace_back(resultObj ? TypedValue(resultObj) : TypedValue{});

        [[maybe_unused]] auto cbResult = frontend::ir::VM::callFunctionRef(
            handle->show, values,
            frontend::Context::withScriptId(frontend::Context{ std::ref(player) }.params, handle->scriptId),
            diagnostics
        );

        if (diagnostics.hasErrors()) {
            if (auto logger = ShopPlugin::getShared()->getLogger())
                logger->error("ShopForm::show callback: {}", diagnostics.getErrorMessage());
        }
    }

    void startCommoditySubflow(const std::shared_ptr<ShopFormHandle>& handle, Player& player, const ShopItemData& item) {
        auto form = std::make_shared<ll::ui::CustomForm>(player, handle->data.title);
        auto count = std::make_shared<ll::ui::ObservableString>("1", ll::ui::ObservableOptions{ true });

        form->label(item.introduce, ll::ui::TextOptions{});
        form->textField(
            item.number, *count,
            ll::ui::TextFieldOptions{ ll::ui::TextValue(item.number), std::nullopt, std::nullopt }
        );

        std::string language = shopFormGetLanguage(player);
        form->button(
            tr(language, "generic.gui.save"),
            [handle, form, count, player = std::ref(player), item]() mutable -> void {
                int number = SystemUtils::toInt(count->getData(), 0);
                ShopType type = handle->data.type == "sell" ? ShopType::sell : ShopType::buy;

                auto result = ShopPlugin::getShared()->commodity(player.get(), number, item, type);
                if (!result.has_value()) {
                    if (auto logger = ShopPlugin::getShared()->getLogger())
                        logger->error("ShopForm::commodity: {}", result.error().message());
                    handle->resultCode = -1;
                } else {
                    handle->resultCode = static_cast<int>(result.value());
                }

                handle->completed = true;
                [[maybe_unused]] auto closeResult = form->close();
            },
            ll::ui::ButtonOptions{}
        );
        form->closeButton();

        [[maybe_unused]] auto showResult = form->show([handle, form, player = std::ref(player)](ll::ui::ScreenSession::Result) mutable -> void {
            if (!handle->completed)
                handle->resultCode = -1;

            finishShopSubflow(handle, player.get());
        });
    }

    void startTitleSubflow(const std::shared_ptr<ShopFormHandle>& handle, Player& player, const ShopItemData& item) {
        auto box = std::make_shared<ll::ui::MessageBox>(player, handle->data.title);
        box->body(item.introduce);
        box->button1(item.confirmButton);
        box->button2(item.cancelButton);

        [[maybe_unused]] auto showResult = box->show([handle, box, player = std::ref(player), item](ll::ui::MessageBox::Result result) mutable -> void {
            if (!result->selection || *result->selection != 0) {
                handle->resultCode = -1;
                handle->completed = false;
            } else {
                ShopType type = handle->data.type == "sell" ? ShopType::sell : ShopType::buy;

                auto action = ShopPlugin::getShared()->title(player.get(), item, type);
                if (!action.has_value()) {
                    if (auto logger = ShopPlugin::getShared()->getLogger())
                        logger->error("ShopForm::title: {}", action.error().message());
                    handle->resultCode = -1;
                } else {
                    handle->resultCode = static_cast<int>(action.value());
                }

                handle->completed = true;
            }

            finishShopSubflow(handle, player.get());
        });
    }

    ObjectRef makeShopFormResult(const std::shared_ptr<ShopFormHandle>& handle) {
        auto obj = std::make_shared<Object>();
        obj->className = "ShopFormResult";
        obj->classIndex = -1;
            obj->fields["closeReason"] = !handle->fromId.empty() ? 2 : (handle->completed ? 1 : 0);
            obj->fields["resultCode"] = handle->resultCode;
            obj->fields["shop"] = handle->dataObject ? TypedValue(handle->dataObject) : TypedValue{};
            obj->fields["itemIndex"] = handle->itemIndex;
        obj->fields["item"] = handle->item ? TypedValue(handle->item) : TypedValue{};
        obj->fields["fromId"] = handle->fromId;
        return obj;
    }

    void addPagingButtons(const std::shared_ptr<ShopFormHandle>& handle, const std::string& language) {
        if (!handle->previousAdded) {
            handle->previousText = ll::ui::TextValue(tr(language, "generic.gui.page.previous"));
            handle->base->button(
                *handle->previousText,
                [handle]() mutable -> void {
                    if (handle->page > 1) {
                        handle->page -= 1;
                        
                        refreshPage(handle.get());
                    }
                },
                ll::ui::ButtonOptions{ std::nullopt, std::nullopt, *handle->previousVisible }
            );
            handle->previousAdded = true;
        }

        if (!handle->nextAdded) {
            handle->nextText = ll::ui::TextValue(tr(language, "generic.gui.page.next"));
            handle->base->button(
                *handle->nextText,
                [handle]() mutable -> void {
                    if (handle->page < handle->pageCount) {
                        handle->page += 1;
                        refreshPage(handle.get());
                    }
                },
                ll::ui::ButtonOptions{ std::nullopt, std::nullopt, *handle->nextVisible }
            );
            handle->nextAdded = true;
        }

        if (!handle->chooseAdded) {
            handle->chooseText = ll::ui::TextValue(tr(language, "generic.gui.page.choose"));
            handle->choosePlaceholder = ll::ui::TextValue(tr(language, "generic.gui.page.choose.input"));

            handle->base->textField(
                "", *handle->input,
                ll::ui::TextFieldOptions{ *handle->choosePlaceholder, std::nullopt, *handle->chooseVisible }
            );
            handle->base->button(
                *handle->chooseText,
                [handle]() mutable -> void {
                    int page = SystemUtils::toInt(handle->input->getData(), 0);
                    if (page >= 1 && page <= handle->pageCount) {
                        handle->page = page;
                        refreshPage(handle.get());
                    }
                },
                ll::ui::ButtonOptions{ std::nullopt, std::nullopt, *handle->chooseVisible }
            );
            handle->chooseAdded = true;
        }
    }

    ObjectRef makeShopForm(const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        auto id = std::get<std::string>(args[0]);
        auto dataObject = std::get<ObjectRef>(args[1]);
        auto& player = std::any_cast<std::reference_wrapper<Player>>(placeholders.at(0)).get();

        auto handle = std::make_shared<ShopFormHandle>();
        handle->scriptId = Context::scriptIdOf(placeholders);
        handle->data = hydrateShopData(dataObject);
        handle->dataObject = dataObject;
        handle->pageCount = std::max(1, (static_cast<int>(handle->data.items.size()) + handle->pageSize - 1) / handle->pageSize);

        handle->labels.reserve(static_cast<size_t>(handle->pageSize));
        handle->visible.reserve(static_cast<size_t>(handle->pageSize));
        for (int i = 0; i < handle->pageSize; ++i) {
            handle->labels.push_back(std::make_shared<ll::ui::ObservableString>(""));
            handle->visible.push_back(std::make_shared<ll::ui::ObservableBoolean>(false));
        }

        handle->pageIndicator = std::make_shared<ll::ui::ObservableString>("");
        handle->input = std::make_shared<ll::ui::ObservableString>("", ll::ui::ObservableOptions{ true });
        handle->previousVisible = std::make_shared<ll::ui::ObservableBoolean>(false);
        handle->nextVisible = std::make_shared<ll::ui::ObservableBoolean>(false);
        handle->chooseVisible = std::make_shared<ll::ui::ObservableBoolean>(false);

        handle->base = std::make_unique<ll::ui::CustomForm>(player, handle->data.title);
        handle->base->label(*handle->pageIndicator);
        if (!handle->data.content.empty())
            handle->base->label(handle->data.content, ll::ui::TextOptions{});

        for (int i = 0; i < handle->pageSize; ++i) {
            handle->base->button(
                *handle->labels[static_cast<size_t>(i)],
                [handle, i]() mutable -> void {
                    const int index = (handle->page - 1) * handle->pageSize + i;
                    if (index < 0 || index >= static_cast<int>(handle->data.items.size()))
                        return;

                    const auto& item = handle->data.items.at(static_cast<size_t>(index));
                    handle->itemIndex = index;
                    handle->item = makeShopItemDataObject(item);
                    handle->completed = false;
                    handle->resultCode = 0;
                    handle->fromId.clear();

                    if (item.type == "from") {
                        handle->fromId = item.id;
                        handle->completed = true;
                    } else if (item.type == "commodity" || item.type == "title") {
                        handle->pendingSubflow = true;
                    }

                    [[maybe_unused]] auto closeResult = handle->base->close();
                },
                ll::ui::ButtonOptions{ std::nullopt, std::nullopt, *handle->visible[static_cast<size_t>(i)] }
            );
        }

        refreshPage(handle.get());

        handle->makeResult = [handle]() -> ObjectRef {
            return makeShopFormResult(handle);
        };
        handle->onClosed = [handle](Player& player) mutable -> void {
            if (!handle->fromId.empty()) {
                auto result = ShopPlugin::getShared()->open(player, handle->fromId);
                if (!result.has_value() && ShopPlugin::getShared()->getLogger())
                    ShopPlugin::getShared()->getLogger()->error("ShopForm::open: {}", result.error().message());
                return;
            }

            if (handle->pendingSubflow && handle->itemIndex >= 0 && handle->itemIndex < static_cast<int>(handle->data.items.size())) {
                const auto& item = handle->data.items.at(static_cast<size_t>(handle->itemIndex));
                if (item.type == "commodity")
                    startCommoditySubflow(handle, player, item);
                else if (item.type == "title")
                    startTitleSubflow(handle, player, item);
            }
        };

        form::GUIManager::getInstance().registerScriptFormUI(id, handle, player);

        auto obj = std::make_shared<Object>();
        obj->className = "ShopForm";
        obj->classIndex = -1;
        obj->native = handle;
        return obj;
    }

    ll::Expected<TypedValue> shopFormShow(const ObjectRef& self, const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        auto handle = std::static_pointer_cast<ShopFormHandle>(self->native);
        if (!args.empty())
            handle->show = std::get<FunctionRefPtr>(args[0]);

        auto& player = std::any_cast<std::reference_wrapper<Player>>(placeholders.at(0)).get();
        addPagingButtons(handle, shopFormGetLanguage(player));

        if (!handle->closeButtonAdded && handle->base) {
            handle->base->closeButton();
            handle->closeButtonAdded = true;
        }

        return self;
    }

    ll::Expected<TypedValue> shopFormLabel(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* handle = static_cast<ShopFormHandle*>(self->native.get());
        if (handle->base)
            handle->base->label(std::get<std::string>(args[0]), ll::ui::TextOptions{});

        return self;
    }

    ll::Expected<TypedValue> shopFormDivider(const ObjectRef& self, const CallbackTypeValues&) {
        auto* handle = static_cast<ShopFormHandle*>(self->native.get());
        if (handle->base)
            handle->base->divider(ll::ui::DividerOptions{});

        return self;
    }

    ll::Expected<TypedValue> shopFormPreviousButton(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* handle = static_cast<ShopFormHandle*>(self->native.get());
        if (!handle->previousAdded && handle->base) {
            handle->previousText = ll::ui::TextValue(std::get<std::string>(args[0]));
            handle->base->button(
                *handle->previousText,
                [handle]() mutable -> void {
                    if (handle->page > 1) {
                        handle->page -= 1;
                        refreshPage(handle);
                    }
                },
                ll::ui::ButtonOptions{ std::nullopt, std::nullopt, *handle->previousVisible }
            );
            handle->previousAdded = true;
        }

        return self;
    }

    ll::Expected<TypedValue> shopFormNextButton(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* handle = static_cast<ShopFormHandle*>(self->native.get());
        if (!handle->nextAdded && handle->base) {
            handle->nextText = ll::ui::TextValue(std::get<std::string>(args[0]));
            handle->base->button(
                *handle->nextText,
                [handle]() mutable -> void {
                    if (handle->page < handle->pageCount) {
                        handle->page += 1;
                        refreshPage(handle);
                    }
                },
                ll::ui::ButtonOptions{ std::nullopt, std::nullopt, *handle->nextVisible }
            );
            handle->nextAdded = true;
        }

        return self;
    }

    ll::Expected<TypedValue> shopFormChooseButton(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* handle = static_cast<ShopFormHandle*>(self->native.get());
        if (!handle->chooseAdded && handle->base) {
            handle->chooseText = ll::ui::TextValue(std::get<std::string>(args[0]));
            handle->choosePlaceholder = ll::ui::TextValue(std::get<std::string>(args[1]));
            handle->base->textField(
                "", *handle->input,
                ll::ui::TextFieldOptions{ *handle->choosePlaceholder, std::nullopt, *handle->chooseVisible }
            );
            handle->base->button(
                *handle->chooseText,
                [handle]() mutable -> void {
                    int page = SystemUtils::toInt(handle->input->getData(), 0);
                    if (page >= 1 && page <= handle->pageCount) {
                        handle->page = page;
                        refreshPage(handle);
                    }
                },
                ll::ui::ButtonOptions{ std::nullopt, std::nullopt, *handle->chooseVisible }
            );
            handle->chooseAdded = true;
        }

        return self;
    }

    ll::Expected<TypedValue> shopFormCloseButton(const ObjectRef& self, const CallbackTypeValues&) {
        auto* handle = static_cast<ShopFormHandle*>(self->native.get());
        if (!handle->closeButtonAdded && handle->base) {
            handle->base->closeButton();
            handle->closeButtonAdded = true;
        }

        return self;
    }

    void registerShopFormClasses(const std::string&) {
        ClassCall& classes = ClassCall::getInstance();

        classes.registerClass("ShopForm", {});
        classes.registerConstructor("ShopForm", makeShopForm, { ParamType::STRING, ParamType::OBJECT });

        classes.registerMethod("ShopForm", "show", shopFormShow, { ParamType::FUNCTION });
        classes.registerMethod("ShopForm", "show", shopFormShow, {});
        classes.registerMethod("ShopForm", "label", shopFormLabel, { ParamType::STRING, ParamType::OBJECT });
        classes.registerMethod("ShopForm", "divider", shopFormDivider, { ParamType::OBJECT });
        classes.registerMethod("ShopForm", "previousButton", shopFormPreviousButton, { ParamType::STRING });
        classes.registerMethod("ShopForm", "nextButton", shopFormNextButton, { ParamType::STRING });
        classes.registerMethod("ShopForm", "chooseButton", shopFormChooseButton, { ParamType::STRING, ParamType::STRING });
        classes.registerMethod("ShopForm", "closeButton", shopFormCloseButton, {});
    }
}

REGISTER_CALLBACK(ShopForm, LOICollection::server::Plugins::registerShopFormClasses)
