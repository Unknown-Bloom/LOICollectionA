#include <gtest/gtest.h>

#include <ll/api/Expected.h>

#include "LOICollectionA/frontend/Context.h"
#include "LOICollectionA/frontend/Callback.h"

using namespace LOICollection::frontend;

TEST(FunctionCallTest, RegisterAndCall) {
    DiagnosticEngine diagnostics;
    auto& fc = FunctionCall::getInstance();
    const std::string ns = "test_callback";

    fc.registerFunction(ns, "add",
        [](const CallbackTypeValues& v) -> TypedValue {
            return std::get<int>(v[0]) + std::get<int>(v[1]);
        }, { ParamType::INT, ParamType::INT });

    CallbackTypeValues args = { 10, 20 };

    auto r1 = fc.callFunction(ns, "add", args, {}, diagnostics);
    EXPECT_TRUE(r1.has_value());
    EXPECT_FALSE(diagnostics.hasErrors());
    EXPECT_EQ(std::get<int>(r1.value()), 30);

    auto r2 = fc.callFunction(ns, "sub", args, {}, diagnostics);
    EXPECT_TRUE(diagnostics.hasErrors());

    fc.unregisterFunction(ns, "add", { ParamType::INT, ParamType::INT }, false);
}

TEST(FunctionCallTest, CombinationFunction) {
    DiagnosticEngine diagnostics;
    auto& fc = FunctionCall::getInstance();
    const std::string ns = "test_combo";

    fc.registerFunction(ns, "get_placeholder",
        [](const CallbackTypeValues&, const CallbackTypePlaces& places) -> TypedValue {
            if (places.count(0))
                return std::any_cast<int>(places.at(0)) * 2;

            return "missing";
        }, {});

    Context ctx(42);
    CallbackTypeValues emptyArgs;

    auto result = fc.callFunction(ns, "get_placeholder", emptyArgs, ctx.params, diagnostics);
    EXPECT_TRUE(result.has_value());
    EXPECT_FALSE(diagnostics.hasErrors());
    EXPECT_EQ(std::get<int>(result.value()), 84);

    fc.unregisterFunction(ns, "get_placeholder", {}, true);
}

TEST(FunctionCallTest, IsRegistered) {
    auto& fc = FunctionCall::getInstance();
    const std::string ns = "test_reg";
    const CallbackTypeArgs args = { ParamType::INT };

    fc.registerFunction(ns, "double",
        [](const CallbackTypeValues& v) -> TypedValue {
            return std::get<int>(v[0]) * 2;
        }, args);

    EXPECT_TRUE(fc.isRegistered(ns, "double", args));
    EXPECT_FALSE(fc.isRegistered(ns, "double", { ParamType::STRING }));
    EXPECT_FALSE(fc.isRegistered(ns, "missing", args));

    fc.unregisterFunction(ns, "double", args, false);
    EXPECT_FALSE(fc.isRegistered(ns, "double", args));
}

TEST(FunctionCallTest, UnregisterCombinationCallback) {
    auto& fc = FunctionCall::getInstance();
    const std::string ns = "test_combo_unreg";

    fc.registerFunction(ns, "combo",
        [](const CallbackTypeValues&, const CallbackTypePlaces&) -> TypedValue { return 1; }, {});

    EXPECT_TRUE(fc.isRegistered(ns, "combo", {}));

    fc.unregisterFunction(ns, "combo", {}, true);

    EXPECT_FALSE(fc.isRegistered(ns, "combo", {}));
}

TEST(FunctionCallTest, WrongArgumentTypes) {
    DiagnosticEngine diagnostics;
    auto& fc = FunctionCall::getInstance();
    const std::string ns = "test_types";

    fc.registerFunction(ns, "only_int",
        [](const CallbackTypeValues& v) -> TypedValue {
            return std::get<int>(v[0]);
        }, { ParamType::INT });

    CallbackTypeValues wrong = { std::string("not int") };
    auto result = fc.callFunction(ns, "only_int", wrong, {}, diagnostics);

    EXPECT_TRUE(diagnostics.hasErrors());
    EXPECT_NE(diagnostics.getErrorMessage().find("Function not registered"), std::string::npos);

    fc.unregisterFunction(ns, "only_int", { ParamType::INT }, false);
}

