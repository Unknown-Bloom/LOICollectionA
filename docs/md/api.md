# LOICollectionAPI

> [!NOTE]
> 以下内容取自 LOICollectionA 1.15.0 的 `LOICollectionAPI` 结构，对于后续版本的 `LOICollectionAPI` 结构可能会有所不同。

## 默认变量

| LOICollectionAPI | 备注 | 类型 |
| --- | --- | --- |
| {version_mc} | 当前服务器 Minecraft 版本 | string |
| {version_ll} | 当前服务器 LeviLamina 版本 | string |
| {version_protocol} | 当前服务器协议版本 | string |
| {player} | 当前玩家名称 | string |
| {player_language} | 获取玩家使用的语言ID | string |
| {player_language_name} | 获取玩家使用的语言名称 | string |
| {player_title} | 获取玩家当前佩戴称号 | string |
| {player_title_time} | 获取玩家剩余拥有称号时间 | string |
| {player_mute} | 玩家是否被禁言 | boolean |
| {player_pvp} | 玩家是否开启PVP | boolean |
| {player_statistcs_onlinetime} | 玩家在线时长 | string |
| {player_statistcs_kills} | 玩家击杀生物数量 | int |
| {player_statistcs_deaths} | 玩家死亡次数 | int |
| {player_statistcs_place} | 玩家放置方块数量 | int |
| {player_statistcs_destroy} | 玩家破坏方块数量 | int |
| {player_statistcs_respawn} | 玩家重生次数 | int |
| {player_statistcs_join} | 玩家加入服务器次数 | int |
| {player_gamemode} | 玩家当前游戏模式 | string |
| {player_pos} | 玩家所在坐标 | string |
| {player_pos_x} | 玩家所在X坐标 | int |
| {player_pos_y} | 玩家所在Y坐标 | int |
| {player_pos_z} | 玩家所在Z坐标 | int |
| {player_pos_respawn} | 玩家重生坐标 | string |
| {player_pos_respawn_x} | 玩家重生X坐标 | int / string(None) |
| {player_pos_respawn_y} | 玩家重生Y坐标 | int / string(None) |
| {player_pos_respawn_z} | 玩家重生Z坐标 | int / string(None) |
| {player_pos_block} | 玩家所在的方块坐标 | string |
| {player_pos_lastdeath} | 玩家上次死亡的坐标 | string |
| {player_realname} | 玩家的真实名字 | string |
| {player_xuid} | 玩家XUID字符串 | string |
| {player_uuid} | 玩家Uuid字符串 | string |
| {player_is_op} | 玩家是否为OP | boolean |
| {player_can_fly} | 玩家是否可以飞行 | boolean |
| {player_health} | 玩家当前生命值 | int |
| {player_max_health} | 玩家最大生命值 | int |
| {player_hunger} | 玩家当前饥饿值 | int / string(None) |
| {player_max_hunger} | 玩家最大饥饿值 | int / string(None) |
| {player_saturation} | 玩家当前饱和度 | int / string(None) |
| {player_max_saturation} | 玩家最大饱和度 | int / string(None) |
| {player_speed} | 玩家当前速度 | float |
| {player_direction} | 玩家当前朝向 | string |
| {player_dimension} | 玩家当前维度ID | int |
| {player_ip} | 玩家连接IP | string |
| {player_exp_xp} | 玩家当前经验 | int / string(None) |
| {player_exp_level} | 玩家当前等级 | int / string(None) |
| {player_exp_level_next} | 玩家下一等级所需经验 | int |
| {player_handitem} | 玩家手持物品名称 | string |
| {player_offhand} | 玩家副手持物品名称 | string |
| {player_os} | 玩家设备名称 | string |
| {player_ms} | 玩家的网络延迟时间 (ms) | int |
| {player_ms_avg} | 玩家的平均网络延迟时间 (ms) | int |
| {player_packet} | 玩家的网络丢包率 (%) | int |
| {player_packet_avg} | 玩家的平均网络丢包率 (%) | int |
| {server_tps} | 获取当前服务器TPS | float |
| {server_mspt} | 获取当前服务器MSPT | float |
| {server_time} | 当前时间 | string |
| {server_player_max} | 最大玩家数量 | int |
| {server_player_online} | 在线玩家数量 | int |
| {server_entity} | 当前服务器实体数量 | int |
| {score(ScoreboardName)} | 玩家的计分板分数 | int |
| {tr(languageId)} | 获取玩家当前语言指定ID的翻译文本 | string |
| {tr(langcode, languageId)} | 获取指定语言指定ID的翻译文本 | string |
| {entity(typeid)} | 获取指定ID的实体数量 | int |

