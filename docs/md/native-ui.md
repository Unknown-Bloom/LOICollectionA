# 原生 UI（Native UI）

> [!NOTE]
> 以下内容取自 LOICollectionA 1.15.0 的原生 UI 结构，对于后续版本可能会有所不同。

## 概述

原生 UI 是脚本语言（`.lcui`）中直接封装 LeviLamina 原生表单与界面能力的类集合，包括：

- 表单：`CustomForm`、`MessageBox`、`PaginatedForm`
- 可观察数据：`ObservableString`、`ObservableNumber`、`ObservableBoolean`、`ObservableUIRawMessage`
- 原始消息：`UIRawMessage`
- 控件选项：`TextOptions`、`ButtonOptions`、`TextFieldOptions`、`DropdownOptions`、`DropdownItem`、`SliderOptions`、`ToggleOptions`、`DividerOptions`、`SpacingOptions`

与 `MenuForm`、`ShopForm` 这类业务表单不同，原生 UI 不包含权限、Score 等业务逻辑，提供的是最底层的表单构建能力。所有模块的内置界面（如 `blacklist.lcui`、`statistics.lcui`）以及 Menu/Shop 的 `menu.lcui`、`shop.lcui` 均构建在原生 UI 之上。

## 表单生命周期与 GUIManager

创建表单时（如 `new CustomForm(id, title)`），表单会以 `id` 为键、按玩家 UUID 注册到 `GUIManager` 中。表单关闭后会自动注销；同一玩家再次创建相同 `id` 的表单会覆盖旧实例。

| 函数 | 说明 |
| --- | --- |
| `GUIManager::open(id, formId, type[, ctx])` | 执行已加载的 lcui 脚本，并打开 `id` 下注册的 `formId` 表单 |
| `GUIManager::switchTo(id, type)` | 直接切换到 `id` 下已注册的表单，不重新执行脚本 |
| `GUIManager::value(id)` | 获取插件注册的 GUI 值（返回数组） |
| `GUIManager::request(id, args)` | 请求插件注册的 GUI 数据（返回数组） |
| `GUIManager::callback(id, args)` | 调用插件注册的 GUI 回调 |

`GUIManager::open` 与 `GUIManager::switchTo` 中的 `type` 为整数：

| type | 表单类型 |
| --- | --- |
| 1 | CustomForm（自定义表单） |
| 2 | MessageBox（对话框） |
| 3 | PaginatedForm（分页表单） |
| 4 | ScriptForm（脚本表单，MenuForm/ShopForm 等） |

`GUIManager::open` 可传入可选的 `ctx` 数组，脚本中通过 `new CtxValue(索引)` 获取对应元素。

> [!TIP]
> 插件模块在启动时会将各自的 `value`、`request`、`callback` 注册到 GUIManager，例如 `blacklist.players`、`market.items` 等。具体注册 ID 请查阅对应模块的 lcui 文件。

## 通用约定

原生 UI 的文本、数值与布尔参数支持直接值或可观察对象：

| 参数类型 | 可接受的值 |
| --- | --- |
| 文本（TextValue） | `string`、`UIRawMessage`、`ObservableString`、`ObservableUIRawMessage` |
| 数值（NumberValue） | `int`、`float`、`ObservableNumber` |
| 布尔（BooleanValue） | `bool`、`ObservableBoolean` |

选项类均通过 `new XxxOptions()` 创建，字段为可选项，未设置的字段使用原生默认值。

表单关闭后，`show` 回调会收到关闭结果；所有 `show` 回调必须恰好接收一个参数，按钮回调不能接收参数。

> [!WARNING]
> 当关闭结果为 `none`（无关闭原因）时，直接与数值比较会触发 `Cannot compare an empty optional value` 错误。建议先通过 `if (值)` 判断真值，或仅在确定存在值时进行比较。

### 关闭原因（closeReason）

关闭原因取自 Minecraft 原生的 `DataDrivenScreenClosedReason`：

