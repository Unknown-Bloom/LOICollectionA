#pragma once

/* Fake native GUI classes for frontend tests.
 *
 * The form classes journal every method call so tests can assert the exact
 * sequence a script produced; the option/observable classes mirror the
 * shapes the shipped .lcui scripts rely on. Registration targets the
 * ClassCall singleton and is idempotent, so each test may re-register. */

#include <string>
#include <utility>
#include <vector>

#include "LOICollectionA/frontend/Callback.h"

namespace LOICollection::frontend::test {
    using namespace LOICollection::frontend;

    /* Journal of form method calls, in order. */
    inline std::vector<std::string>& formCalls() {
        static std::vector<std::string> calls;
        return calls;
    }

    inline ll::Expected<ObjectRef> makeNativeObject(const std::string& className) {
        auto obj = std::make_shared<Object>();
        obj->className = className;
        return obj;
    }

    /* A method whose only observable behaviour is the journal entry. */
    inline NativeMethod loggedMethod(std::string name) {
        return [name = std::move(name)](const ObjectRef&, const CallbackTypeValues&) -> ll::Expected<TypedValue> {
            formCalls().push_back(name);
            return std::string("");
        };
    }

    inline void registerSimpleClass(const std::string& name, const std::vector<std::string>& fields = {}) {
        auto& cc = ClassCall::getInstance();

        cc.registerClass(name, fields);
        cc.registerConstructor(name, [name](const CallbackTypeValues&) -> ll::Expected<ObjectRef> {
            return makeNativeObject(name);
        }, {});
    }

    inline void registerGuiTestEnvironment() {
        auto& cc = ClassCall::getInstance();

        registerSimpleClass("TextOptions");
        registerSimpleClass("SpacingOptions");
        registerSimpleClass("DividerOptions");
        registerSimpleClass("DropdownOptions");
        registerSimpleClass("ButtonOptions", { "tooltip" });
        registerSimpleClass("TextFieldOptions", { "description" });
        registerSimpleClass("MessageBoxResult");

        cc.registerClass("ObservableString", {});
        cc.registerConstructor("ObservableString", [](const CallbackTypeValues&) -> ll::Expected<ObjectRef> {
            return makeNativeObject("ObservableString");
        }, { ParamType::STRING, ParamType::BOOL });
        cc.registerMethod("ObservableString", "getData", loggedMethod("getData"), {});

        cc.registerClass("ObservableNumber", {});
        cc.registerConstructor("ObservableNumber", [](const CallbackTypeValues&) -> ll::Expected<ObjectRef> {
            return makeNativeObject("ObservableNumber");
        }, { ParamType::FLOAT, ParamType::BOOL });
        cc.registerMethod("ObservableNumber", "getData", loggedMethod("getData"), {});

        using Signature = std::pair<std::string, CallbackTypeArgs>;

        cc.registerClass("CustomForm", { "handler" });
        cc.registerConstructor("CustomForm", [](const CallbackTypeValues&) -> ll::Expected<ObjectRef> {
            return makeNativeObject("CustomForm");
        }, { ParamType::STRING, ParamType::STRING });
        for (auto& [name, args] : std::vector<Signature>{
            { "label", { ParamType::STRING, ParamType::OBJECT } },
            { "spacer", { ParamType::OBJECT } },
            { "divider", { ParamType::OBJECT } },
            { "textField", { ParamType::STRING, ParamType::OBJECT, ParamType::OBJECT } },
            { "dropdown", { ParamType::STRING, ParamType::OBJECT, ParamType::ARRAY, ParamType::OBJECT } },
            { "closeButton", {} },
            { "close", {} },
            { "show", { ParamType::FUNCTION } },
            { "show", {} },
        })
            cc.registerMethod("CustomForm", name, loggedMethod(name), args);

        /* `button` additionally keeps its handler so a test can fire it
         * afterwards and observe what the closure does. */
        cc.registerMethod("CustomForm", "button",
            [](const ObjectRef& self, const CallbackTypeValues& args) -> ll::Expected<TypedValue> {
                formCalls().push_back("button");
                if (args.size() >= 2)
                    self->fields["handler"] = args[1];

                return std::string("");
            }, { ParamType::STRING, ParamType::FUNCTION, ParamType::OBJECT });

        cc.registerClass("PaginatedForm", {});
        cc.registerConstructor("PaginatedForm", [](const CallbackTypeValues&) -> ll::Expected<ObjectRef> {
            return makeNativeObject("PaginatedForm");
        }, { ParamType::STRING, ParamType::STRING, ParamType::ARRAY, ParamType::INT });
        for (auto& [name, args] : std::vector<Signature>{
            { "label", { ParamType::STRING, ParamType::OBJECT } },
            { "previousButton", { ParamType::STRING } },
            { "nextButton", { ParamType::STRING } },
            { "chooseButton", { ParamType::STRING, ParamType::STRING } },
            { "closeButton", {} },
            { "close", {} },
            { "show", { ParamType::FUNCTION } },
            { "show", {} },
        })
            cc.registerMethod("PaginatedForm", name, loggedMethod(name), args);

        cc.registerClass("MessageBox", {});
        cc.registerConstructor("MessageBox", [](const CallbackTypeValues&) -> ll::Expected<ObjectRef> {
            return makeNativeObject("MessageBox");
        }, { ParamType::STRING, ParamType::STRING });
        for (auto& [name, args] : std::vector<Signature>{
            { "body", { ParamType::STRING } },
            { "button1", { ParamType::STRING } },
            { "button2", { ParamType::STRING } },
            { "close", {} },
            { "show", { ParamType::FUNCTION } },
        })
            cc.registerMethod("MessageBox", name, loggedMethod(name), args);
    }
}