> [!TIP]
> 因为解析器的原因，在传递任何形式的字符串时，都应使用 `"` 或 `'` 进行包裹。

---

## 阻断标识符 - $

> 阻断标识符用于阻止变量的替换，即原变量内容将保留原样，不会被解析为其他内容。

```text
${variable}
```

| 参数 | 备注 |
| --- | --- |
| variable | 变量名 |

> [!TIP]
> 阻断标识符通常用于变量前，且在完成替换后标识符会被移除。

### **阻断标识符具体使用实例**  

以下将展示玩家的真实名字，且不会解析其他内容

```text
{player_realname} + '->' + ${team}
```

- 返回结果：`player -> {team}`

---

## 判断语句 - if

> 判断语句用于判断条件是否成立，如果成立则将`result_yes` 的内容替换掉原语句，否则将以 `result_no` 的内容替换掉原语句  

```text
if (condition)[result_yes : result_no]
```

| 参数 | 备注 |
| --- | --- |
| condition | 判断条件 |
| result_yes | 条件成立时替换的内容 |
| result_no | 条件不成立时替换的内容 |

> [!TIP]  
> 在判断语句中是支持 `if` 嵌套的，但建议不要嵌套过多，否则会导致语句难以阅读  

### **判断语句具体使用实例**  

以下将以不同的颜色展示玩家的平均网络延迟时间

> [!WARNING]
> 需要注意的是语句的原 `result` 无法解析特殊字符，需要将其整合为字符串类型

```text
if({player_ms_avg} <= 50)["§a" : if( {player_ms_avg} > 50 && {player_ms_avg} <= 250)["§e" : "§c"]] + {player_ms_avg} + "§bms"
```

- **当判断条件满足 `{player_ms_avg} <= 50` 时，其会返回 "§a {player_ms_avg}§bms"**  
- **当判断条件满足 `{player_ms_avg} > 50 && {player_ms_avg} <= 250` 时，其会返回 "§e {player_ms_avg}§bms"**  
- **当判断条件不满足以上条件时，其会返回 "§c {player_ms_avg}§bms"**

---

## 运算符 - operator

> 运算符通常是用于原始语句中进行更加方便快捷的处理工具，包括但不限于 `+`、`-`、`*`、`/` 等。

| 运算符1 | 说明 | 运算符2 | 说明 |
| --- | --- | --- | --- |
| + | 加法 | - | 减法 |
| * | 乘法 | / | 除法 |
| % | 取模 | ^ | 幂运算 |
| ! | 逻辑非 | && | 逻辑与 |
| \|\| | 逻辑或 | == | 等于 |
| != | 不等于 | > | 大于 |
| < | 小于 | >= | 大于等于 |
| <= | 小于等于 | ... | ... |

### **运算符具体使用实例**  

以上内容中的大部分均为表面含义，可直接使用，故不再赘述。  
接下来将展示一些`特殊用法`。

1. 字符串拼接

    ```text
    "Hello" + "World"
    ```

    - 返回结果："HelloWorld"

