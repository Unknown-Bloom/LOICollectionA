#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>
#include <variant>

#include "LOICollectionA/frontend/AST.h"
#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/frontend/DiagnosticEngine.h"

using namespace LOICollection::frontend;

bool FormClassRegistrationTestIntFieldEquals(const ObjectRef& obj, const std::string& field, int expected) {
    const ValueNode::ValueType* value = obj->find(field);

    return value != nullptr && std::holds_alternative<int>(*value) && std::get<int>(*value) == expected;
}

bool FormClassRegistrationTestStringFieldEquals(const ObjectRef& obj, const std::string& field, const std::string& expected) {
    const ValueNode::ValueType* value = obj->find(field);

    return value != nullptr && std::holds_alternative<std::string>(*value) && std::get<std::string>(*value) == expected;
}

bool FormClassRegistrationTestFieldIsArray(const ObjectRef& obj, const std::string& field) {
    const ValueNode::ValueType* value = obj->find(field);

    return value != nullptr && std::holds_alternative<ArrayRef>(*value);
}

TEST(FormClassRegistrationTest, RegisteredClassNamesExist) {
    auto& classes = ClassCall::getInstance();

    EXPECT_TRUE(classes.isRegistered("MenuData"));
    EXPECT_TRUE(classes.isRegistered("MenuItemData"));
    EXPECT_TRUE(classes.isRegistered("MenuControlData"));
    EXPECT_TRUE(classes.isRegistered("ShopData"));
    EXPECT_TRUE(classes.isRegistered("ShopItemData"));
    EXPECT_TRUE(classes.isRegistered("MenuForm"));
    EXPECT_TRUE(classes.isRegistered("ShopForm"));
    EXPECT_TRUE(classes.isRegistered("MenuMessageBox"));
    EXPECT_TRUE(classes.isRegistered("ScoreRequirement"));
    EXPECT_FALSE(classes.isRegistered("FormClassRegistrationTestUnknownClass"));
}

TEST(FormClassRegistrationTest, FieldLists) {
    auto& classes = ClassCall::getInstance();

    EXPECT_EQ(
        classes.getFields("MenuItemData"),
        (std::vector<std::string>{ "type", "title", "id", "run", "permission", "scores" })
    );
    EXPECT_EQ(
        classes.getFields("MenuControlData"),
        (std::vector<std::string>{
            "type", "id", "title", "placeholder", "defaultValue", "options", "min", "max", "step", "tooltip"
        })
    );
    EXPECT_EQ(
        classes.getFields("ScoreRequirement"),
        (std::vector<std::string>{ "objective", "value" })
    );
    EXPECT_EQ(
        classes.getFields("ShopItemData"),
        (std::vector<std::string>{
            "type", "title", "introduce", "number", "id", "nbt", "confirmButton", "cancelButton", "time", "scores"
        })
    );
    EXPECT_EQ(
        classes.getFields("ShopData"),
        (std::vector<std::string>{
            "id", "type", "title", "content", "exitCommand", "scoreCommand", "titleCommand", "itemCommand", "items"
        })
    );
    EXPECT_EQ(classes.getFields("MenuData").size(), 14u);
    EXPECT_TRUE(classes.getFields("MenuForm").empty());
    EXPECT_TRUE(classes.getFields("ShopForm").empty());
    EXPECT_TRUE(classes.getFields("MenuMessageBox").empty());

    EXPECT_TRUE(classes.hasField("MenuItemData", "permission"));
    EXPECT_TRUE(classes.hasField("MenuItemData", "scores"));
    EXPECT_TRUE(classes.hasField("MenuItemData", "run"));
    EXPECT_FALSE(classes.hasField("MenuItemData", "content"));
    EXPECT_TRUE(classes.hasField("MenuControlData", "tooltip"));
    EXPECT_TRUE(classes.hasField("MenuControlData", "min"));
    EXPECT_FALSE(classes.hasField("MenuControlData", "scores"));
    EXPECT_TRUE(classes.hasField("MenuData", "controls"));
    EXPECT_FALSE(classes.hasField("FormClassRegistrationTestUnknownClass", "id"));
}

