#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CommonTest.h"
#include "GuiTestEnv.h"

using namespace LOICollection::frontend;
using LOICollection::frontend::test::formCalls;
using LOICollection::frontend::test::registerGuiTestEnvironment;

/* §5.1 — declarative UI blocks: `new Form(args) { ... }` must behave exactly
 * like the imperative expansion `form = new Form(args); form.method(...)`. */

class DeclarativeBlockTest : public ::testing::Test {
protected:
    void SetUp() override {
        registerGuiTestEnvironment();
        formCalls().clear();
    }
};

TEST_F(DeclarativeBlockTest, BlockMatchesManualExpansion) {
    eval(R"(
        form = new CustomForm("id", "title") {
            label("hello", new TextOptions());
            spacer(new SpacingOptions());
            divider(new DividerOptions());
            closeButton();
            show();
        };
    )");

    EXPECT_EQ(formCalls(), (std::vector<std::string>{
        "label", "spacer", "divider", "closeButton", "show"
    }));
}

TEST_F(DeclarativeBlockTest, ManualExpansionBaseline) {
    eval(R"(
        form = new CustomForm("id", "title");
        form.label("hello", new TextOptions());
        form.spacer(new SpacingOptions());
        form.divider(new DividerOptions());
        form.closeButton();
        form.show();
    )");

    EXPECT_EQ(formCalls(), (std::vector<std::string>{
        "label", "spacer", "divider", "closeButton", "show"
    }));
}

TEST_F(DeclarativeBlockTest, ControlFlowInsideBlock) {
    eval(R"(
        flag = new GlobalValue();
        flag.value = true;

        form = new CustomForm("id", "title") {
            if (flag.value) [
                label("yes", new TextOptions());
            :
                divider(new DividerOptions());
            ]
            closeButton();
        };

        form2 = new CustomForm("id", "title") {
            if (!flag.value) [
                label("yes", new TextOptions());
            :
                divider(new DividerOptions());
            ]
            closeButton();
        };
    )");

    EXPECT_EQ(formCalls(), (std::vector<std::string>{
        "label", "closeButton", "divider", "closeButton"
    }));
}

TEST_F(DeclarativeBlockTest, OnSugarMapsHandlerPositionally) {
    /* `button(text, on: handler, options)` must pass the handler in the
     * same position as the explicit form; the journal proves the call
     * resolved to the real `button` overload. */
    eval(R"(
        form = new CustomForm("id", "title") {
            button("go", on: func () -> void { form.close(); }, new ButtonOptions());
        };

        trigger = form.handler;
        trigger();
    )");

    EXPECT_EQ(formCalls(), (std::vector<std::string>{ "button", "close" }));
}

TEST_F(DeclarativeBlockTest, ReceiverVisibleInNestedLambda) {
    /* The receiver is bound before the body runs, so a handler defined in
     * the block can reference the form under construction by name. */
    eval(R"(
        form = new CustomForm("id", "title") {
            button("go", func () -> void { form.close(); }, new ButtonOptions());
            show();
        };

        trigger = form.handler;
        trigger();
    )");

    EXPECT_EQ(formCalls(), (std::vector<std::string>{ "button", "show", "close" }));
}

TEST_F(DeclarativeBlockTest, ImportedHelpersUsableInsideBlock) {
    /* Bare calls that are NOT form methods fall back to ordinary function
     * resolution, so imported helpers stay callable inside a block. */
    eval(R"(
        func saveLabel() -> string {
            return "Save";
        }

        form = new CustomForm("id", "title") {
            button(saveLabel(), on: func () -> void { form.close(); }, new ButtonOptions());
        };
    )");

    EXPECT_EQ(formCalls(), (std::vector<std::string>{ "button" }));
}

TEST_F(DeclarativeBlockTest, NonFormClassRejected) {
    EXPECT_THROW(
        eval("form = new GlobalValue() { label(\"x\", new TextOptions()); };"),
        std::runtime_error
    );
}

TEST_F(DeclarativeBlockTest, BlockInsideFunctionBody) {
    eval(R"(
        func build() -> void {
            form = new CustomForm("id", "title") {
                label("x", new TextOptions());
                closeButton();
            };
        }

        build();
    )");

    EXPECT_EQ(formCalls(), (std::vector<std::string>{ "label", "closeButton" }));
}