2. 字符串比较

    ```text
    "Hello" <= "World"
    ```

    - 返回结果：false

    > [!NOTE]
    > 这里的比较规则大致可以参考 `C++` 的比较规则

---

## 函数 - function

> 函数是一些常用的功能的集合，可以帮助我们更加方便的进行一些操作。  
> 以下将提供一些内置的函数，更多函数可自行进行扩展。

```text
namespaces::function_name(parameter1, parameter2,...)
```

| 参数 | 备注 |
| --- | --- |
| namespaces | 命名空间，用于区分不同功能 |
| function_name | 函数名 |
| parameter1 | 参数1 |
| parameter2 | 参数2 |
| ... | ... |

> [!WARNING]
> 在函数调用中，参数最大数量只支持到 `100` 个，超出数量将会解析失败。详情见 [Too many args in function call](./errors.md#too_many_arguments_in_function_call) 错误

### **函数具体使用实例**

以下将展示计算 `cos(sqrt(100))` 和 `sin(sqrt(100))` 的最大值，并取其绝对值。

> [!TIP]
> 需要注意的是，函数的调用需要使用 `::` 进行分隔，且函数的参数需要使用 `,` 进行分隔。

```text
math::abs(math::max(math::cos(math::sqrt(100)), math::sin(math::sqrt(100))))
```

- 返回结果：0.544021

> [!WARNING]
> 每个表达式后都必须添加分隔符 `;`

---

## 脚本语言特性（.lcui）

从 1.15.0 起，Menu 与 Shop 的界面数据改为 `.lcui` 脚本文件（见 [数据文件](./data.md)），其余模块的内置界面也使用同一套脚本语言编写。脚本支持变量、数组、字符串、数值、布尔值与 `none`，并支持 `if`、`while`、`for`、`break`、`continue`、`return` 等流程控制。

### 函数定义与匿名函数

使用 `func` 定义命名函数，参数与返回值可以指定类型（未指定时为动态类型）：

```lcui
func add(a: int, b: int) -> int {
    return a + b;
}

result = add(1, 2);
```

匿名函数（Lambda）使用 `func (参数) -> 返回类型 { ... }` 定义，可以赋值给变量或直接作为参数传递：

```lcui
double = func (x: int) -> int {
    return x * 2;
};
value = double(3);

form.button("Button", func () -> void {
    mc::runCmd("say Hello");
}, new ButtonOptions());
```

> [!TIP]
> 你知道吗：天是蓝的......虽然有时候不是蓝(￣y▽￣)╭  

### 类与继承

脚本语言支持类、继承、访问控制与静态成员：

```lcui
class Animal {
public:
    name = "unknown";

    Animal(name) {
        this.name = name;
    }

    func speak() -> string {
        return "...";
    }
}

class Dog extends Animal {
    func speak() -> string {
        return "Woof";
    }
}

dog = new Dog("dog");
if (dog instanceof Animal) [
    mc::runCmd("say " + dog.speak());
]
```

类相关的关键字与运算符：

| 关键字/运算符 | 说明 |
| --- | --- |
| `class` | 定义类 |
| `extends` | 继承基类 |
| `new` | 创建对象 |
| `this` | 当前对象 |
| `super` | 调用基类方法，`super(...)` 调用基类构造器 |
| `instanceof` | 判断对象是否为指定类的实例 |
| `public:` / `private:` | 访问控制段 |
| `static` | 静态成员或静态方法 |
| `using Alias = Type;` | 类型别名 |

### 内置命名空间与函数

| 命名空间 | 函数 | 说明 |
| --- | --- | --- |
| `math` | `abs`、`max`、`min`、`sqrt`、`pow`、`log`、`sin`、`cos`、`random` | 数学函数 |
| `string` | `length`、`upper`、`lower`、`substr`、`trim`、`replace` | 字符串函数 |
| `std` | `format(formatStr, array)` | 使用数组中的值格式化字符串 |
| `mc` | `runCmd(command)` | 以当前玩家身份执行命令 |
| `GUIManager` | `value(id)` | 获取 GUI 注册值 |
| `GUIManager` | `request(id, args)` | 请求 GUI 注册数据 |
| `GUIManager` | `callback(id, args)` | 调用 GUI 注册回调 |
| `GUIManager` | `open(id, formId, type[, ctx])` | 打开指定类型的 GUI |
| `GUIManager` | `switchTo(id, type)` | 将当前 GUI 切换为指定类型 |

> [!TIP]
> `GUIManager::open` 与 `GUIManager::switchTo` 中的 `type` 为整数：`1` 为 CustomForm，`2` 为 MessageBox，`3` 为 PaginatedForm，`4` 为 ScriptForm。

> [!NOTE]
> `GUIManager::open` 跳转到其他脚本时，需要在 `gui/permission.json` 的 `scripts.<id>.gui.navigations` 中声明目标脚本 id，否则调用会被拒绝。打开自身表单无需声明。

### 常用内置类

| 类 | 说明 |
| --- | --- |
| `MenuData` | 菜单数据，包含 `id`、`type`、`title`、`content`、`permission`、`exitCommand`、`scoreCommand`、`permissionCommand`、`items`、`controls`、`confirm`、`cancel`、`run`、`submit` |
| `MenuItemData` | 菜单按钮数据，包含 `type`、`title`、`id`、`run`、`permission`、`scores` |
| `MenuForm` | 菜单表单，`new MenuForm(id, title)`；方法：`header`、`label`、`divider`、`spacer`、`textField`、`dropdown`、`toggle`、`slider`、`button`、`closeButton`、`show`；回调结果为 `closeReason`、`actionIndex`、`action` |
| `MenuMessageBox` | 菜单对话框，`new MenuMessageBox(id, title)`；方法：`body`、`button1`、`button2`、`show`；回调结果为 `closeReason`、`selection`、`action` |
| `ShopData` | 商店数据，包含 `id`、`type`、`title`、`content`、`exitCommand`、`scoreCommand`、`titleCommand`、`itemCommand`、`items` |
| `ShopItemData` | 商店商品数据，包含 `type`、`title`、`introduce`、`number`、`id`、`nbt`、`confirmButton`、`cancelButton`、`time`、`scores` |
| `ShopForm` | 商店表单，`new ShopForm(id, data)`；方法：`label`、`divider`、`previousButton`、`nextButton`、`chooseButton`、`closeButton`、`show`；回调结果为 `closeReason`、`resultCode`、`shop`、`itemIndex`、`item`、`fromId` |
| `ScoreRequirement` | Score 需求，包含 `objective`、`value` |
| `PaginatedForm` | 分页表单，`new PaginatedForm(guiId, title, elements[, pageSize])`；方法：`previous`、`next`、`choose`、`previousButton`、`nextButton`、`chooseButton`、`button`、`closeButton`、`divider`、`dropdown`、`header`、`label`、`slider`、`spacer`、`textField`、`toggle`、`show`、`close`、`isShowing`；回调结果为 `closeReason`、`selection`、`selectionIndex`、`page` |
| `ObservableString` / `ObservableNumber` / `ObservableBoolean` | 可观察数据，构造参数为（初始值，客户端是否可写）；方法：`getData`、`setData`、`subscribe`、`unsubscribe`、`isClientWritable` |
| `TextOptions` / `ButtonOptions` / `TextFieldOptions` / `DropdownOptions` / `DropdownItem` / `SliderOptions` / `ToggleOptions` / `DividerOptions` / `SpacingOptions` | 表单控件选项类 |
| `GlobalValue` | 全局值容器，包含 `value` 字段 |

---

> 脚本语言的常见错误及解决方案，请参见 [常见错误含义](./errors.md)。
> 原生 UI 的完整类与方法说明，请参见 [原生 UI（Native UI）](./native-ui.md)。