| 值 | 含义 |
| --- | --- |
| 0 | 程序主动关闭（ProgrammaticClose） |
| 1 | 程序关闭全部（ProgrammaticCloseAll） |
| 2 | 玩家取消/关闭界面（ClientCanceled） |
| 3 | 玩家正忙（UserBusy） |
| 4 | 表单无效（InvalidForm） |

## CustomForm（自定义表单）

自定义表单是最常用的原生表单，支持标题、文本、输入框、下拉框、开关、滑块、按钮等控件。

### CustomForm 构造

```lcui
form = new CustomForm(id, title);
```

其中 `id` 为注册 ID，`title` 为表单标题（支持 string 或 UIRawMessage/ObservableString）。

### CustomForm 方法

| 方法 | 说明 |
| --- | --- |
| `header(text, options)` | 添加标题文本，`options` 为 `TextOptions` |
| `label(text, options)` | 添加标签文本，`options` 为 `TextOptions` |
| `divider(options)` | 添加分割线，`options` 为 `DividerOptions` |
| `spacer(options)` | 添加空白间距，`options` 为 `SpacingOptions` |
| `textField(label, observable, options)` | 添加输入框，`observable` 为 `ObservableString`，`options` 为 `TextFieldOptions` |
| `dropdown(label, observable, items, options)` | 添加下拉框，`observable` 为 `ObservableNumber`（选中项索引），`items` 为 `DropdownItem` 数组，`options` 为 `DropdownOptions` |
| `toggle(label, observable, options)` | 添加开关，`observable` 为 `ObservableBoolean`，`options` 为 `ToggleOptions` |
| `slider(label, observable, min, max, options)` | 添加滑块，`observable` 为 `ObservableNumber`，`options` 为 `SliderOptions` |
| `button(label, callback, options)` | 添加按钮，`callback` 为无参函数，`options` 为 `ButtonOptions` |
| `closeButton()` | 添加关闭按钮 |
| `show(callback)` | 显示表单，`callback` 接收一个参数（`closeReason`，类型为 int 或 `none`） |
| `close()` | 主动关闭表单 |
| `isShowing()` | 返回表单是否正在显示 |

### CustomForm 示例

```lcui
input = new ObservableString("", true);
count = new ObservableNumber(1, false);
enabled = new ObservableBoolean(true, true);

form = new CustomForm("example", "Native UI Example");
form.header("Hello", new TextOptions());
form.label("This is a native custom form", new TextOptions());
form.divider(new DividerOptions());
form.textField("Input", input, new TextFieldOptions());
form.slider("Count", count, 1, 10, new SliderOptions());
form.toggle("Enabled", enabled, new ToggleOptions());
form.button("Submit", func () -> void {
    mc::runCmd("say " + input.getData());
}, new ButtonOptions());
form.closeButton();

form.show(func (closeReason) -> void {
    if (closeReason == 2) [
        mc::runCmd("say closed");
    ]
});
```

> [!TIP]
> 说实话，世界真的很美，多出去走走能看到不一样的天地o(*￣▽￣*)o

## MessageBox（对话框）

对话框用于向玩家展示一段内容并让玩家在按钮 1 / 按钮 2 之间选择。

### MessageBox 构造

```lcui
box = new MessageBox(id, title);
```

### MessageBox 方法

| 方法 | 说明 |
| --- | --- |
| `body(text)` | 设置对话框正文内容 |
| `button1(label[, tooltip])` | 设置按钮 1 的文本与可选提示 |
| `button2(label[, tooltip])` | 设置按钮 2 的文本与可选提示 |
| `show(callback)` | 显示对话框，`callback` 接收一个 `MessageBoxResult` 参数 |
| `close()` | 主动关闭对话框 |
| `isShowing()` | 返回对话框是否正在显示 |

`MessageBoxResult` 字段：