TEST(CallbackTest, ValuesToTypes) {
    DiagnosticEngine diagnostics;
    CallbackTypeValues vals = { 1, 2.5f, std::string("hello"), true };
    
    auto types = valuesToTypes(vals, diagnostics);
    ASSERT_EQ(types.size(), 4);
    EXPECT_EQ(types[0], ParamType::INT);
    EXPECT_EQ(types[1], ParamType::FLOAT);
    EXPECT_EQ(types[2], ParamType::STRING);
    EXPECT_EQ(types[3], ParamType::BOOL);
}

TEST(CallbackTest, ValuesToTypesWithObject) {
    DiagnosticEngine diagnostics;
    auto obj = std::make_shared<Object>();
    CallbackTypeValues vals = { obj };

    auto types = valuesToTypes(vals, diagnostics);
    ASSERT_EQ(types.size(), 1u);
    EXPECT_EQ(types[0], ParamType::OBJECT);
}

TEST(MacroCallTest, RegisterAndCall) {
    DiagnosticEngine diagnostics;
    MacroCall::getInstance().registerMacro("echo",
        [](const CallbackTypeValues& args) -> std::string {
            return std::get<std::string>(args[0]);
        }, { ParamType::STRING });
    
    CallbackTypeValues args = { std::string("test123") };

    auto r1 = MacroCall::getInstance().callMacro("echo", args, {}, diagnostics);
    EXPECT_TRUE(r1.has_value());
    EXPECT_FALSE(diagnostics.hasErrors());
    EXPECT_EQ(std::get<std::string>(r1.value()), "test123");

    auto r2 = MacroCall::getInstance().callMacro("nonexistent", args, {}, diagnostics);
    EXPECT_TRUE(diagnostics.hasErrors());

    MacroCall::getInstance().unregisterMacro("echo", { ParamType::STRING }, false);
}

TEST(MacroCallTest, CombinationWithPlaceholders) {
    DiagnosticEngine diagnostics;
    MacroCall::getInstance().registerMacro("placeholder_add",
        [](const CallbackTypeValues&, const CallbackTypePlaces& places) -> TypedValue {
            return std::any_cast<int>(places.at(0)) + 10;
        }, {});

    Context ctx(32);
    CallbackTypeValues noArgs;

    auto result = MacroCall::getInstance().callMacro("placeholder_add", noArgs, ctx.params, diagnostics);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(diagnostics.hasErrors());
    EXPECT_EQ(std::get<int>(result.value()), 42);

    MacroCall::getInstance().unregisterMacro("placeholder_add", {}, true);
}

TEST(MacroCallTest, UnregisterCombinationMacro) {
    MacroCall::getInstance().registerMacro("combo_unreg",
        [](const CallbackTypeValues&, const CallbackTypePlaces&) -> TypedValue { return "ok"; }, {});

    EXPECT_TRUE(MacroCall::getInstance().isRegistered("combo_unreg", {}));

    MacroCall::getInstance().unregisterMacro("combo_unreg", {}, true);

    EXPECT_FALSE(MacroCall::getInstance().isRegistered("combo_unreg", {}));
}

TEST(ClassCallTest, NativeCounterRegistered) {
    auto& cc = ClassCall::getInstance();

    EXPECT_TRUE(cc.isRegistered("GlobalValue"));
    EXPECT_TRUE(cc.hasField("GlobalValue", "value"));
    EXPECT_FALSE(cc.hasField("GlobalValue", "nope"));
}

TEST(ClassCallTest, RegisterAndInspect) {
    auto& cc = ClassCall::getInstance();
    const std::string name = "TestNative";

    EXPECT_FALSE(cc.isRegistered(name));

    cc.registerClass(name, { "a", "b" });

    EXPECT_TRUE(cc.isRegistered(name));
    EXPECT_TRUE(cc.hasField(name, "a"));
    EXPECT_TRUE(cc.hasField(name, "b"));
    EXPECT_FALSE(cc.hasField(name, "c"));
    EXPECT_EQ(cc.getFields(name), (std::vector<std::string>{ "a", "b" }));
}

TEST(ClassCallTest, CreateAndCallMethod) {
    auto& cc = ClassCall::getInstance();
    DiagnosticEngine diagnostics;
    const std::string name = "Box";

    cc.registerClass(name, { "value" });
    cc.registerConstructor(name,
        [](const CallbackTypeValues& args) -> ll::Expected<ObjectRef> {
            auto obj = std::make_shared<Object>();
            obj->className = "Box";
            obj->fields["value"] = std::get<int>(args[0]);
            return obj;
        }, { ParamType::INT });
    cc.registerMethod(name, "get",
        [](const ObjectRef& self, const CallbackTypeValues&) -> TypedValue {
            return self->fields["value"];
        }, {});

    CallbackTypeValues args = { 7 };
    auto objResult = cc.create(name, args, {}, diagnostics);

    ASSERT_TRUE(objResult.has_value());
    EXPECT_FALSE(diagnostics.hasErrors());

    auto value = cc.callMethod(name, "get", {}, *objResult, {}, diagnostics);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(std::get<int>(value.value()), 7);

    auto missing = cc.callMethod(name, "nope", {}, *objResult, {}, diagnostics);
    EXPECT_FALSE(missing.has_value());
}

