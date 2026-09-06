# LOICollectionAPI Extension Guide

> [!NOTE]
> The following content is derived from the code structure of LOICollectionA 1.17.0 and may differ in later versions.

This article explains how to extend LOICollectionAPI with C++ — that is, registering custom variables, functions, and classes for LCUI scripts and string templates (the `{variable}` syntax).

> [!TIP]
> If you are a **script user** (using LOICollectionAPI in `.lcui` or configuration strings), please read [LOICollectionAPI](../md/api.md) and [LCUI Script Syntax](../md/lcui.md). This article is intended for C++ developers who wish to **extend** these capabilities.

## Type System

Value types exchanged between scripts and C++:

```cpp
using TypedValue = std::variant<int, float, std::string, bool, ObjectRef, FunctionRefPtr, ArrayRef, std::monostate>;

enum class ParamType { INT, FLOAT, STRING, BOOL, OBJECT, FUNCTION, ARRAY };

using CallbackTypeArgs   = std::vector<ParamType>;          // parameter type signature
using CallbackTypeValues = std::vector<TypedValue>;         // actual argument values
using CallbackTypePlaces = std::unordered_map<int, std::any>; // context placeholders (e.g. the current player)
```

- `TypedValue` is a `std::variant`; use `std::get<T>` or `std::visit` to read it
- `std::monostate` represents "empty" (`None` in scripts)
- `CallbackTypePlaces` carries the script execution context; index `0` is currently defined as `std::reference_wrapper<Player>` (the current player)

## Registering Variables (String Template `{xxx}`)

`LOICollectionAPI::CallbackUtils::registerVariable` has 4 overloads, corresponding to the four usages in scripts:

| Overload | Script Usage | Description |
| --- | --- | --- |
| `registerVariable(name, f())` | `{name}` | Parameterless variable, e.g. `{server_tps}` |
| `registerVariable(name, f(Player&))` | `{name}` | Player-context variable, e.g. `{player}` |
| `registerVariable(name, f(CallbackTypeValues), args)` | `{name(args)}` | Variable with parameters, e.g. `{tr(languageId)}` |
| `registerVariable(name, f(Player&, CallbackTypeValues), args)` | `{name(args)}` | Player context + parameters |

> [!NOTE]
> When a variable is not registered, `getValueForVariable` returns the string `"None"`. Registering a variable also automatically registers a macro with the same name (`MacroCall`), so both `{name}` and `name()` can be used in scripts.

Example (see `modules/CallbackUtils.cpp`):

```cpp
#include "LOICollectionA/include/CallbackUtils.h"

// parameterless variable
CallbackUtils::getInstance().registerVariable("server_tps", []() -> ll::Expected<frontend::TypedValue> {
    return ServerStatus::getInstance().getTps();   // returns int/float/string, etc.
});

// player variable
CallbackUtils::getInstance().registerVariable("player_realname",
    [](Player& player) -> ll::Expected<frontend::TypedValue> {
        return player.getRealName();
    });

// variable with parameters: {score(ScoreboardName)}
CallbackUtils::getInstance().registerVariable("score",
    [](const frontend::CallbackTypeValues& args) -> ll::Expected<frontend::TypedValue> {
        const auto& name = std::get<std::string>(args[0]);
        return getScore(name);
    }, { frontend::ParamType::STRING });
```

## Registering Functions (`namespace::function`)

`frontend::FunctionCall::registerFunction` registers namespace functions; the same signature can be overloaded:

```cpp
#include "LOICollectionA/frontend/Callback.h"
using namespace LOICollection::frontend;

ll::Expected<TypedValue> myAbs(const CallbackTypeValues& args) {
    return std::visit([](auto&& arg) -> ll::Expected<TypedValue> {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int> || std::is_same_v<T, float>)
            return std::abs(arg);
        return ll::makeStringError("my::abs: requires a numeric argument");
    }, args[0]);
}

void registerMyFunctions(const std::string& namespaces) {
    FunctionCall& functions = FunctionCall::getInstance();
    functions.registerFunction(namespaces, "abs", myAbs, { ParamType::INT });
    functions.registerFunction(namespaces, "abs", myAbs, { ParamType::FLOAT });
}
```

> [!TIP]
> See the real implementations: how functions such as `math::abs` and `math::min` are registered in `frontend/builtin/MathBuiltin.cpp`; `frontend/builtin/mc/server/CommandBuiltin.cpp` demonstrates server functions such as `mc::runCmd`.

### Combination Variant

When you need to access the script context (the player), use `CallbackFuncCombination` (with an extra `CallbackTypePlaces` parameter) and the corresponding `registerFunction` overload:

```cpp
functions.registerFunction(namespaces, "runCmd", myRunCmd, { ParamType::STRING });
// myRunCmd: ll::Expected<TypedValue>(const CallbackTypeValues&, const CallbackTypePlaces& placeholders)
// get the current player: std::any_cast<std::reference_wrapper<Player>>(placeholders.at(0))
```

