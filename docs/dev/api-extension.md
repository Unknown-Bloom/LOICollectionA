# LOICollectionAPI 扩展指南

> [!NOTE]
> 以下内容取自 LOICollectionA 1.17.0 的代码结构，对于后续版本可能会有所不同。

本文介绍如何用 C++ 扩展 LOICollectionAPI——即向 LCUI 脚本与字符串模板（`{变量}` 语法）注册自定义的变量、函数与类。

> [!TIP]
> 若您是 **脚本使用者**（在 `.lcui` 或配置字符串中使用 LOICollectionAPI），请阅读 [LOICollectionAPI](../md/api.md) 与 [LCUI 脚本语法](../md/lcui.md)。本文面向希望**扩展**这些能力的 C++ 开发者。

## 类型系统

脚本与 C++ 交互的值类型：

```cpp
using TypedValue = std::variant<int, float, std::string, bool, ObjectRef, FunctionRefPtr, ArrayRef, std::monostate>;

enum class ParamType { INT, FLOAT, STRING, BOOL, OBJECT, FUNCTION, ARRAY };

using CallbackTypeArgs   = std::vector<ParamType>;          // 参数类型签名
using CallbackTypeValues = std::vector<TypedValue>;         // 实际参数值
using CallbackTypePlaces = std::unordered_map<int, std::any>; // 上下文占位符（如当前玩家）
```

- `TypedValue` 是 `std::variant`，读取时用 `std::get<T>` 或 `std::visit`
- `std::monostate` 表示"空值"（脚本中的 `None`）
- `CallbackTypePlaces` 携带脚本执行上下文，目前约定索引 `0` 为 `std::reference_wrapper<Player>`（当前玩家）

## 注册变量（字符串模板 `{xxx}`）

`LOICollectionAPI::CallbackUtils::registerVariable` 有 4 种重载，对应脚本中的四种用法：

| 重载 | 脚本用法 | 说明 |
| --- | --- | --- |
| `registerVariable(name, f())` | `{name}` | 无参变量，如 `{server_tps}` |
| `registerVariable(name, f(Player&))` | `{name}` | 玩家上下文变量，如 `{player}` |
| `registerVariable(name, f(CallbackTypeValues), args)` | `{name(参数)}` | 带参数变量，如 `{tr(languageId)}` |
| `registerVariable(name, f(Player&, CallbackTypeValues), args)` | `{name(参数)}` | 玩家上下文 + 参数 |

> [!NOTE]
> 变量未注册时，`getValueForVariable` 返回字符串 `"None"`。变量注册的同时会自动注册为同名宏（`MacroCall`），因此脚本中 `{name}` 与 `name()` 均可使用。

示例（参考 `modules/CallbackUtils.cpp`）：

```cpp
#include "LOICollectionA/include/CallbackUtils.h"

// 无参变量
CallbackUtils::getInstance().registerVariable("server_tps", []() -> ll::Expected<frontend::TypedValue> {
    return ServerStatus::getInstance().getTps();   // 返回 int/float/string 等
});

// 玩家变量
CallbackUtils::getInstance().registerVariable("player_realname",
    [](Player& player) -> ll::Expected<frontend::TypedValue> {
        return player.getRealName();
    });

// 带参数变量：{score(ScoreboardName)}
CallbackUtils::getInstance().registerVariable("score",
    [](const frontend::CallbackTypeValues& args) -> ll::Expected<frontend::TypedValue> {
        const auto& name = std::get<std::string>(args[0]);
        return getScore(name);
    }, { frontend::ParamType::STRING });
```

## 注册函数（`namespace::function`）

`frontend::FunctionCall::registerFunction` 注册命名空间函数，签名相同即可重载：

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
> 参考真实实现：`frontend/builtin/MathBuiltin.cpp` 中 `math::abs`、`math::min` 等函数的注册方式；`frontend/builtin/mc/server/CommandBuiltin.cpp` 演示了 `mc::runCmd` 这类服务端函数。

### Combination 变体

需要访问脚本上下文（玩家）时，使用 `CallbackFuncCombination`（多一个 `CallbackTypePlaces` 参数）与对应的 `registerFunction` 重载：