TEST(ClassCallTest, RegisterFieldAndStaticMembers) {
    auto& cc = ClassCall::getInstance();
    const std::string name = "NativeMembers";

    cc.registerClass(name, {});
    cc.registerField(name, "count", 10);
    cc.registerStaticField(name, "version", 1);

    EXPECT_TRUE(cc.hasField(name, "count"));
    EXPECT_FALSE(cc.hasField(name, "nope"));
    EXPECT_TRUE(cc.hasStaticField(name, "version"));
    EXPECT_FALSE(cc.hasStaticField(name, "nope"));
    EXPECT_EQ(cc.getFields(name), (std::vector<std::string>{ "count" }));
    EXPECT_EQ(cc.getStaticFields(name), (std::vector<std::string>{ "version" }));

    DiagnosticEngine diagnostics;
    auto objResult = cc.create(name, {}, {}, diagnostics);
    ASSERT_TRUE(objResult.has_value());
    EXPECT_EQ(std::get<int>((*objResult)->fields["count"]), 10);

    auto staticValue = cc.getStaticField(name, "version");
    ASSERT_TRUE(staticValue.has_value());
    EXPECT_EQ(std::get<int>(staticValue.value()), 1);

    cc.setStaticField(name, "version", 2);
    staticValue = cc.getStaticField(name, "version");
    ASSERT_TRUE(staticValue.has_value());
    EXPECT_EQ(std::get<int>(staticValue.value()), 2);

    cc.registerStaticMethod(name, "double",
        [](const CallbackTypeValues& args) -> TypedValue {
            return std::get<int>(args[0]) * 2;
        }, { ParamType::INT });

    auto signatures = cc.getStaticMethodSignatures(name, "double");
    ASSERT_EQ(signatures.size(), 1u);
    EXPECT_EQ(signatures[0], (CallbackTypeArgs{ ParamType::INT }));

    auto result = cc.callStaticMethod(name, "double", { 5 }, {}, diagnostics);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(diagnostics.hasErrors());
    EXPECT_EQ(std::get<int>(result.value()), 10);

    cc.registerStaticMethod(name, "placeholder",
        [](const CallbackTypeValues&, const CallbackTypePlaces& places) -> TypedValue {
            return std::any_cast<int>(places.at(0)) + 1;
        }, {});

    Context ctx(41);
    auto combo = cc.callStaticMethod(name, "placeholder", {}, ctx.params, diagnostics);
    ASSERT_TRUE(combo.has_value());
    EXPECT_FALSE(diagnostics.hasErrors());
    EXPECT_EQ(std::get<int>(combo.value()), 42);
}

TEST(FunctionCallCacheTest, CacheHitSkipsRegistryLookup) {
    DiagnosticEngine diagnostics;
    auto& fc = FunctionCall::getInstance();
    const std::string ns = "cache_hit_ns";
    const CallbackTypeArgs sig = { ParamType::INT };

    fc.registerFunction(ns, "answer",
        [](const CallbackTypeValues& v) -> TypedValue {
            return std::get<int>(v[0]) + 1;
        }, sig);

    FunctionCallCacheSlot slot;
    CallbackTypeValues args = { 41 };

    auto first = fc.callFunctionCached(ns, "answer", args, {}, slot, diagnostics);
    ASSERT_TRUE(first.has_value());
    EXPECT_FALSE(diagnostics.hasErrors());
    EXPECT_EQ(std::get<int>(first.value()), 42);
    EXPECT_TRUE(slot.valid);

    slot.callback = [](const CallbackTypeValues&) -> TypedValue { return 999; };

    auto second = fc.callFunctionCached(ns, "answer", args, {}, slot, diagnostics);
    ASSERT_TRUE(second.has_value());
    EXPECT_FALSE(diagnostics.hasErrors());
    EXPECT_EQ(std::get<int>(second.value()), 999);

    fc.unregisterFunction(ns, "answer", sig, false);
}

