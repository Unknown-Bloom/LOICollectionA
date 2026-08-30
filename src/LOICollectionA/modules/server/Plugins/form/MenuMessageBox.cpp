#include <any>
#include <memory>
#include <string>
#include <optional>

#include <ll/api/Expected.h>
#include <ll/api/ui/form/MessageBox.h>

#include <mc/world/actor/player/Player.h>

#include "LOICollectionA/frontend/Callback.h"

#include "LOICollectionA/frontend/builtin/ui/form/ScriptFormClass.h"

#include "LOICollectionA/include/form/GUIManager.h"
#include "LOICollectionA/include/server/Plugins/form/MenuData.h"

#include "LOICollectionA/utils/mc-server/ScoreboardUtils.h"

using namespace LOICollection::frontend;

namespace LOICollection::server::Plugins {
    struct MenuMessageBoxHandle : ScriptFormClass::ScriptFormHandle {
        ObjectRef action1;
        ObjectRef action2;
        ObjectRef resultAction;
        int selection = -1;
        int closeReason = 0;
    };

    ObjectRef makeMenuMessageBoxResult(const std::shared_ptr<MenuMessageBoxHandle>& handle) {
        auto obj = std::make_shared<Object>();
        obj->className = "MenuMessageBoxResult";
        obj->classIndex = -1;
        obj->assign("closeReason", handle->closeReason);
        obj->assign("selection", handle->selection);
        obj->assign("action", handle->resultAction ? TypedValue(handle->resultAction) : TypedValue{});
        return obj;
    }

    void checkAction(MenuMessageBoxHandle& handle, Player& player, const ObjectRef& actionObject) {
        auto action = hydrateMenuItem(actionObject);
        handle.resultAction = actionObject;

        if (static_cast<int>(player.getCommandPermissionLevel()) < action.permission) {
            handle.closeReason = 2;
            return;
        }

        for (const auto& score : action.scores) {
            if (score.value > ScoreboardUtils::getScore(player, score.objective)) {
                handle.closeReason = 3;
                return;
            }
        }

        handle.closeReason = 1;
    }

    ll::Expected<ObjectRef> makeMenuMessageBox(const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
        auto id = std::get<std::string>(args[0]);
        auto title = std::get<std::string>(args[1]);
        auto& player = std::any_cast<std::reference_wrapper<Player>>(placeholders.at(0)).get();

        auto handle = std::make_shared<MenuMessageBoxHandle>();
        handle->box = std::make_unique<ll::ui::MessageBox>(player, title);
        handle->makeResult = [handle]() -> ObjectRef {
            return makeMenuMessageBoxResult(handle);
        };
        handle->onBoxResult = [handle, player = std::ref(player)](const ll::ui::MessageBox::Result& result) mutable -> void {
            handle->selection = result->selection ? static_cast<int>(result->selection.value()) : -1;

            if (handle->selection == 0 && handle->action1)
                checkAction(*handle, player.get(), handle->action1);
            else if (handle->selection == 1 && handle->action2)
                checkAction(*handle, player.get(), handle->action2);
            else
                handle->closeReason = 0;
        };

        form::GUIManager::getInstance().registerScriptFormUI(id, handle, player);

        auto obj = std::make_shared<Object>();
        obj->className = "MenuMessageBox";
        obj->classIndex = -1;
        obj->native = handle;
        return obj;
    }

    ll::Expected<TypedValue> menuMessageBoxShow(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* handle = static_cast<MenuMessageBoxHandle*>(self->native.get());
        if (!args.empty())
            handle->show = std::get<FunctionRefPtr>(args[0]);
        return self;
    }

    ll::Expected<TypedValue> menuMessageBoxBody(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* handle = static_cast<MenuMessageBoxHandle*>(self->native.get());
        handle->box->body(std::get<std::string>(args[0]));
        return self;
    }

    ll::Expected<TypedValue> menuMessageBoxButton1(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* handle = static_cast<MenuMessageBoxHandle*>(self->native.get());
        std::string title = std::get<std::string>(args[0]);

        if (args.size() >= 2 && std::holds_alternative<ObjectRef>(args[1])) {
            handle->action1 = std::get<ObjectRef>(args[1]);
            if (args.size() >= 3)
                handle->box->button1(title, std::get<std::string>(args[2]));
            else
                handle->box->button1(title);
        } else if (args.size() >= 2) {
            handle->action1.reset();
            handle->box->button1(title, std::get<std::string>(args[1]));
        } else {
            handle->action1.reset();
            handle->box->button1(title);
        }

        return self;
    }

    ll::Expected<TypedValue> menuMessageBoxButton2(const ObjectRef& self, const CallbackTypeValues& args) {
        auto* handle = static_cast<MenuMessageBoxHandle*>(self->native.get());
        std::string title = std::get<std::string>(args[0]);

        if (args.size() >= 2 && std::holds_alternative<ObjectRef>(args[1])) {
            handle->action2 = std::get<ObjectRef>(args[1]);
            if (args.size() >= 3)
                handle->box->button2(title, std::get<std::string>(args[2]));
            else
                handle->box->button2(title);
        } else if (args.size() >= 2) {
            handle->action2.reset();
            handle->box->button2(title, std::get<std::string>(args[1]));
        } else {
            handle->action2.reset();
            handle->box->button2(title);
        }

        return self;
    }

    void registerMenuMessageBoxClasses(const std::string&) {
        ClassCall& classes = ClassCall::getInstance();

        classes.registerClass("MenuMessageBox", {});
        classes.registerConstructor("MenuMessageBox", makeMenuMessageBox, { ParamType::STRING, ParamType::STRING });

        classes.registerMethod("MenuMessageBox", "show", menuMessageBoxShow, { ParamType::FUNCTION });
        classes.registerMethod("MenuMessageBox", "show", menuMessageBoxShow, {});
        classes.registerMethod("MenuMessageBox", "body", menuMessageBoxBody, { ParamType::STRING });
        classes.registerMethod("MenuMessageBox", "button1", menuMessageBoxButton1, { ParamType::STRING });
        classes.registerMethod("MenuMessageBox", "button1", menuMessageBoxButton1, { ParamType::STRING, ParamType::OBJECT });
        classes.registerMethod("MenuMessageBox", "button1", menuMessageBoxButton1, { ParamType::STRING, ParamType::STRING });
        classes.registerMethod("MenuMessageBox", "button1", menuMessageBoxButton1, { ParamType::STRING, ParamType::OBJECT, ParamType::STRING });
        classes.registerMethod("MenuMessageBox", "button2", menuMessageBoxButton2, { ParamType::STRING });
        classes.registerMethod("MenuMessageBox", "button2", menuMessageBoxButton2, { ParamType::STRING, ParamType::OBJECT });
        classes.registerMethod("MenuMessageBox", "button2", menuMessageBoxButton2, { ParamType::STRING, ParamType::STRING });
        classes.registerMethod("MenuMessageBox", "button2", menuMessageBoxButton2, { ParamType::STRING, ParamType::OBJECT, ParamType::STRING });
    }
}

REGISTER_CALLBACK(MenuMessageBox, LOICollection::server::Plugins::registerMenuMessageBoxClasses)