## Registering Macros (`name(args)`)

`frontend::MacroCall::registerMacro` registers macros that can be called directly in expressions and string templates. It is usually not needed directly — `registerVariable` automatically registers a macro with the same name. Use it when you need custom argument validation or a macro independent of variables:

```cpp
MacroCall::getInstance().registerMacro("entity",
    [](const CallbackTypeValues& args) -> ll::Expected<TypedValue> {
        return countEntities(std::get<std::string>(args[0]));
    }, { ParamType::STRING });
```

## Registering Native Classes (`new ClassName(...)`)

`frontend::ClassCall` provides native classes to scripts. Take `CustomForm` as an example (see `frontend/builtin/ui/form/CustomFormClass.cpp`):

```cpp
#include "LOICollectionA/frontend/Callback.h"
using namespace LOICollection::frontend;

// constructor: returns ObjectRef (native object handle)
ll::Expected<ObjectRef> makeMyClass(const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
    auto handle = std::make_shared<MyClassHandle>();       // custom handle
    auto obj = std::make_shared<Object>();
    obj->className = "MyClass";
    obj->classIndex = -1;
    obj->native = handle;                                   // store the native handle in Object
    return obj;
}

// instance method: the first parameter is self (ObjectRef); retrieve the handle from native
ll::Expected<TypedValue> myMethod(const ObjectRef& self, const CallbackTypeValues& args) {
    auto* handle = static_cast<MyClassHandle*>(self->native.get());
    return handle->doSomething();
}

void registerMyClass(const std::string&) {
    ClassCall& classes = ClassCall::getInstance();
    classes.registerClass("MyClass", { "field1", "field2" });              // declare fields
    classes.registerConstructor("MyClass", makeMyClass, { ParamType::STRING });
    classes.registerMethod("MyClass", "myMethod", myMethod, {});
    classes.registerStaticMethod("MyClass", "staticFn", myStatic, { ParamType::INT });
}
```

| ClassCall method | Description |
| --- | --- |
| `registerClass(name, fields)` | Registers a class and its instance fields |
| `registerConstructor(name, cb, args)` | Registers a constructor (multiple overloads allowed) |
| `registerMethod(className, method, cb, args)` | Registers an instance method (multiple overloads allowed) |
| `registerStaticMethod(className, method, cb, args)` | Registers a static method |
| `registerField(className, field, defaultValue?)` | Appends a field (with optional default value) |
| `registerStaticField(className, field, defaultValue?)` | Appends a static field |

> [!NOTE]
> A native method returning `self` enables chained calls in scripts (such as `form.label(...).button(...)`). The `Object` structure contains `className`, `classIndex`, and `native` (`std::any`, which stores any native handle).

## Registration Entry Point: the REGISTER_CALLBACK Macro

The macro at the end of `frontend/Callback.h` invokes registration functions during **static initialization**:

```cpp
#define REGISTER_CALLBACK(NAME, BINDER) const auto NAME##_RegisterHelper = []() -> bool {     BINDER(#NAME);     return true; }();

// usage: the namespaces parameter will be passed "math"
REGISTER_CALLBACK(math, MathBuiltin::registerFunctions)
REGISTER_CALLBACK(CustomForm, CustomFormClass::registerClasses)
```

Therefore, registration functions should have the signature `void(const std::string&)`, where the string parameter is the namespace name (for functions) or the class-name prefix (for classes).

## Complete Example: Registering a Namespace with Variables

```cpp
// MyBuiltin.cpp
#include <ll/api/Expected.h>
#include "LOICollectionA/frontend/Callback.h"
#include "LOICollectionA/include/CallbackUtils.h"

using namespace LOICollection::frontend;
using namespace LOICollection::LOICollectionAPI;

namespace MyBuiltin {
    ll::Expected<TypedValue> greet(const CallbackTypeValues&, const CallbackTypePlaces& places) {
        auto& player = std::any_cast<std::reference_wrapper<Player>>(places.at(0));
        return "Hello, " + player.getRealName();
    }

    void registerFunctions(const std::string& ns) {
        FunctionCall::getInstance().registerFunction(ns, "greet", greet, {});
    }
}

REGISTER_CALLBACK(my, MyBuiltin::registerFunctions)
```

After registration, scripts can use `my::greet()` and the `{variable}` syntax. After making changes, run `/xxx reload` or restart the server for the changes to take effect.

## Correspondence with the Script-Side Documentation

| This article (C++ registration) | Script-side usage documentation |
| --- | --- |
| `registerVariable` | [Default Variables and Functions](../md/api.md) |
| `registerFunction` | [Built-in Namespaces and Functions](../md/api.md) |
| `ClassCall` | [Native UI Classes](../md/native-ui.md) and [Classes and Inheritance](../md/api.md) |