TEST(FunctionCallCacheTest, ReregistrationInvalidatesSlot) {
    DiagnosticEngine diagnostics;
    auto& fc = FunctionCall::getInstance();
    const std::string ns = "cache_invalidate_ns";
    const CallbackTypeArgs sig = { ParamType::INT };

    fc.registerFunction(ns, "val",
        [](const CallbackTypeValues&) -> TypedValue { return 1; }, sig);

    FunctionCallCacheSlot slot;
    CallbackTypeValues args = { 0 };

    auto first = fc.callFunctionCached(ns, "val", args, {}, slot, diagnostics);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(std::get<int>(first.value()), 1);
    EXPECT_TRUE(slot.valid);

    fc.registerFunction(ns, "val",
        [](const CallbackTypeValues&) -> TypedValue { return 2; }, sig);

    auto second = fc.callFunctionCached(ns, "val", args, {}, slot, diagnostics);
    ASSERT_TRUE(second.has_value());
    EXPECT_FALSE(diagnostics.hasErrors());
    EXPECT_EQ(std::get<int>(second.value()), 2);

    fc.unregisterFunction(ns, "val", sig, false);
}

TEST(FunctionCallCacheTest, ShapeChangeReDispatches) {
    DiagnosticEngine diagnostics;
    auto& fc = FunctionCall::getInstance();
    const std::string ns = "cache_shape_ns";

    fc.registerFunction(ns, "describe",
        [](const CallbackTypeValues&) -> TypedValue { return std::string("int"); },
        { ParamType::INT });
    fc.registerFunction(ns, "describe",
        [](const CallbackTypeValues&) -> TypedValue { return std::string("float"); },
        { ParamType::FLOAT });

    FunctionCallCacheSlot slot;

    auto intResult = fc.callFunctionCached(ns, "describe", { 1 }, {}, slot, diagnostics);
    ASSERT_TRUE(intResult.has_value());
    EXPECT_EQ(std::get<std::string>(intResult.value()), "int");

    auto floatResult = fc.callFunctionCached(ns, "describe", { 1.5f }, {}, slot, diagnostics);
    ASSERT_TRUE(floatResult.has_value());
    EXPECT_EQ(std::get<std::string>(floatResult.value()), "float");

    auto backResult = fc.callFunctionCached(ns, "describe", { 2 }, {}, slot, diagnostics);
    ASSERT_TRUE(backResult.has_value());
    EXPECT_EQ(std::get<std::string>(backResult.value()), "int");

    EXPECT_FALSE(diagnostics.hasErrors());

    fc.unregisterFunction(ns, "describe", { ParamType::INT }, false);
    fc.unregisterFunction(ns, "describe", { ParamType::FLOAT }, false);
}

TEST(FunctionCallCacheTest, CombinationCallbackCached) {
    DiagnosticEngine diagnostics;
    auto& fc = FunctionCall::getInstance();
    const std::string ns = "cache_combo_ns";

    fc.registerFunction(ns, "scaled",
        [](const CallbackTypeValues&, const CallbackTypePlaces& places) -> TypedValue {
            return std::any_cast<int>(places.at(0)) * 3;
        }, {});

    FunctionCallCacheSlot slot;
    Context ctx(14);

    for (int i = 0; i < 2; ++i) {
        auto result = fc.callFunctionCached(ns, "scaled", {}, ctx.params, slot, diagnostics);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(std::get<int>(result.value()), 42);
    }

    EXPECT_TRUE(slot.valid);
    EXPECT_TRUE(slot.isCombination);
    EXPECT_FALSE(diagnostics.hasErrors());

    fc.unregisterFunction(ns, "scaled", {}, true);
}

TEST(ClassCallCacheTest, ConstructorCacheCreatesFreshObjects) {
    auto& cc = ClassCall::getInstance();
    DiagnosticEngine diagnostics;
    const std::string name = "CacheBox";

    cc.registerClass(name, {});
    cc.registerConstructor(name,
        [](const CallbackTypeValues& args) -> ll::Expected<ObjectRef> {
            auto obj = std::make_shared<Object>();
            obj->className = "CacheBox";
            obj->fields["value"] = std::get<int>(args[0]);
            return obj;
        }, { ParamType::INT });

    NativeConstructorCacheSlot slot;

    auto first = cc.createCached(name, { 1 }, {}, slot, diagnostics);
    auto second = cc.createCached(name, { 2 }, {}, slot, diagnostics);

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(*first, *second);
    EXPECT_EQ(std::get<int>((*first)->fields["value"]), 1);
    EXPECT_EQ(std::get<int>((*second)->fields["value"]), 2);
    EXPECT_FALSE(diagnostics.hasErrors());
}

