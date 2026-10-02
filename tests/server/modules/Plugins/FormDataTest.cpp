#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>
#include <variant>

#include "LOICollectionA/frontend/AST.h"
#include "LOICollectionA/frontend/Callback.h"

#include "LOICollectionA/include/server/Plugins/form/ShopData.h"
#include "LOICollectionA/include/server/Plugins/form/MenuData.h"
#include "LOICollectionA/include/server/Plugins/form/ScoreData.h"

using namespace LOICollection::frontend;
using namespace LOICollection::server::Plugins;

ObjectRef FormDataTestMakeObject(const std::string& className) {
    auto obj = std::make_shared<Object>();
    obj->className = className;
    obj->classIndex = -1;

    return obj;
}

ArrayRef FormDataTestMakeArray() {
    return std::make_shared<ArrayValue>();
}

ObjectRef FormDataTestMakeScoreRequirement(const TypedValue& objective, const TypedValue& value) {
    auto obj = FormDataTestMakeObject("ScoreRequirement");
    obj->assign("objective", objective);
    obj->assign("value", value);

    return obj;
}

ArrayRef FormDataTestWrapControl(const MenuControlData& control) {
    auto array = FormDataTestMakeArray();
    array->elements.emplace_back(makeMenuControlDataObject(control));

    return array;
}

TEST(FormDataTest, ReadScoresKeepsOnlyScoreRequirementElements) {
    auto obj = FormDataTestMakeObject("MenuItemData");

    auto array = FormDataTestMakeArray();
    array->elements.emplace_back(FormDataTestMakeScoreRequirement(std::string("test_objective_a"), 5));
    array->elements.emplace_back(FormDataTestMakeObject("OtherData"));
    array->elements.emplace_back(10);
    array->elements.emplace_back(std::string("test_objective_b"));
    array->elements.emplace_back(FormDataTestMakeScoreRequirement(std::string("test_objective_c"), 7));

    obj->assign("scores", array);

    auto scores = readScores(obj);

    ASSERT_EQ(scores.size(), 2u);
    EXPECT_EQ(scores.at(0).objective, "test_objective_a");
    EXPECT_EQ(scores.at(0).value, 5);
    EXPECT_EQ(scores.at(1).objective, "test_objective_c");
    EXPECT_EQ(scores.at(1).value, 7);
}

TEST(FormDataTest, ReadScoresRejectsNonArrayMissingFieldAndUnknownClassName) {
    auto integerField = FormDataTestMakeObject("MenuItemData");
    integerField->assign("scores", 3);
    EXPECT_TRUE(readScores(integerField).empty());

    auto stringField = FormDataTestMakeObject("MenuItemData");
    stringField->assign("scores", std::string("test_not_an_array"));
    EXPECT_TRUE(readScores(stringField).empty());

    auto bareObject = FormDataTestMakeObject("MenuItemData");
    EXPECT_TRUE(readScores(bareObject).empty());

    auto unknownClass = FormDataTestMakeObject("FormDataTestUnknownClass");
    EXPECT_TRUE(readScores(unknownClass).empty());
}

TEST(FormDataTest, ReadScoresHonoursCustomFieldName) {
    auto obj = FormDataTestMakeObject("MenuItemData");

    auto array = FormDataTestMakeArray();
    array->elements.emplace_back(FormDataTestMakeScoreRequirement(std::string("test_objective_custom"), 11));

    obj->assign("customScores", array);

    EXPECT_TRUE(readScores(obj).empty());

    auto scores = readScores(obj, "customScores");

    ASSERT_EQ(scores.size(), 1u);
    EXPECT_EQ(scores.at(0).objective, "test_objective_custom");
    EXPECT_EQ(scores.at(0).value, 11);
}

TEST(FormDataTest, ReadScoresCoercesObjectiveAndValue) {
    auto obj = FormDataTestMakeObject("MenuItemData");

    auto array = FormDataTestMakeArray();
    array->elements.emplace_back(FormDataTestMakeScoreRequirement(42, 9));
    array->elements.emplace_back(FormDataTestMakeScoreRequirement(std::string("test_objective_float"), 3.75f));
    array->elements.emplace_back(FormDataTestMakeScoreRequirement(std::string("test_objective_negative"), -3.75f));
    array->elements.emplace_back(FormDataTestMakeScoreRequirement(std::string("test_objective_string"), std::string("12")));
    array->elements.emplace_back(FormDataTestMakeScoreRequirement(std::string("test_objective_bool"), true));

    obj->assign("scores", array);

    auto scores = readScores(obj);

    ASSERT_EQ(scores.size(), 5u);
    EXPECT_EQ(scores.at(0).objective, "");
    EXPECT_EQ(scores.at(0).value, 9);
    EXPECT_EQ(scores.at(1).value, 3);
    EXPECT_EQ(scores.at(2).value, -3);
    EXPECT_EQ(scores.at(3).value, 0);
    EXPECT_EQ(scores.at(4).value, 0);
}