TEST(FormClassRegistrationTest, ConstructorSignatures) {
    auto& classes = ClassCall::getInstance();

    auto menuForm = classes.getConstructorSignatures("MenuForm");
    ASSERT_EQ(menuForm.size(), 1u);
    EXPECT_EQ(menuForm.at(0), (CallbackTypeArgs{ ParamType::STRING, ParamType::STRING }));

    auto shopForm = classes.getConstructorSignatures("ShopForm");
    ASSERT_EQ(shopForm.size(), 1u);
    EXPECT_EQ(shopForm.at(0), (CallbackTypeArgs{ ParamType::STRING, ParamType::OBJECT }));

    auto messageBox = classes.getConstructorSignatures("MenuMessageBox");
    ASSERT_EQ(messageBox.size(), 1u);
    EXPECT_EQ(messageBox.at(0), (CallbackTypeArgs{ ParamType::STRING, ParamType::STRING }));

    EXPECT_TRUE(classes.getConstructorSignatures("MenuData").empty());
    EXPECT_TRUE(classes.getConstructorSignatures("MenuItemData").empty());
    EXPECT_TRUE(classes.getConstructorSignatures("MenuControlData").empty());
    EXPECT_TRUE(classes.getConstructorSignatures("ShopData").empty());
    EXPECT_TRUE(classes.getConstructorSignatures("ShopItemData").empty());
    EXPECT_TRUE(classes.getConstructorSignatures("ScoreRequirement").empty());
}

TEST(FormClassRegistrationTest, MethodSignatureOverloadCounts) {
    auto& classes = ClassCall::getInstance();

    auto button1 = classes.getMethodSignatures("MenuMessageBox", "button1");
    ASSERT_EQ(button1.size(), 4u);

    auto button2 = classes.getMethodSignatures("MenuMessageBox", "button2");
    ASSERT_EQ(button2.size(), 4u);

    auto slider = classes.getMethodSignatures("MenuForm", "slider");
    ASSERT_EQ(slider.size(), 2u);

    const CallbackTypeArgs sliderInteger{ ParamType::STRING, ParamType::OBJECT, ParamType::INT, ParamType::INT, ParamType::OBJECT };
    const CallbackTypeArgs sliderFloat{ ParamType::STRING, ParamType::OBJECT, ParamType::FLOAT, ParamType::FLOAT, ParamType::OBJECT };

    EXPECT_NE(std::find(slider.begin(), slider.end(), sliderInteger), slider.end());
    EXPECT_NE(std::find(slider.begin(), slider.end(), sliderFloat), slider.end());

    auto body = classes.getMethodSignatures("MenuMessageBox", "body");
    ASSERT_EQ(body.size(), 1u);
    EXPECT_EQ(body.at(0), (CallbackTypeArgs{ ParamType::STRING }));

    auto chooseButton = classes.getMethodSignatures("ShopForm", "chooseButton");
    ASSERT_EQ(chooseButton.size(), 1u);
    EXPECT_EQ(chooseButton.at(0), (CallbackTypeArgs{ ParamType::STRING, ParamType::STRING }));

    EXPECT_EQ(classes.getMethodSignatures("MenuForm", "button").size(), 2u);
    EXPECT_EQ(classes.getMethodSignatures("MenuForm", "show").size(), 2u);
    EXPECT_EQ(classes.getMethodSignatures("ShopForm", "show").size(), 2u);
    EXPECT_TRUE(classes.getMethodSignatures("MenuForm", "FormClassRegistrationTestUnknownMethod").empty());
}

TEST(FormClassRegistrationTest, CreateMenuControlDataDefaults) {
    auto& classes = ClassCall::getInstance();
    DiagnosticEngine diagnostics;

    auto result = classes.create("MenuControlData", {}, {}, diagnostics);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(diagnostics.hasErrors());

    ObjectRef control = result.value();

    EXPECT_EQ(control->className, "MenuControlData");
    EXPECT_EQ(control->classIndex, -1);
    EXPECT_TRUE(FormClassRegistrationTestIntFieldEquals(control, "min", 0));
    EXPECT_TRUE(FormClassRegistrationTestIntFieldEquals(control, "max", 100));
    EXPECT_TRUE(FormClassRegistrationTestIntFieldEquals(control, "step", 1));
    EXPECT_TRUE(FormClassRegistrationTestIntFieldEquals(control, "defaultValue", 0));
    EXPECT_TRUE(FormClassRegistrationTestStringFieldEquals(control, "type", ""));
    EXPECT_TRUE(FormClassRegistrationTestStringFieldEquals(control, "id", ""));
    EXPECT_TRUE(FormClassRegistrationTestStringFieldEquals(control, "title", ""));
    EXPECT_TRUE(FormClassRegistrationTestStringFieldEquals(control, "placeholder", ""));
    EXPECT_TRUE(FormClassRegistrationTestStringFieldEquals(control, "tooltip", ""));
    EXPECT_TRUE(FormClassRegistrationTestFieldIsArray(control, "options"));
}