```cpp
functions.registerFunction(namespaces, "runCmd", myRunCmd, { ParamType::STRING });
// myRunCmd: ll::Expected<TypedValue>(const CallbackTypeValues&, const CallbackTypePlaces& placeholders)
// 取当前玩家：std::any_cast<std::reference_wrapper<Player>>(placeholders.at(0))
```

## 注册宏（`name(args)`）

`frontend::MacroCall::registerMacro` 注册可在表达式与字符串模板中直接调用的宏。通常不需要直接使用——`registerVariable` 会自动注册同名宏。需要自定义参数校验或独立于变量的宏时使用：

```cpp
MacroCall::getInstance().registerMacro("entity",
    [](const CallbackTypeValues& args) -> ll::Expected<TypedValue> {
        return countEntities(std::get<std::string>(args[0]));
    }, { ParamType::STRING });
```

## 注册原生类（`new ClassName(...)`）

`frontend::ClassCall` 为脚本提供原生类。以 `CustomForm` 为例（参考 `frontend/builtin/ui/form/CustomFormClass.cpp`）：

```cpp
#include "LOICollectionA/frontend/Callback.h"
using namespace LOICollection::frontend;

// 构造器：返回 ObjectRef（原生对象句柄）
ll::Expected<ObjectRef> makeMyClass(const CallbackTypeValues& args, const CallbackTypePlaces& placeholders) {
    auto handle = std::make_shared<MyClassHandle>();       // 自定义句柄
    auto obj = std::make_shared<Object>();
    obj->className = "MyClass";
    obj->classIndex = -1;
    obj->native = handle;                                   // 原生句柄存入 Object
    return obj;
}

// 实例方法：第一个参数为 self（ObjectRef），native 中取回句柄
ll::Expected<TypedValue> myMethod(const ObjectRef& self, const CallbackTypeValues& args) {
    auto* handle = static_cast<MyClassHandle*>(self->native.get());
    return handle->doSomething();
}

void registerMyClass(const std::string&) {
    ClassCall& classes = ClassCall::getInstance();
    classes.registerClass("MyClass", { "field1", "field2" });              // 声明字段
    classes.registerConstructor("MyClass", makeMyClass, { ParamType::STRING });
    classes.registerMethod("MyClass", "myMethod", myMethod, {});
    classes.registerStaticMethod("MyClass", "staticFn", myStatic, { ParamType::INT });
}
```

| ClassCall 方法 | 说明 |
| --- | --- |
| `registerClass(name, fields)` | 注册类与实例字段 |
| `registerConstructor(name, cb, args)` | 注册构造器（可多个重载） |
| `registerMethod(className, method, cb, args)` | 注册实例方法（可多个重载） |
| `registerStaticMethod(className, method, cb, args)` | 注册静态方法 |
| `registerField(className, field, defaultValue?)` | 追加字段（可带默认值） |
| `registerStaticField(className, field, defaultValue?)` | 追加静态字段 |

> [!NOTE]
> 原生方法返回 `self` 可实现脚本中的链式调用（如 `form.label(...).button(...)`）。`Object` 结构包含 `className`、`classIndex` 与 `native`（`std::any`，存放任意原生句柄）。

## 注册入口：REGISTER_CALLBACK 宏

`frontend/Callback.h` 末尾的宏在**静态初始化**阶段调用注册函数：

```cpp
#define REGISTER_CALLBACK(NAME, BINDER) const auto NAME##_RegisterHelper = []() -> bool {     BINDER(#NAME);     return true; }();

// 使用：namespaces 参数将传入 "math"
REGISTER_CALLBACK(math, MathBuiltin::registerFunctions)
REGISTER_CALLBACK(CustomForm, CustomFormClass::registerClasses)
```

因此注册函数应具有签名 `void(const std::string&)`，其中字符串参数即命名空间名（函数）或类名前缀（类）。

## 完整示例：注册一个带变量的命名空间

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

注册后，脚本即可使用 `my::greet()` 与 `{变量}` 语法。修改后执行 `/xxx reload` 或重启服务器生效。

## 与脚本侧文档的对应关系

| 本文（C++ 注册） | 脚本侧使用文档 |
| --- | --- |
| `registerVariable` | [默认变量与函数](../md/api.md) |
| `registerFunction` | [内置命名空间与函数](../md/api.md) |
| `ClassCall` | [原生 UI 类](../md/native-ui.md) 与 [类与继承](../md/api.md) |