TEST(FormDataTest, MakeScoreArrayRoundTrip) {
    std::vector<ScoreRequirement> input = {
        { "test_objective_x", 10 },
        { "test_objective_y", -4 }
    };

    auto array = makeScoreArray(input);

    ASSERT_EQ(array->elements.size(), 2u);
    ASSERT_TRUE(std::holds_alternative<ObjectRef>(array->elements.at(0)));

    auto first = std::get<ObjectRef>(array->elements.at(0));
    EXPECT_EQ(first->className, "ScoreRequirement");
    EXPECT_EQ(first->classIndex, -1);

    auto obj = FormDataTestMakeObject("MenuItemData");
    obj->assign("scores", array);

    auto scores = readScores(obj);

    ASSERT_EQ(scores.size(), 2u);
    EXPECT_EQ(scores.at(0).objective, "test_objective_x");
    EXPECT_EQ(scores.at(0).value, 10);
    EXPECT_EQ(scores.at(1).objective, "test_objective_y");
    EXPECT_EQ(scores.at(1).value, -4);
}

TEST(FormDataTest, MakeMenuItemDataObjectRoundTrip) {
    MenuItemData item;
    item.type = "button";
    item.title = "Test Item";
    item.id = "test_item_id";
    item.run = { "say one", "say two" };
    item.permission = 3;
    item.scores = { { "test_objective_money", 250 } };

    auto obj = makeMenuItemDataObject(item);

    EXPECT_EQ(obj->className, "MenuItemData");
    EXPECT_EQ(obj->classIndex, -1);

    auto hydrated = hydrateMenuItem(obj);

    EXPECT_EQ(hydrated.type, "button");
    EXPECT_EQ(hydrated.title, "Test Item");
    EXPECT_EQ(hydrated.id, "test_item_id");
    EXPECT_EQ(hydrated.permission, 3);
    ASSERT_EQ(hydrated.run.size(), 2u);
    EXPECT_EQ(hydrated.run.at(0), "say one");
    EXPECT_EQ(hydrated.run.at(1), "say two");
    ASSERT_EQ(hydrated.scores.size(), 1u);
    EXPECT_EQ(hydrated.scores.at(0).objective, "test_objective_money");
    EXPECT_EQ(hydrated.scores.at(0).value, 250);
}

TEST(FormDataTest, HydrateMenuItemDefaultsForBareObject) {
    auto obj = FormDataTestMakeObject("MenuItemData");
    auto item = hydrateMenuItem(obj);

    EXPECT_TRUE(item.type.empty());
    EXPECT_TRUE(item.title.empty());
    EXPECT_TRUE(item.id.empty());
    EXPECT_TRUE(item.run.empty());
    EXPECT_EQ(item.permission, 0);
    EXPECT_TRUE(item.scores.empty());

    auto unknownClass = FormDataTestMakeObject("FormDataTestUnknownClass");
    auto unknownItem = hydrateMenuItem(unknownClass);

    EXPECT_TRUE(unknownItem.type.empty());
    EXPECT_TRUE(unknownItem.title.empty());
    EXPECT_TRUE(unknownItem.id.empty());
    EXPECT_TRUE(unknownItem.run.empty());
    EXPECT_EQ(unknownItem.permission, 0);
    EXPECT_TRUE(unknownItem.scores.empty());
}

