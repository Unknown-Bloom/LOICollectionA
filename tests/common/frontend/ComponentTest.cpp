#include <gtest/gtest.h>

#include <string>

#include "common/frontend/CommonTest.h"

#include "LOICollectionA/frontend/Callback.h"

using namespace LOICollection::frontend;

namespace {
    void registerFormStubs() {
        static const bool registered = [] {
            ClassCall& classes = ClassCall::getInstance();

            classes.registerClass("CustomForm", {});
            classes.registerConstructor(
                "CustomForm",
                [](const CallbackTypeValues&) -> ll::Expected<ObjectRef> { return nullptr; },
                { ParamType::STRING, ParamType::STRING }
            );
            classes.registerConstructor(
                "CustomForm",
                [](const CallbackTypeValues&) -> ll::Expected<ObjectRef> { return nullptr; },
                { ParamType::STRING }
            );
            classes.registerMethod(
                "CustomForm",
                "button",
                [](const ObjectRef&, const CallbackTypeValues&) -> ll::Expected<TypedValue> { return TypedValue{}; },
                { ParamType::STRING, ParamType::FUNCTION, ParamType::OBJECT }
            );
            classes.registerMethod(
                "CustomForm",
                "close",
                [](const ObjectRef&, const CallbackTypeValues&) -> ll::Expected<TypedValue> { return TypedValue{}; },
                {}
            );
            classes.registerMethod(
                "CustomForm",
                "show",
                [](const ObjectRef&, const CallbackTypeValues&) -> ll::Expected<TypedValue> { return TypedValue{}; },
                {}
            );

            classes.registerClass("ButtonOptions", { "disabled", "tooltip", "visible" });
            classes.registerField("ButtonOptions", "disabled");
            classes.registerField("ButtonOptions", "tooltip");
            classes.registerField("ButtonOptions", "visible");

            return true;
        }();

        (void)registered;
    }

    std::string errorsOf(const std::string& source) {
        registerFormStubs();

        DiagnosticEngine diagnostics;
        compile(source, diagnostics);

        return diagnostics.getErrorMessage();
    }
}

TEST(ComponentTest, ParametersAreDeclaredNotAssigned) {
    EXPECT_EQ(errorsOf(R"(
        component Twice(text) {
            let doubled = text + text;
        }

        let form = new CustomForm("t", "T") {
            Twice("x");
            show();
        };
    )"), "");
}

TEST(ComponentTest, ParametersAcceptCallbacks) {
    EXPECT_EQ(errorsOf(R"(
        component Bar(label, onHit) {
            button(label, on: onHit, new ButtonOptions());
        }

        let form = new CustomForm("t", "T") {
            Bar("go", func () -> void {
                this.close();
            });
            show();
        };
    )"), "");
}

TEST(ComponentTest, ParametersDoNotCollideAcrossComponents) {
    EXPECT_EQ(errorsOf(R"(
        component First(text) {
            let a = text;
        }

        component Second(text) {
            let b = text;
        }

        let form = new CustomForm("t", "T") {
            First("x");
            Second("y");
            show();
        };
    )"), "");
}

TEST(ComponentTest, ComponentIsRejectedOutsideADeclarativeBlock) {
    EXPECT_NE(errorsOf(R"(
        component Twice(text) {
            let doubled = text;
        }

        Twice("x");
    )"), "");
}