| 字段 | 说明 |
| --- | --- |
| `closeReason` | 关闭原因（见上文取值表） |
| `selection` | 选择的按钮：`0` 为按钮 1，`1` 为按钮 2，未选择时为 `none` |

### MessageBox 示例

```lcui
box = new MessageBox("confirm", "Confirm");
box.body("Are you sure?");
box.button1("Yes");
box.button2("No");
box.show(func (result) -> void {
    if (result.selection) [
        mc::runCmd("say Button 2 selected");
    : 
        mc::runCmd("say Button 1 selected or no selection");
    ]
});
```

> [!NOTE]
> `selection` 为 `0`（按钮 1）时真值为假，`1`（按钮 2）时真值为真，`none`（未选择）时真值为假；`none` 不能与数值直接比较。

## PaginatedForm（分页表单）

分页表单用于展示大量条目，按页显示，并提供上一页、下一页与页码跳转能力。

### PaginatedForm 构造

```lcui
form = new PaginatedForm(guiId, title, elements[, pageSize]);
```

其中 `elements` 为字符串数组（每个字符串为一项），`pageSize` 为每页条数，默认 `10`，最小为 `1`。

### PaginatedForm 方法

| 方法 | 说明 |
| --- | --- |
| `previous()` | 切换到上一页 |
| `next()` | 切换到下一页 |
| `choose(page)` | 跳转到指定页 |
| `previousButton(text)` | 添加上一页按钮（自定义文本） |
| `nextButton(text)` | 添加下一页按钮（自定义文本） |
| `chooseButton(text, placeholder)` | 添加页码跳转按钮与输入框占位符 |
| `button(label, callback, options)` | 添加普通按钮 |
| `closeButton()` | 添加关闭按钮 |
| `divider(options)` | 添加分割线 |
| `dropdown(label, observable, items, options)` | 添加下拉框 |
| `header(text, options)` | 添加标题文本 |
| `label(text, options)` | 添加标签文本 |
| `slider(label, observable, min, max, options)` | 添加滑块 |
| `spacer(options)` | 添加空白间距 |
| `textField(label, observable, options)` | 添加输入框 |
| `toggle(label, observable, options)` | 添加开关 |
| `show(callback)` | 显示表单，`callback` 接收一个 `PaginatedFormResult` 参数 |
| `close()` | 主动关闭表单 |
| `isShowing()` | 返回表单是否正在显示 |

> [!WARNING]
> `previousButton`、`nextButton`、`chooseButton` 每个表单只能添加一次；分页按钮需要在 `show` 之前添加。

`PaginatedFormResult` 字段：

| 字段 | 说明 |
| --- | --- |
| `closeReason` | 关闭原因（见上文取值表） |
| `selection` | 玩家点击的条目内容（未点击时为 `""`） |
| `selectionIndex` | 点击条目在所有条目中的索引（从 0 开始） |
| `page` | 点击条目所在页码 |

### PaginatedForm 示例

```lcui
pages = [ "Apple", "Banana", "Cherry", "Dragon Fruit", "Elderberry" ];

form = new PaginatedForm("fruits", "Fruit List", pages, 2);
form.previousButton("Previous");
form.nextButton("Next");
form.chooseButton("Go", "Page number");
form.closeButton();

form.show(func (result) -> void {
    if (result.closeReason == 2) [
        if (result.selection != "") [
            mc::runCmd("say Selected: " + result.selection);
        ]
    ]
});
```

## Observable 系列（可观察数据）

可观察数据用于表单控件的双向数据绑定。控件修改数据后，`getData()` 会返回最新值；也可以订阅变化。

### Observable 构造

```lcui
str = new ObservableString("default", true);
num = new ObservableNumber(0, false);
flag = new ObservableBoolean(false, true);
raw = new ObservableUIRawMessage(UIRawMessage.text("hello"), true);
```

构造参数为（初始值，客户端是否可写）。`clientWritable` 为 `true` 时，玩家可以在界面上修改该数据。

### Observable 方法