TEST(FormDataTest, HydrateMenuDataCopiesScalarsAndFiltersElements) {
    MenuItemData menuItem;
    menuItem.type = "button";
    menuItem.title = "Item";
    menuItem.id = "item_id";
    menuItem.run = { "say item" };
    menuItem.permission = 2;

    MenuControlData control;
    control.type = "slider";
    control.id = "control_id";
    control.title = "Control";
    control.placeholder = "Control Placeholder";
    control.tooltip = "Control Tooltip";
    control.options = { "alpha", "beta" };
    control.min = -5;
    control.max = 250;
    control.step = 3;
    control.defaultValue = 2.5f;

    auto obj = FormDataTestMakeObject("MenuData");
    obj->assign("id", std::string("test_menu_id"));
    obj->assign("type", std::string("Simple"));
    obj->assign("title", std::string("Test Menu"));
    obj->assign("content", std::string("Test Content"));
    obj->assign("permission", 1);
    obj->assign("exitCommand", std::string("test_menu_exit"));
    obj->assign("scoreCommand", std::string("test_menu_score"));
    obj->assign("permissionCommand", std::string("test_menu_permission"));
    obj->assign("submit", std::string("test_menu_submit"));

    auto items = FormDataTestMakeArray();
    items->elements.emplace_back(makeMenuItemDataObject(menuItem));
    items->elements.emplace_back(FormDataTestMakeObject("ShopItemData"));
    items->elements.emplace_back(5);
    obj->assign("items", items);

    auto controls = FormDataTestMakeArray();
    controls->elements.emplace_back(makeMenuControlDataObject(control));
    controls->elements.emplace_back(makeMenuItemDataObject(menuItem));
    controls->elements.emplace_back(std::string("test_not_an_object"));
    obj->assign("controls", controls);

    auto runArray = FormDataTestMakeArray();
    runArray->elements.emplace_back(std::string("run_one"));
    runArray->elements.emplace_back(7);
    runArray->elements.emplace_back(std::string("run_two"));
    runArray->elements.emplace_back(true);
    obj->assign("run", runArray);

    obj->assign("confirm", makeMenuItemDataObject(menuItem));
    obj->assign("cancel", 9);

    auto data = hydrateMenuData(obj);

    EXPECT_EQ(data.id, "test_menu_id");
    EXPECT_EQ(data.type, "Simple");
    EXPECT_EQ(data.title, "Test Menu");
    EXPECT_EQ(data.content, "Test Content");
    EXPECT_EQ(data.permission, 1);
    EXPECT_EQ(data.exitCommand, "test_menu_exit");
    EXPECT_EQ(data.scoreCommand, "test_menu_score");
    EXPECT_EQ(data.permissionCommand, "test_menu_permission");
    EXPECT_EQ(data.submit, "test_menu_submit");

    ASSERT_EQ(data.items.size(), 1u);
    EXPECT_EQ(data.items.at(0).type, "button");
    EXPECT_EQ(data.items.at(0).id, "item_id");
    EXPECT_EQ(data.items.at(0).permission, 2);
    ASSERT_EQ(data.items.at(0).run.size(), 1u);
    EXPECT_EQ(data.items.at(0).run.at(0), "say item");

    ASSERT_EQ(data.controls.size(), 1u);
    EXPECT_EQ(data.controls.at(0).type, "slider");
    EXPECT_EQ(data.controls.at(0).id, "control_id");
    EXPECT_EQ(data.controls.at(0).title, "Control");
    EXPECT_EQ(data.controls.at(0).placeholder, "Control Placeholder");
    EXPECT_EQ(data.controls.at(0).tooltip, "Control Tooltip");
    EXPECT_EQ(data.controls.at(0).min, -5);
    EXPECT_EQ(data.controls.at(0).max, 250);
    EXPECT_EQ(data.controls.at(0).step, 3);
    ASSERT_EQ(data.controls.at(0).options.size(), 2u);
    EXPECT_EQ(data.controls.at(0).options.at(0), "alpha");
    EXPECT_EQ(data.controls.at(0).options.at(1), "beta");
    ASSERT_TRUE(std::holds_alternative<float>(data.controls.at(0).defaultValue));
    EXPECT_FLOAT_EQ(std::get<float>(data.controls.at(0).defaultValue), 2.5f);

    ASSERT_EQ(data.run.size(), 2u);
    EXPECT_EQ(data.run.at(0), "run_one");
    EXPECT_EQ(data.run.at(1), "run_two");

    EXPECT_EQ(data.confirm.id, "item_id");
    EXPECT_EQ(data.confirm.permission, 2);
    EXPECT_TRUE(data.cancel.id.empty());
    EXPECT_EQ(data.cancel.permission, 0);
    EXPECT_TRUE(data.cancel.run.empty());
}

TEST(FormDataTest, HydrateMenuDataDefaultsForBareObject) {
    auto obj = FormDataTestMakeObject("MenuData");
    auto data = hydrateMenuData(obj);

    EXPECT_TRUE(data.id.empty());
    EXPECT_TRUE(data.type.empty());
    EXPECT_TRUE(data.title.empty());
    EXPECT_TRUE(data.content.empty());
    EXPECT_EQ(data.permission, 0);
    EXPECT_TRUE(data.exitCommand.empty());
    EXPECT_TRUE(data.scoreCommand.empty());
    EXPECT_TRUE(data.permissionCommand.empty());
    EXPECT_TRUE(data.items.empty());
    EXPECT_TRUE(data.controls.empty());
    EXPECT_TRUE(data.run.empty());
    EXPECT_TRUE(data.submit.empty());
    EXPECT_TRUE(data.confirm.id.empty());
    EXPECT_TRUE(data.cancel.id.empty());
}