TEST(ClassCallCacheTest, MethodCacheFollowsReceiverClass) {
    auto& cc = ClassCall::getInstance();
    DiagnosticEngine diagnostics;

    for (const std::string& name : { "CacheLeft", "CacheRight" }) {
        cc.registerClass(name, {});
        cc.registerMethod(name, "who",
            [name](const ObjectRef&, const CallbackTypeValues&) -> TypedValue {
                return name;
            }, {});
    }

    auto left = std::make_shared<Object>();
    left->className = "CacheLeft";
    auto right = std::make_shared<Object>();
    right->className = "CacheRight";

    NativeMethodCacheSlot slot;

    auto l1 = cc.callMethodCached("CacheLeft", "who", {}, left, {}, slot, diagnostics);
    auto r1 = cc.callMethodCached("CacheRight", "who", {}, right, {}, slot, diagnostics);
    auto l2 = cc.callMethodCached("CacheLeft", "who", {}, left, {}, slot, diagnostics);
    auto r2 = cc.callMethodCached("CacheRight", "who", {}, right, {}, slot, diagnostics);

    ASSERT_TRUE(l1.has_value());
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(l2.has_value());
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(std::get<std::string>(l1.value()), "CacheLeft");
    EXPECT_EQ(std::get<std::string>(r1.value()), "CacheRight");
    EXPECT_EQ(std::get<std::string>(l2.value()), "CacheLeft");
    EXPECT_EQ(std::get<std::string>(r2.value()), "CacheRight");
    EXPECT_FALSE(diagnostics.hasErrors());
}

TEST(ClassCallCacheTest, MethodReregistrationInvalidatesSlot) {
    auto& cc = ClassCall::getInstance();
    DiagnosticEngine diagnostics;
    const std::string name = "CacheFlip";

    cc.registerClass(name, {});
    cc.registerMethod(name, "get",
        [](const ObjectRef&, const CallbackTypeValues&) -> TypedValue { return 1; }, {});

    auto obj = std::make_shared<Object>();
    obj->className = name;

    NativeMethodCacheSlot slot;

    auto first = cc.callMethodCached(name, "get", {}, obj, {}, slot, diagnostics);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(std::get<int>(first.value()), 1);
    EXPECT_TRUE(slot.valid);

    cc.registerMethod(name, "get",
        [](const ObjectRef&, const CallbackTypeValues&) -> TypedValue { return 2; }, {});

    auto second = cc.callMethodCached(name, "get", {}, obj, {}, slot, diagnostics);
    ASSERT_TRUE(second.has_value());
    EXPECT_FALSE(diagnostics.hasErrors());
    EXPECT_EQ(std::get<int>(second.value()), 2);
}

TEST(ClassCallCacheTest, StaticAndValueMethodCache) {
    auto& cc = ClassCall::getInstance();
    DiagnosticEngine diagnostics;
    const std::string name = "CacheStatics";

    cc.registerClass(name, {});
    cc.registerStaticMethod(name, "double",
        [](const CallbackTypeValues& v) -> TypedValue {
            return std::get<int>(v[0]) * 2;
        }, { ParamType::INT });
    cc.registerValueMethod("CacheValueHost", "len",
        [](const TypedValue& self, const CallbackTypeValues&) -> TypedValue {
            return static_cast<int>(std::get<std::string>(self).size());
        }, {});

    NativeStaticMethodCacheSlot staticSlot;
    auto s1 = cc.callStaticMethodCached(name, "double", { 21 }, {}, staticSlot, diagnostics);
    auto s2 = cc.callStaticMethodCached(name, "double", { 21 }, {}, staticSlot, diagnostics);
    ASSERT_TRUE(s1.has_value());
    ASSERT_TRUE(s2.has_value());
    EXPECT_EQ(std::get<int>(s1.value()), 42);
    EXPECT_EQ(std::get<int>(s2.value()), 42);

    NativeValueMethodCacheSlot valueSlot;
    auto v1 = cc.callValueMethodCached("CacheValueHost", "len", { std::string("abcd") }, {}, valueSlot, diagnostics);
    auto v2 = cc.callValueMethodCached("CacheValueHost", "len", { std::string("abcd") }, {}, valueSlot, diagnostics);
    ASSERT_TRUE(v1.has_value());
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(std::get<int>(v1.value()), 4);
    EXPECT_EQ(std::get<int>(v2.value()), 4);

    EXPECT_FALSE(diagnostics.hasErrors());
}
