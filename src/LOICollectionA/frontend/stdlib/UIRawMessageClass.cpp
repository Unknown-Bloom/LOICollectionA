#include <memory>
#include <ranges>
#include <string>

#include <ll/api/Expected.h>
#include <ll/api/io/Logger.h>
#include <ll/api/io/LoggerRegistry.h>
#include <ll/api/ui/base/UIRawMessage.h>

#include "LOICollectionA/frontend/Callback.h"

#include "LOICollectionA/frontend/stdlib/UIRawMessageClass.h"

using namespace LOICollection::frontend;

namespace UIRawMessageClass {
    ll::Expected<ObjectRef> text(const CallbackTypeValues& args) {
        auto handle = std::make_shared<UIRawMessageHandle>();
        handle->base = ll::ui::UIRawMessage::text(std::get<std::string>(args[0]));

        auto obj = std::make_shared<Object>();
        obj->className = "UIRawMessage";
        obj->classIndex = -1;
        obj->native = handle;

        return obj;
    }

    ll::Expected<ObjectRef> translate(const CallbackTypeValues& args) {
        auto key = std::get<std::string>(args[0]);
        auto handle = std::make_shared<UIRawMessageHandle>();

        if (args.size() == 1) {
            handle->base = ll::ui::UIRawMessage::translate(key);
        } else {
            auto result = std::visit([&](auto&& arg) -> ll::Expected<void> {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, ArrayRef>) {
                    auto& elements = arg->elements;
                    if (elements.empty())
                        return {};

                    if (!std::ranges::all_of(elements, [](auto&& e) {
                        return std::holds_alternative<std::string>(e);
                    })) {
                        return ll::makeStringError("translate's ArrayRef argument must contain only strings");
                    }

                    auto subs = elements
                        | std::views::transform([](auto&& e) { return std::get<std::string>(e); })
                        | std::ranges::to<std::vector>();
                    
                    handle->base = ll::ui::UIRawMessage::translate(key, std::move(subs));

                    return {};
                } else if constexpr (std::is_same_v<T, ObjectRef>) {
                    if (arg->className == "UIRawMessage") {
                        handle->base = ll::ui::UIRawMessage::translate(key,
                            static_cast<UIRawMessageHandle*>(arg->native.get())->base
                        );

                        return {};
                    }
                }

                return ll::makeStringError("translate only needs one UIRawMessage or ArrayRef<string> parameter");
            }, args[1]);

            if (!result.has_value())
                return ll::Unexpected(result.error());
        }

        auto obj = std::make_shared<Object>();
        obj->className = "UIRawMessage";
        obj->classIndex = -1;
        obj->native = handle;
        return obj;
    }

    ll::Expected<ObjectRef> rawText(const CallbackTypeValues& args) {
        auto handle = std::make_shared<UIRawMessageHandle>();

        auto& elements = std::get<ArrayRef>(args[0])->elements;
        if (elements.empty())
            return {};
        
        if (!std::ranges::all_of(elements, [](auto&& e) {
            return std::holds_alternative<std::string>(e) && std::get<ObjectRef>(e)->className == "UIRawMessage";
        })) {
            return ll::makeStringError("rawText's ArrayRef argument must contain only UIRawMessages");
        }

        auto subs = elements
            | std::views::transform([](auto&& e) { return static_cast<UIRawMessageHandle*>(std::get<ObjectRef>(e)->native.get())->base; })
            | std::ranges::to<std::vector>();
        
        handle->base = ll::ui::UIRawMessage::rawText(subs);

        auto obj = std::make_shared<Object>();
        obj->className = "UIRawMessage";
        obj->classIndex = -1;
        obj->native = handle;

        return obj;
    }

    void registerClasses(const std::string&) {
        ClassCall& classes = ClassCall::getInstance();

        classes.registerClass("UIRawMessage", {});
        classes.registerStaticMethod("UIRawMessage", "text", text, { ParamType::STRING });
        classes.registerStaticMethod("UIRawMessage", "translate", translate, { ParamType::STRING });
        classes.registerStaticMethod("UIRawMessage", "translate", translate, { ParamType::STRING, ParamType::ARRAY });
        classes.registerStaticMethod("UIRawMessage", "translate", translate, { ParamType::STRING, ParamType::OBJECT });
        classes.registerStaticMethod("UIRawMessage", "rawText", rawText, { ParamType::ARRAY });
    }
}

REGISTER_CALLBACK(UIRawMessage, UIRawMessageClass::registerClasses)