TEST(FormDataTest, MenuControlDataDefaults) {
    MenuControlData control;

    EXPECT_TRUE(control.type.empty());
    EXPECT_TRUE(control.id.empty());
    EXPECT_TRUE(control.title.empty());
    EXPECT_TRUE(control.placeholder.empty());
    EXPECT_TRUE(control.tooltip.empty());
    EXPECT_TRUE(control.options.empty());
    EXPECT_EQ(control.min, 0);
    EXPECT_EQ(control.max, 100);
    EXPECT_EQ(control.step, 1);
    ASSERT_TRUE(std::holds_alternative<int>(control.defaultValue));
    EXPECT_EQ(std::get<int>(control.defaultValue), 0);
}

TEST(FormDataTest, MakeMenuControlDataObjectRoundTrip) {
    MenuControlData defaults;
    auto defaultObject = makeMenuControlDataObject(defaults);

    EXPECT_EQ(defaultObject->className, "MenuControlData");
    EXPECT_EQ(defaultObject->classIndex, -1);

    auto defaultMenu = FormDataTestMakeObject("MenuData");
    defaultMenu->assign("controls", FormDataTestWrapControl(defaults));

    auto defaultData = hydrateMenuData(defaultMenu);

    ASSERT_EQ(defaultData.controls.size(), 1u);
    EXPECT_EQ(defaultData.controls.at(0).min, 0);
    EXPECT_EQ(defaultData.controls.at(0).max, 100);
    EXPECT_EQ(defaultData.controls.at(0).step, 1);
    EXPECT_TRUE(defaultData.controls.at(0).options.empty());
    EXPECT_EQ(defaultData.controls.at(0).defaultValue, defaults.defaultValue);

    MenuControlData control;
    control.type = "dropdown";
    control.id = "control_id";
    control.title = "Control Title";
    control.placeholder = "Control Placeholder";
    control.tooltip = "Control Tooltip";
    control.options = { "one", "two", "three" };
    control.min = -10;
    control.max = 300;
    control.step = 5;
    control.defaultValue = std::string("two");

    auto menu = FormDataTestMakeObject("MenuData");
    menu->assign("controls", FormDataTestWrapControl(control));

    auto data = hydrateMenuData(menu);

    ASSERT_EQ(data.controls.size(), 1u);
    EXPECT_EQ(data.controls.at(0).type, "dropdown");
    EXPECT_EQ(data.controls.at(0).id, "control_id");
    EXPECT_EQ(data.controls.at(0).title, "Control Title");
    EXPECT_EQ(data.controls.at(0).placeholder, "Control Placeholder");
    EXPECT_EQ(data.controls.at(0).tooltip, "Control Tooltip");
    EXPECT_EQ(data.controls.at(0).min, -10);
    EXPECT_EQ(data.controls.at(0).max, 300);
    EXPECT_EQ(data.controls.at(0).step, 5);
    ASSERT_EQ(data.controls.at(0).options.size(), 3u);
    EXPECT_EQ(data.controls.at(0).options.at(0), "one");
    EXPECT_EQ(data.controls.at(0).options.at(1), "two");
    EXPECT_EQ(data.controls.at(0).options.at(2), "three");
    ASSERT_TRUE(std::holds_alternative<std::string>(data.controls.at(0).defaultValue));
    EXPECT_EQ(std::get<std::string>(data.controls.at(0).defaultValue), "two");
}

TEST(FormDataTest, HydrateShopItemRoundTrip) {
    ShopItemData item;
    item.type = "commodity";
    item.title = "Shop Item";
    item.introduce = "Shop Introduce";
    item.number = "Shop Number";
    item.id = "shop_item_id";
    item.nbt = "test_nbt";
    item.confirmButton = "Confirm Button";
    item.cancelButton = "Cancel Button";
    item.time = 60;
    item.scores = { { "test_objective_shop", 500 } };

    auto obj = makeShopItemDataObject(item);

    EXPECT_EQ(obj->className, "ShopItemData");
    EXPECT_EQ(obj->classIndex, -1);

    auto hydrated = hydrateShopItem(obj);

    EXPECT_EQ(hydrated.type, "commodity");
    EXPECT_EQ(hydrated.title, "Shop Item");
    EXPECT_EQ(hydrated.introduce, "Shop Introduce");
    EXPECT_EQ(hydrated.number, "Shop Number");
    EXPECT_EQ(hydrated.id, "shop_item_id");
    EXPECT_EQ(hydrated.nbt, "test_nbt");
    EXPECT_EQ(hydrated.confirmButton, "Confirm Button");
    EXPECT_EQ(hydrated.cancelButton, "Cancel Button");
    EXPECT_EQ(hydrated.time, 60);
    ASSERT_EQ(hydrated.scores.size(), 1u);
    EXPECT_EQ(hydrated.scores.at(0).objective, "test_objective_shop");
    EXPECT_EQ(hydrated.scores.at(0).value, 500);
}