| 方法 | 说明 |
| --- | --- |
| `isClientWritable()` | 返回客户端是否可写 |
| `getData()` | 获取当前值 |
| `setData(value)` | 设置当前值（`ObservableNumber` 支持 `int` / `float`） |
| `subscribe(callback)` | 订阅变化，`callback` 接收一个参数（变化后的新值），返回订阅 ID |
| `unsubscribe(id)` | 通过订阅 ID 取消订阅 |

### Observable 运算符

`ObservableNumber` / `ObservableString` / `ObservableBoolean` 重载了常用运算符，操作数可以是同类型 Observable 或对应的原生值，且原生值在运算符**任意一侧**均可（`num + 3` 与 `3 + num` 等价），按当前值参与运算并返回普通值：

```lcui
num = new ObservableNumber(2, false);
num + 3;        // 5
3 + num;        // 5
num * 2 - 1;    // 3
num > 2;        // true
5 == num + 3;   // true

text = new ObservableString("ab", false);
text + "cd";    // "abcd"
"x" + text;     // "xab"
text == "ab";   // true

flag = new ObservableBoolean(true, false);
flag == false;  // false
```

| 类 | 支持的运算符 |
| --- | --- |
| `ObservableNumber` | `+ - * / % ^` 与 `== != > < >= <=` |
| `ObservableString` | `+`（拼接）与 `== != > < >= <=` |
| `ObservableBoolean` | `== !=` |

### Observable 示例

```lcui
text = new ObservableString("", true);
text.subscribe(func (value) -> void {
    mc::runCmd("say changed to: " + value);
});

text.setData("new value");
```

## UIRawMessage（原始消息）

`UIRawMessage` 用于构造富文本/翻译文本，可作为表单的文本参数（TextValue）传入。

| 静态方法 | 说明 |
| --- | --- |
| `UIRawMessage.text(string)` | 创建纯文本消息 |
| `UIRawMessage.translate(key)` | 创建翻译文本 |
| `UIRawMessage.translate(key, array)` | 创建带替换参数的翻译文本，`array` 为字符串数组 |
| `UIRawMessage.translate(key, uiRawMessage)` | 创建带嵌套消息的翻译文本 |
| `UIRawMessage.rawText(array)` | 将多个 `UIRawMessage` 组合为原始文本 |

```lcui
title = UIRawMessage.translate("chat.gui.title");
form = new CustomForm("raw", title);
```

## 选项类

选项类用于控制对应控件的显示与行为，通过 `new XxxOptions()` 创建后设置字段。

| 类 | 字段 |
| --- | --- |
| `TextOptions` | `visible` |
| `ButtonOptions` | `disabled`、`tooltip`、`visible` |
| `TextFieldOptions` | `description`、`disabled`、`visible` |
| `DropdownOptions` | `description`、`disabled`、`visible` |
| `DropdownItem` | `label`（必填）、`value`（必填）、`description` |
| `SliderOptions` | `description`、`disabled`、`step`、`visible` |
| `ToggleOptions` | `description`、`disabled`、`visible` |
| `DividerOptions` | `visible` |
| `SpacingOptions` | `visible` |

```lcui
item = new DropdownItem();
item.label = "Option 1";
item.value = 0;
item.description = "First option";

options = new DropdownOptions();
options.description = "Choose one";
options.disabled = false;
options.visible = true;
```

## 常见问题

- **`show` 回调参数数量错误**：`CustomForm`、`MessageBox`、`PaginatedForm` 的 `show` 回调必须恰好接收一个参数，否则会报 `show callback must take exactly one parameter`。
- **按钮回调带参数**：`button` 的回调不能接收参数，否则会报 `button callback must not take any arguments`。
- **重复添加分页按钮**：`previousButton`、`nextButton`、`chooseButton` 重复添加会报错（如 `previousButton has already been added`）。
- **表单关闭后无法再次显示**：表单关闭后会自动从 GUIManager 注销，需要重新执行 lcui 脚本创建新实例。