TEST(FormClassRegistrationTest, CreateMenuItemDataDefaults) {
    auto& classes = ClassCall::getInstance();
    DiagnosticEngine diagnostics;

    auto result = classes.create("MenuItemData", {}, {}, diagnostics);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(diagnostics.hasErrors());

    ObjectRef item = result.value();

    EXPECT_EQ(item->className, "MenuItemData");
    EXPECT_EQ(item->classIndex, -1);
    EXPECT_TRUE(FormClassRegistrationTestIntFieldEquals(item, "permission", 0));
    EXPECT_TRUE(FormClassRegistrationTestStringFieldEquals(item, "type", ""));
    EXPECT_TRUE(FormClassRegistrationTestStringFieldEquals(item, "title", ""));
    EXPECT_TRUE(FormClassRegistrationTestStringFieldEquals(item, "id", ""));
    EXPECT_TRUE(FormClassRegistrationTestFieldIsArray(item, "run"));
    EXPECT_TRUE(FormClassRegistrationTestFieldIsArray(item, "scores"));
}

TEST(FormClassRegistrationTest, CreateShopItemDataDefaults) {
    auto& classes = ClassCall::getInstance();
    DiagnosticEngine diagnostics;

    auto result = classes.create("ShopItemData", {}, {}, diagnostics);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(diagnostics.hasErrors());

    ObjectRef item = result.value();

    EXPECT_EQ(item->className, "ShopItemData");
    EXPECT_EQ(item->classIndex, -1);
    EXPECT_TRUE(FormClassRegistrationTestIntFieldEquals(item, "time", 0));
    EXPECT_TRUE(FormClassRegistrationTestStringFieldEquals(item, "type", ""));
    EXPECT_TRUE(FormClassRegistrationTestStringFieldEquals(item, "id", ""));
    EXPECT_TRUE(FormClassRegistrationTestStringFieldEquals(item, "nbt", ""));
    EXPECT_TRUE(FormClassRegistrationTestStringFieldEquals(item, "confirmButton", ""));
    EXPECT_TRUE(FormClassRegistrationTestStringFieldEquals(item, "cancelButton", ""));
    EXPECT_TRUE(FormClassRegistrationTestFieldIsArray(item, "scores"));
}

TEST(FormClassRegistrationTest, CreateScoreRequirementMenuDataAndShopDataDefaults) {
    auto& classes = ClassCall::getInstance();
    DiagnosticEngine diagnostics;

    auto scoreResult = classes.create("ScoreRequirement", {}, {}, diagnostics);
    ASSERT_TRUE(scoreResult.has_value());
    EXPECT_FALSE(diagnostics.hasErrors());

    ObjectRef score = scoreResult.value();

    EXPECT_EQ(score->className, "ScoreRequirement");
    EXPECT_TRUE(FormClassRegistrationTestStringFieldEquals(score, "objective", ""));
    EXPECT_TRUE(FormClassRegistrationTestIntFieldEquals(score, "value", 0));

    auto menuResult = classes.create("MenuData", {}, {}, diagnostics);
    ASSERT_TRUE(menuResult.has_value());
    EXPECT_FALSE(diagnostics.hasErrors());

    ObjectRef menu = menuResult.value();

    EXPECT_EQ(menu->className, "MenuData");
    EXPECT_EQ(menu->classIndex, -1);
    EXPECT_TRUE(FormClassRegistrationTestIntFieldEquals(menu, "permission", 0));
    EXPECT_TRUE(FormClassRegistrationTestStringFieldEquals(menu, "id", ""));
    EXPECT_TRUE(FormClassRegistrationTestStringFieldEquals(menu, "submit", ""));
    EXPECT_TRUE(FormClassRegistrationTestFieldIsArray(menu, "items"));
    EXPECT_TRUE(FormClassRegistrationTestFieldIsArray(menu, "controls"));
    EXPECT_TRUE(FormClassRegistrationTestFieldIsArray(menu, "run"));
    EXPECT_TRUE(FormClassRegistrationTestIntFieldEquals(menu, "confirm", 0));
    EXPECT_TRUE(FormClassRegistrationTestIntFieldEquals(menu, "cancel", 0));

    auto shopResult = classes.create("ShopData", {}, {}, diagnostics);
    ASSERT_TRUE(shopResult.has_value());
    EXPECT_FALSE(diagnostics.hasErrors());

    ObjectRef shop = shopResult.value();

    EXPECT_EQ(shop->className, "ShopData");
    EXPECT_TRUE(FormClassRegistrationTestStringFieldEquals(shop, "id", ""));
    EXPECT_TRUE(FormClassRegistrationTestFieldIsArray(shop, "items"));
}

TEST(FormClassRegistrationTest, CreateUnknownClassFails) {
    auto& classes = ClassCall::getInstance();
    DiagnosticEngine diagnostics;

    auto result = classes.create("FormClassRegistrationTestUnknownClass", {}, {}, diagnostics);

    EXPECT_FALSE(result.has_value());
}