TEST(FormDataTest, HydrateShopItemDefaultsAndTimeCoercion) {
    auto obj = FormDataTestMakeObject("ShopItemData");
    auto item = hydrateShopItem(obj);

    EXPECT_TRUE(item.type.empty());
    EXPECT_TRUE(item.title.empty());
    EXPECT_TRUE(item.introduce.empty());
    EXPECT_TRUE(item.number.empty());
    EXPECT_TRUE(item.id.empty());
    EXPECT_TRUE(item.nbt.empty());
    EXPECT_TRUE(item.confirmButton.empty());
    EXPECT_TRUE(item.cancelButton.empty());
    EXPECT_EQ(item.time, 0);
    EXPECT_TRUE(item.scores.empty());

    auto floatTime = FormDataTestMakeObject("ShopItemData");
    floatTime->assign("time", 2.9f);

    EXPECT_EQ(hydrateShopItem(floatTime).time, 2);
}

TEST(FormDataTest, HydrateShopDataCopiesScalarsAndFiltersItems) {
    ShopItemData item;
    item.type = "title";
    item.title = "Shop Item";
    item.id = "shop_item_id";
    item.time = 30;

    auto obj = FormDataTestMakeObject("ShopData");
    obj->assign("id", std::string("test_shop_id"));
    obj->assign("type", std::string("sell"));
    obj->assign("title", std::string("Test Shop"));
    obj->assign("content", std::string("Shop Content"));
    obj->assign("exitCommand", std::string("test_shop_exit"));
    obj->assign("scoreCommand", std::string("test_shop_score"));
    obj->assign("titleCommand", std::string("test_shop_title"));
    obj->assign("itemCommand", std::string("test_shop_item"));

    auto items = FormDataTestMakeArray();
    items->elements.emplace_back(makeShopItemDataObject(item));
    items->elements.emplace_back(FormDataTestMakeObject("MenuItemData"));
    items->elements.emplace_back(3);
    obj->assign("items", items);

    auto data = hydrateShopData(obj);

    EXPECT_EQ(data.id, "test_shop_id");
    EXPECT_EQ(data.type, "sell");
    EXPECT_EQ(data.title, "Test Shop");
    EXPECT_EQ(data.content, "Shop Content");
    EXPECT_EQ(data.exitCommand, "test_shop_exit");
    EXPECT_EQ(data.scoreCommand, "test_shop_score");
    EXPECT_EQ(data.titleCommand, "test_shop_title");
    EXPECT_EQ(data.itemCommand, "test_shop_item");

    ASSERT_EQ(data.items.size(), 1u);
    EXPECT_EQ(data.items.at(0).type, "title");
    EXPECT_EQ(data.items.at(0).title, "Shop Item");
    EXPECT_EQ(data.items.at(0).id, "shop_item_id");
    EXPECT_EQ(data.items.at(0).time, 30);
}

TEST(FormDataTest, HydrateShopDataDefaultsAndNonArrayItems) {
    auto obj = FormDataTestMakeObject("ShopData");
    auto data = hydrateShopData(obj);

    EXPECT_TRUE(data.id.empty());
    EXPECT_TRUE(data.type.empty());
    EXPECT_TRUE(data.title.empty());
    EXPECT_TRUE(data.content.empty());
    EXPECT_TRUE(data.exitCommand.empty());
    EXPECT_TRUE(data.scoreCommand.empty());
    EXPECT_TRUE(data.titleCommand.empty());
    EXPECT_TRUE(data.itemCommand.empty());
    EXPECT_TRUE(data.items.empty());

    auto integerItems = FormDataTestMakeObject("ShopData");
    integerItems->assign("items", 5);
    EXPECT_TRUE(hydrateShopData(integerItems).items.empty());

    auto arrayItems = FormDataTestMakeArray();
    arrayItems->elements.emplace_back(FormDataTestMakeObject("MenuItemData"));
    arrayItems->elements.emplace_back(std::string("test_not_an_object"));

    auto filteredItems = FormDataTestMakeObject("ShopData");
    filteredItems->assign("items", arrayItems);
    EXPECT_TRUE(hydrateShopData(filteredItems).items.empty());
}
