# LCUI 脚本语法

> [!NOTE]
> 以下内容取自 LOICollectionA 1.15.0 的 `.lcui` 脚本语言实现，对于后续版本可能会有所不同。

## 概述

`.lcui` 是 LOICollectionA 用于编写游戏内 GUI 的脚本语言。所有模块的内置界面（如 `blacklist.lcui`、`statistics.lcui`）以及 Menu/Shop 的 `menu.lcui`、`shop.lcui` 均使用该语言编写。

脚本的完整处理流程为：

```text
ScriptLoader（导入解析与合并） -> Lexer（词法分析） -> Parser（语法分析） -> SemanticAnalyzer（语义分析） -> Compiler（字节码编译） -> Optimizer（优化） -> VM（执行）
```

- 插件启动时或执行 `/xxx reload` 命令后，`GUIManager::load` 会读取 `.lcui` 文件，先解析 `import` 导入图（见「多文件导入」），再完成上述编译流程；编译产物除缓存在内存外，还会以 `.lcc` 字节码文件落盘（见「字节码磁盘缓存」）。
- 玩家打开 GUI 时，`GUIManager::open(id, formId, type[, ctx])` 会针对该玩家执行缓存的脚本；脚本中 `new CustomForm(...)`、`new PaginatedForm(...)` 等创建的表单会注册到 `GUIManager`，随后切换到 `formId` 对应的表单。

> 原生表单类的方法与生命周期详见 [原生 UI（Native UI）](./native-ui.md)，Menu/Shop 专用表单与默认变量详见 [LOICollectionAPI](./api.md)。

## 一个最小的脚本

```lcui
form = new CustomForm("example.main", {tr("example.gui.title")}) {
    label({tr("example.gui.hello")}, new TextOptions());
    button({tr("generic.gui.close")}, on: func () -> void {
        form.close();
    }, new ButtonOptions());
    closeButton();
    show();
};
```

## 声明式 UI 块

创建表单时，可以在构造调用后紧跟一个 `{ ... }` 块，按界面出现顺序声明控件。块内省略接收者的方法调用会自动作用于正在构建的表单。上面的最小脚本完全等价于手工展开的命令式写法：

```lcui
form = new CustomForm("example.main", {tr("example.gui.title")});
form.label({tr("example.gui.hello")}, new TextOptions());
form.button({tr("generic.gui.close")}, on: func () -> void {
    form.close();
}, new ButtonOptions());
form.closeButton();
form.show();
```

要点：

- **接收者**：`变量 = new 表单类(参数) { ... }` 中，块内的裸调用（如 `label(...)`）会脱糖为 `变量.label(...)`。接收者变量在块体执行前就已绑定，因此块内定义的 lambda（例如按钮回调）可以直接用名字引用正在构建的表单，如上例中的 `form.close()`。
- **`on:` 命名参数**：在块内（且仅在块内）可以为 `button` 等带回调的方法使用 `on: 回调` 传参，参数位置与显式写法一致。
- **控制流**：块内允许 `if` 等控制流语句，按实际执行顺序决定哪些控件被加入。例如内置的 `market_store.lcui` 中，审核按钮仅在管理员且开启审核时才会加入：

```lcui
mineForm = new CustomForm("market.store.mine", {tr("market.gui.title")}) {
    /* ... 其他按钮 ... */
    if (GUIManager::request("market.isAdmin", [])[0] && GUIManager::request("market.store.review.enabled", [])[0]) [
        button({tr("market.gui.store.mine.audit")}, on: func () -> void {
            navigateAudit.value = true;

            mineForm.close();
        }, auditOption);
    ]

    closeButton();
};
```

- **仅限表单类**：声明式块只能用于 `CustomForm`、`MessageBox`、`PaginatedForm`、`ScriptForm`；对其他类使用（如 `new GlobalValue() { ... }`）会报编译错误。
- **普通函数回退**：块内不属于表单方法的裸调用会按普通函数解析，因此导入的辅助函数（如 `pagePrevious()`、`saveLabel()`）可以直接在块内调用。

## 组件（component）

`component` 把重复的表单控件组合抽象成可复用的片段。组件在使用点按编译期宏展开（卫生展开），参数按名绑定，运行期零开销；组件体内的裸方法调用继承使用点的当前表单隐式接收者：

```lcui
component ConfirmBar(confirmText, onConfirm) {
    button(confirmText, on: onConfirm, new ButtonOptions());
    closeButton();
}

form = new CustomForm("wallet.main", {tr("wallet.gui.title")}) {
    label({tr("wallet.gui.label")}, new TextOptions());
    ConfirmBar(saveLabel(), func () -> void {
        form.close();
    });
    show();
};
```

上面的写法在编译期等价于直接在块内写：

```lcui
form = new CustomForm("wallet.main", {tr("wallet.gui.title")}) {
    label({tr("wallet.gui.label")}, new TextOptions());
    button(saveLabel(), on: func () -> void {
        form.close();
    }, new ButtonOptions());
    closeButton();
    show();
};
```

规则：

| 规则 | 说明 |
| --- | --- |
| 定义位置 | 组件只能定义在脚本顶层；嵌套在函数体或块内定义会报编译错误 |
| 使用位置 | 组件调用只能作为声明式 UI 块内的语句；在块外调用或作为表达式使用会报编译错误 |
| 参数绑定 | 调用参数个数必须与组件参数一致，按位置绑定到参数名 |
| 卫生展开 | 组件体内声明的局部变量会被自动重命名，不会捕获或污染使用点的外层同名变量；多个使用点的展开相互独立 |
| 禁止递归 | 组件（直接或间接）调用自身会报编译错误 |
| 块内能力 | 组件体内允许控制流与局部变量（块作用域），含义与手写在声明式块内完全一致 |
| 接收者继承 | 展开发生在使用点，因此组件体内的裸调用作用于使用点的表单接收者（上例中的 `form`） |
| 跨文件复用 | 组件定义属于顶层定义，可以放在被 `import` 的文件中（如 `generic.lcui`）供多个脚本共用 |

内置脚本的公共控件组合就定义在 `generic.lcui` 中：`PageControls()`（上一页/下一页/跳转三连按钮）、`SectionHeader()`（间隔 + 分隔线）、`ConfirmBar(confirmText, onConfirm)`（确认按钮 + 关闭按钮）。

## 多文件导入（import）

脚本顶部可以使用 `import` 引入其他 `.lcui` 文件的顶层定义（`class` / `func` / `using` / `component`），实现跨文件复用。内置脚本的公共界面词汇表（分页按钮、保存/取消文案等）就抽取在 `generic.lcui` 中：

```lcui
import "generic.lcui";

quote = new PaginatedForm("market.quote", {tr("market.gui.title")}, GUIManager::value("market.quote.items"), 10) {
    label({tr("market.gui.quote.list.label")}, new TextOptions());
    PageControls();
    closeButton();
    show();
};
```

规则：

| 规则 | 说明 |
| --- | --- |
| 路径解析 | `import` 路径相对于入口脚本所在目录解析 |
| 合并顺序 | 导入图按依赖优先（拓扑序）展开，每个文件的定义只合并一次；菱形导入（A、B 都导入 C）合法，C 只出现一次 |
| 定义冲突 | 不同文件中同名 `class` / `func` / `using` / `component` 会报错，错误信息包含两处定义的文件与行号 |
| 仅限定义 | 被导入的文件只能包含 `class` / `func` / `using` / `component` / `import`，不能包含可执行语句（避免导入产生副作用）；入口文件不受此限制 |
| 循环导入 | 循环导入（如 A → B → A）会报错，并给出完整环路径 |
| 文件缺失 | 导入的文件不存在或不可读时报错 |

## 注释、字面量与语句分隔

### 注释

支持行注释 `//` 与块注释 `/* ... */`：

```lcui
// 这是行注释
/* 这是
   块注释 */
```

### 字面量

| 字面量 | 示例 | 说明 |
| --- | --- | --- |
| 整数 | `42`、`-1` | 32 位有符号整数 |
| 浮点数 | `3.14`、`.5`、`5.` | 支持省略小数点前的 `0` |
| 字符串 | `"hello"`、`'world'` | 单引号或双引号包裹 |
| 布尔 | `true`、`false` | — |
| 空值 | `None` | 仅可用于 `optional` 上下文 |
| 数组 | `[1, "a", true]` | 可混合元素类型 |

字符串支持转义序列：`\n`（换行）、`\t`（制表符）、`\r`（回车）、`\\`（反斜杠）、`\"`（双引号）、`\'`（单引号）。未知转义（如 `\x`）保持字面量并产生编译期警告。若字符串中需要包含某种引号，也可以使用另一种引号包裹，例如 `'say "hi"'`；字符串也允许跨行。

### 语句分隔

语句之间可以使用 `;` 分隔，也可以使用换行分隔。同一行内存在多条语句时必须使用 `;`，推荐每条语句都以 `;` 结尾：

```lcui
a = 1; b = 2;   // 同一行需要 ;

c = 3           // 换行可以省略 ;
d = 4;
```

## 变量与类型

### 动态变量

直接赋值即可创建变量，未标注类型时为动态类型，可随时赋其他类型的值：

```lcui
a = 1;
a = "test";     // 合法，动态类型
b = [1, 2, 3];
```

### 类型标注

使用 `变量名: 类型 = 值` 声明带类型的变量，类型声明必须同时提供初始值：

```lcui
count: int = 1;
name: string = "LOICollection";
flag: bool = true;
score: float = 1.5;
```

类型标注会进行赋值检查，类型不匹配或声明后赋其他类型的值都会报错。

### 内置类型

| 类型 | 说明 |
| --- | --- |
| `int` | 整数 |
| `float` | 浮点数 |
| `string` | 字符串 |
| `bool` | 布尔值 |
| `void` | 函数无返回值时使用 |
| `ClassName` | 自定义类或原生类类型 |
| `optional<T>` | 可选值，可能为空 |
| `variant<T1, T2, ...>` | 联合值，只能保存声明中的某一种类型 |

未标注类型的函数参数、函数返回值与变量均为动态类型。`None` 是空值字面量而不是可写的类型名（`optional` 为空时，其 `.type` 返回 `none`）。

### optional（可选值）

`optional<T>` 可以保存 `T` 类型的一个值，也可以是空值 `None`：

```lcui
value: optional<int> = 1;
empty: optional<string> = None;

value.has_value;    // true
value.value;        // 1
empty.has_value;    // false
empty.value;        // 报错：optional 为空
```

`optional` 支持三个特殊成员：

| 成员 | 说明 |
| --- | --- |
| `has_value` | 是否有值（布尔） |
| `value` | 当前值，为空时访问报错 |
| `type` | 当前值的类型名；为空时为 `none` |

`None` 只能出现在 `optional` 上下文中（例如 optional 变量的初始值、optional 参数、optional 返回值），在其他位置使用会报错。空 `optional` 直接参与算术、比较等运算也会报错，建议先用 `if (value)` 或 `has_value` 判断，或使用空值安全操作符 `??` / `?.` 提供默认值（见「空值安全操作符」）。

### variant（联合值）

`variant<T1, T2, ...>` 只能保存声明列表中的某一种类型，赋值时会校验：

```lcui
data: variant<string, bool, int> = 1;
data.type;      // "int"
data.value;     // 1
data = "test";  // 合法
data = 1.5;     // 报错：float 不在声明列表中
```

`variant` 至少需要两个类型参数（如 `variant<int>` 不合法）。

### 类型别名 using

使用 `using` 可以为类型定义别名，仅允许出现在脚本顶层：

```lcui
using Value = variant<string, bool, int>;
using Count = int;

a: Value = 1;
b: Count = 3;
```

别名可以链式引用（`using B = A;`），但不能自引用，也不能重复定义。

### 数组

数组使用 `[元素1, 元素2, ...]` 创建，支持任意混合类型：

```lcui
items = [1, "a", true];
items[0];       // 1
items.length;   // 3

items.push(2);  // 追加元素，返回新长度
items[1] = "b"; // 修改已有元素（索引等于当前长度时同样会追加）
```

数组索引从 `0` 开始，越界读取、负数索引会报错。数组的 `==`/`!=` 比较的是引用（是否为同一个数组），而非内容。类的数组字段在每个实例中相互隔离（创建实例时会拷贝），修改一个实例的数组不会影响其他实例。

### 数组方法

数组自带以下方法（无需引入，直接以 `数组.方法(...)` 调用）：

| 方法 | 签名 | 说明 |
| --- | --- | --- |
| `push` | `arr.push(v)` | 追加元素，返回新长度 |
| `pop` | `arr.pop()` | 弹出并返回末元素；空数组报错 |
| `contains` | `arr.contains(v)` | 是否包含相等的元素（`==` 语义） |
| `indexOf` | `arr.indexOf(v)` | 首次出现的索引；找不到返回 `-1` |
| `join` | `arr.join(sep)` | 元素全部转字符串后用 `sep` 拼接 |
| `slice` | `arr.slice(start, end)` | 半开区间切片，返回新数组；越界自动裁剪 |
| `sort` | `arr.sort([cmp])` | 原地排序；`cmp` 为 `func(a, b) -> int`，省略时要求元素两两可比较 |

```lcui
items = [3, 1, 2];
items.push(4);        // items = [3, 1, 2, 4]，返回 4
items.sort();         // items = [1, 2, 3, 4]
names = items.join(",");   // "1,2,3,4"

words = ["b", "a"];
words.sort(func (a, b) -> int {
    return if (a < b) [ -1 : 1 ];
});
```

`sort` 无参数版本遇到不可比较的元素对（例如数字与字符串混合）会运行时报错；`slice` 的索引为负数时报错。

### 字符串方法

字符串同样以 `字符串.方法(...)` 调用：

| 方法 | 签名 | 说明 |
| --- | --- | --- |
| `split` | `s.split(sep)` | 按分隔符拆分为数组；`sep` 为空串时按字符拆分 |
| `contains` | `s.contains(sub)` | 是否包含子串 |
| `startsWith` / `endsWith` | `s.startsWith(sub)` | 前缀 / 后缀判断 |
| `indexOf` | `s.indexOf(sub)` | 首次出现位置；找不到返回 `-1` |
| `toInt` / `toFloat` | `s.toInt()` | 解析为 `optional<int>` / `optional<float>`，失败时为空 |

```lcui
"a,b,c".split(",");         // ["a", "b", "c"]
"hello".startsWith("he");   // true

value: optional<int> = "42".toInt();
value.has_value;            // true
value.value;                // 42

bad: optional<int> = "abc".toInt();
bad.has_value;              // false
```

### Map

`Map` 是保持插入序的键值容器，键支持 `int` / `float` / `string` / `bool`（值相等即同键）：

```lcui
m = new Map();
m.set("apple", 3);
m.set("banana", 5);
m.get("apple");         // 3
m.has("banana");        // true
m.remove("apple");
m.length;               // 1
m.keys;                 // ["banana"]，插入序

for (k in m.keys) [
    println(k + " = " + m.get(k));
]
```

`get` 在键不存在时返回 `None`，适合搭配空值安全操作符（见下文）。

## 运算符

### 算术运算符

| 运算符 | 说明 |
| --- | --- |
| `+` | 加法 / 字符串拼接 |
| `-` | 减法 |
| `*` | 乘法 |
| `/` | 除法（两个整数相除也会产生浮点数） |
| `%` | 取模（仅支持整数） |
| `^` | 幂运算（右结合） |
| `+`/`-`（一元） | 正号 / 负号 |

```lcui
1 + 2 * 3;   // 7
2 ^ 3 ^ 2;   // 512（右结合）
10 / 3;      // 3.33333...
10 % 3;      // 1
"Hello " + "World";  // "Hello World"
```

字符串与数值、布尔等类型使用 `+` 时都会转换为字符串拼接。`%` 只能用于整数，除数为零、整数溢出、取模非整数类型都会报错。

### 复合赋值与自增自减

`=` 可与算术运算符组合为复合赋值，左侧只求值一次（对 `arr[i] += 1`、`obj.field += 1` 尤为重要）：

```lcui
a = 10;
a += 5;    // 15
a -= 3;    // 12
a *= 2;    // 24
a /= 4;    // 6
a %= 4;    // 2

arr = [1, 2, 3];
arr[1] += 10;   // arr = [1, 12, 3]
```

`++` / `--` 为自增 / 自减，前后缀语义一致，等价于 `+= 1` / `-= 1`：

```lcui
i = 0;
i++;      // i = 1
++i;      // i = 2
i--;      // i = 1
```

> [!WARNING]
> `++` / `--` 作为表达式参与运算时返回的是**更新后的新值**（前缀语义），与 C/C++ 的后缀 `i++` 返回旧值不同：`i = 5; j = i++;` 得到 `j == 6` 而不是 `5`。如需旧值，请先保存再自增。

> 历史版本中 `++i` 会被解析为双重一元正号（值不变），引入前缀自增后语义改变；实际脚本中几乎不会出现这种写法，但属于已知的破坏性变更。

### 空值安全操作符

`??`（空值合并）在左侧为 `None` 或空 `optional` 时取右侧的值，否则取左侧；`0`、`""`、`false` **不会**触发合并（与真值规则无关）：

```lcui
name = value ?? "默认";
count = 0 ?? 10;    // 0：0 不是 None，不触发合并
```

`?.`（安全访问）在左侧为 `None` 或空 `optional` 时使整个访问链短路，结果为 `None`，支持字段与索引两种形式：

```lcui
len = maybeArr?.length ?? 0;
item = obj?.data?.[0];
```

`??` 的优先级高于赋值、低于比较运算，且不能与 `&&` / `||` 无括号混用（直接报错，避免歧义）。`x?.y` 的结果可能为 `None`，赋给 `T` 类型标注变量会报错，需搭配 `??` 或使用 `optional<T>`。

### 比较运算符

`==`、`!=`、`>`、`<`、`>=`、`<=` 可用于数值、字符串与布尔比较；数值的 `int`/`float` 可以直接比较。字符串比较规则大致参考 C++。类型不一致的 `==`/`!=` 会报错（例如 `1 == "1"`）。

对象、数组与函数支持 `==`/`!=`，但比较的是引用：

```lcui
a = [1];
b = [1];
a == b;   // false，内容相同但不是同一个数组
c = a;
a == c;   // true
```

`None == None` 为 `true`。

### 逻辑运算符

`&&`、`||` 支持短路求值：`false && 任意表达式` 不会执行右侧，`true || 任意表达式` 同样不会执行右侧。`!` 为逻辑非。

### 真值规则

条件判断使用以下真值规则：

| 值 | 真值 |
| --- | --- |
| `int` | 非 `0` 为真 |
| `float` | 绝对值大于 `0` 为真 |
| `string` | `""`、`"false"`、`"FALSE"`（不区分大小写）为假，其余非空字符串为真 |
| `bool` | 本身 |
| `array` | 非空为真 |
| 对象 / 函数 | 恒为真 |
| `None` | 恒为假 |

## 流程控制

### if / else

`if` 使用方括号 `[ ... ]` 作为分支块，`:` 分隔 else 分支，else 可以省略：

```lcui
if (count > 0) [
    mc::runCmd("say positive");
:
    mc::runCmd("say not positive");
]

if (flag) [
    mc::runCmd("say flag is true");
]
```

`if` 本身也是表达式，分支中的最后一个值会作为结果返回，可以参与运算或赋值：

```lcui
result = if (count > 0) [ 10 : 20 ];
total = if (flag) [ 1 : 2 ] + 5;
```

### while

```lcui
i = 0;
while (i < 5) [
    i++;
]
```

### for

```lcui
s = 0;
for (i = 0; i < 10; i++) [
    if (i % 2 == 0) [ continue; ];
    s += i;
]
```

`for (;;)` 可以省略三个子句，配合 `break` 使用。

### for-in 遍历

`for-in` 用于遍历数组，可同时获得索引与元素：

```lcui
items = ["a", "b", "c"];
for (item in items) [
    println(item);
]

for (i, item in items) [
    println(i + ": " + item);
]

for (k in new Map().keys) [
    println(k);
]
```

- `for (item in arr)` 遍历元素；`for (i, item in arr)` 中 `i` 为索引、`item` 为元素。
- 只支持数组（`Map` 可通过 `m.keys` 遍历键）。
- 循环体中 `continue` 不会跳过步进；每轮会重新读取 `length`，遍历期间追加的元素也会被遍历到。
- 元素变量在每轮迭代中是独立副本：迭代内创建的 lambda 捕获的是当轮的值。
- `in` 仅在 `for (` 之后识别为关键字，其他位置仍可用作普通标识符。

### 区间

`a..b` 生成整数区间（仅 `int`），配合 for-in 使用。区间为半开 `[a, b)`，端点在循环开始前求值一次：

```lcui
for (i in 0..5) [
    println(i);      // 依次输出 0、1、2、3、4
]

for (i in 5..0) [    // 降序：5、4、3、2、1
    println(i);
]
```

`5..5` 为空区间（不执行循环体）。

### break / continue

`break` 跳出当前循环，`continue` 跳过本次循环剩余部分；两者只能出现在循环内部，嵌套循环只作用于最近的一层。

## 函数

### 命名函数

使用 `func` 定义函数，参数与返回值可以标注类型，也可以省略（省略时为动态类型）：

```lcui
func add(a: int, b: int) -> int {
    return a + b;
}

func pick(x) {
    return x;
}

result = add(1, 2);
```

函数支持递归与相互递归调用，也支持按参数类型重载：

```lcui
func id(x: int) -> int {
    return x;
}

func id(x: string) -> string {
    return x;
}

id(7);      // 调用 int 版本
id("s");    // 调用 string 版本
```

函数和类定义只能出现在脚本顶层，不能嵌套在函数或其他块中。`return` 只能出现在函数体内；没有显式 `return` 的函数返回空值。

### 匿名函数（Lambda）

使用 `func (参数) -> 返回类型 { ... }` 定义匿名函数，可以赋值给变量、作为参数传递，并捕获外部局部变量、参数与 `this`：

```lcui
double = func (x: int) -> int {
    return x * 2;
};

form.button("Button", func () -> void {
    GUIManager::callback("example.submit", [ double(21) ]);
}, new ButtonOptions());
```

## 类与继承

### 定义类

```lcui
class Animal {
public:
    name = "unknown";
    count: int = 0;

    Animal(name) {
        this.name = name;
    }

    func speak() -> string {
        return "...";
    }
}

dog = new Animal("dog");
dog.name;   // "dog"
```

### 字段与方法

- 字段可以带默认值、类型标注，也可以两者都省略（动态类型）。
- 没有默认值的字段必须在构造函数中赋值。
- 构造函数的名称必须与类名一致，每个类最多一个构造函数；未定义构造函数时使用默认构造。
- 方法使用 `func 方法名(参数) -> 返回类型 { ... }` 定义，支持重载。

### 访问控制

使用 `public:` 与 `private:` 声明访问段，默认（首个访问段之前）为公开：

```lcui
class SecretBox {
private:
    secret = 42;
public:
    func getSecret() -> int {
        return secret;
    }
}

box = new SecretBox();
box.getSecret();    // 42
box.secret;         // 报错：私有字段
```

私有成员不会被继承，子类中无法通过 `this` 或裸名称访问基类的私有成员。

### 静态成员

使用 `static` 声明静态字段或静态方法，通过 `类名.成员` 访问：

```lcui
class Counter {
public:
    static total = 0;

    static func inc() -> int {
        total += 1;
        return total;
    }
}

Counter.inc();  // 1
Counter.total;  // 1
```

静态字段在所有实例之间共享；静态方法中不能使用 `this`，实例对象也不能直接调用静态方法。静态成员支持继承与重写。

### 继承

使用 `extends` 继承基类：

```lcui
class Dog extends Animal {
    func speak() -> string {
        return "Woof";
    }
}

dog = new Dog("dog");
dog instanceof Animal;  // true
```

相关关键字：

| 关键字 | 说明 |
| --- | --- |
| `extends` | 声明基类 |
| `super(...)` | 调用基类构造函数 |
| `super.方法(...)` | 调用基类方法 |
| `instanceof` | 判断对象是否为指定类（含派生类）的实例 |
| `this` | 当前对象 |

构造规则：

- 子类构造函数若不显式调用 `super(...)`，会隐式调用基类的无参构造（或默认构造）。
- 基类只有带参构造函数时，子类构造函数必须显式 `super(...)`，否则报错。
- 类名先声明后声明均可，支持多层继承与多态；循环继承、重复类名、未知基类会报错。

对象的 `==`/`!=` 同样为引用比较。

## 内置命名空间与宏

### 内置命名空间

| 命名空间 | 函数 | 说明 |
| --- | --- | --- |
| `math` | `abs`、`min`、`max`、`sqrt`、`pow`、`log`、`sin`、`cos`、`random` | 数学函数 |
| `string` | `length`、`upper`、`lower`、`substr`、`trim`、`replace` | 字符串函数 |
| `std` | `format(formatStr, array)` | 使用数组中的值格式化字符串（fmt 风格） |
| `mc` | `runCmd(command)` | 以当前玩家身份执行命令 |
| `GUIManager` | `value(id)`、`request(id, args)`、`callback(id, args)`、`open(id, formId, type[, ctx])`、`switchTo(id, type)` | 与 GUI 数据交互 |

调用格式为 `命名空间::函数(参数)`，例如：

```lcui
mc::runCmd("say hello");
GUIManager::callback("example.submit", [ "data" ]);
text = std::format({tr("example.gui.info")}, [ 1, "two" ]);
```

> `GUIManager::open` 与 `GUIManager::switchTo` 中的 `type` 为整数：`1` 为 CustomForm，`2` 为 MessageBox，`3` 为 PaginatedForm，`4` 为 ScriptForm。

### 宏 `{...}`

使用 `{名称}` 或 `{名称(参数)}` 调用宏，例如 `{tr("language.gui.title")}`。`tr` 返回当前玩家语言的翻译文本；`{player}`、`{server_tps}`、`{score(名称)}` 等默认变量也以宏形式使用，完整列表见 [LOICollectionAPI](./api.md)。

```lcui
title = {tr("example.gui.title")};
label = {player} + " -> " + {tr("example.gui.hello")};
```

### 透传与阻断 `$`

`$` 用于透传原始文本：`$` 之后到 `}`（或 `;`）之间的内容保持原样，不会参与解析：

```lcui
raw = $custom content};
```

`${变量}` 形式可以阻断变量/宏替换，使内容保留字面形式，例如 `{player_realname} + '->' + ${team}` 中的 `${team}` 会原样输出为 `{team}`。

## 原生类

脚本中可直接 `new` 以下原生类（概览）：

| 类 | 用途 | 详细文档 |
| --- | --- | --- |
| `GlobalValue` | 全局值容器，字段 `value` | [api.md](./api.md) |
| `CtxValue` | 读取 `GUIManager::open` 传入的 `ctx` 数组，字段 `value` | 见下文 |
| `Map` | 保持插入序的键值容器 | 见上文「Map」 |
| `ObservableString` / `ObservableNumber` / `ObservableBoolean` / `ObservableUIRawMessage` | 可观察数据，用于控件双向绑定 | [native-ui.md](./native-ui.md) |
| `UIRawMessage` | 构造富文本 / 翻译文本 | [native-ui.md](./native-ui.md) |
| `CustomForm` / `MessageBox` / `PaginatedForm` | 原生表单 | [native-ui.md](./native-ui.md) |
| `TextOptions` / `ButtonOptions` / `TextFieldOptions` / `DropdownOptions` / `DropdownItem` / `SliderOptions` / `ToggleOptions` / `DividerOptions` / `SpacingOptions` | 控件选项 | [native-ui.md](./native-ui.md) |
| `MenuData` / `MenuItemData` / `MenuControlData` / `MenuForm` / `MenuMessageBox` | 菜单数据与表单 | [api.md](./api.md) |
| `ShopData` / `ShopItemData` / `ShopForm` | 商店数据与表单 | [api.md](./api.md) |
| `ScoreRequirement` | Score 需求，字段 `objective`、`value` | [api.md](./api.md) |

### CtxValue

当脚本通过 `GUIManager::open(id, formId, type, ctx)` 打开并传入 `ctx` 数组时，可以使用 `new CtxValue(索引)` 读取对应元素：

```lcui
detail = new CtxValue(0);
form.label(detail.value, new TextOptions());
```

索引越界或未传入 `ctx` 时使用 `CtxValue` 会报错。

## 完整示例

综合使用变量、类型、函数、类、流程控制与原生表单：

```lcui
class Item {
public:
    name = "";
    price: int = 0;

    Item(name, price) {
        this.name = name;
        this.price = price;
    }

    func format() -> string {
        return name + " (" + price + ")";
    }
}

func makeItems() {
    return [ new Item("Apple", 3), new Item("Banana", 5) ];
}

items = makeItems();
selected = new GlobalValue();
selected.value = "";

form = new CustomForm("example.shop", {tr("example.shop.title")}) {
    label({tr("example.shop.list")}, new TextOptions());
    button(items[0].format(), on: func () -> void {
        selected.value = items[0].name;

        form.close();
    }, new ButtonOptions());
    button(items[1].format(), on: func () -> void {
        selected.value = items[1].name;

        form.close();
    }, new ButtonOptions());
    closeButton();
    show();
};
```

## 执行预算与调用深度

为防止失控脚本卡死服务器，VM 对每次脚本执行设置了两道硬限制：

| 限制 | 上限 | 触发后的行为 |
| --- | --- | --- |
| 指令数 | 1,000,000 条 | 终止执行并报错 `Execution budget exhausted` |
| 调用深度 | 256 层调用帧 | 终止执行并报错 `Call stack depth limit exceeded` |

普通界面脚本远达不到这两个上限；忘写步进条件的死循环（如 `while (true)`）或无终止条件的递归会被及时中止，而不是拖垮服务器。

## 字节码优化

编译完成后、执行之前，字节码会经过一轮优化器（Optimizer）处理。优化只改变「怎么算」，不改变「算什么」——程序该报的错一个不会少，报错时的源码位置也保持不变。对脚本作者来说这一切是透明的，了解它有助于写出天然受益的脚本：

| 优化 | 例子 | 效果 |
| --- | --- | --- |
| 常量折叠 | `1 + 2 * 3` | 编译期算成 `PUSH_INT 7`，运行期不再计算 |
| 变量值转发 | `x = 1; if (x == 1) [...]` | 后续读取 `x` 直接替换为已知值，条件分支随之消除 |
| 纯函数折叠 | `math::abs(-3)`、`math::pow(2, 10)` | 全常量参数的 `math::` 纯函数在编译期求值；`math::random` 等非纯函数不折叠 |
| 超级指令 | `x = 1`（`DUP` + `STORE_VAR`） | 高频指令对融合为单条 `DUP_STORE` / `DUP_IS_NONE`，一次分发完成两件事 |
| 跳转窥孔 | 条件跳转后紧跟同目标无条件跳转 | 双重跳转合并，中间不可达代码整体删除 |
| 死代码删除 | `while (false) [...]`、`if (false) [...]` | 不可达分支整体移除 |

优化器会反复迭代以上过程直到字节码不再变化（不动点），因此一条优化暴露出的新机会（如变量转发让条件变为常量）会在下一轮被继续吃掉。

运行期还有一层**内联缓存**：脚本反复调用同一个原生函数或原生方法时（例如循环里的 `math::abs(...)`、`arr.push(...)`），VM 会记住上一次的解析结果，命中时跳过签名匹配与注册表查找。这只影响速度不影响行为——缓存失效（如插件重载导致注册表变化）时自动回退到完整查找。

## 字节码磁盘缓存

`GUIManager::load` 编译脚本后，会把字节码序列化到源文件旁边的 `<脚本名>.lcc` 文件（例如 `market.lcui` 对应 `market.lcui.lcc`）：

- 再次加载时优先读取 `.lcc` 缓存，命中则跳过词法/语法/语义分析与编译，直接复用字节码；
- 缓存头记录源文件及整个导入图中每个文件的 SHA-256；源文件或任一导入文件内容变化、缓存格式版本不同都会使缓存整体失效并重新编译；
- 序列化数据附带校验和，损坏的缓存会被丢弃并重新编译；
- 缓存仅是加速手段：删除 `.lcc` 文件不影响正确性，只是下次加载需要重新编译。

当前缓存格式版本为 v2（v2 起字节码支持 `DUP_STORE` / `DUP_IS_NONE` 超级指令）。插件升级引入新格式版本后，旧 `.lcc` 缓存会因版本不一致自动失效并重新编译，无需手动清理。

## 常见约束与陷阱

- 类、命名函数、`using` 与组件只能出现在脚本顶层，不能嵌套定义。
- 声明式 UI 块只能用于表单类（`CustomForm` / `MessageBox` / `PaginatedForm` / `ScriptForm`）；`on:` 命名参数只在块内可用。
- 组件调用只能作为声明式 UI 块内的语句；组件递归（直接或间接）会报编译错误。
- 被导入的文件只能包含顶层定义（`class` / `func` / `using` / `component` / `import`）；不同文件中的同名定义会冲突报错。
- 单次脚本执行的指令数上限为 1,000,000、调用深度上限为 256 层，超限会终止执行。
- `return` 只能在函数内使用；`break`/`continue` 只能在循环内使用。
- 类型声明（`x: int`）必须同时提供初始值；没有默认值的类字段必须在构造函数中赋值。
- `None` 只能用于 `optional` 上下文；空 `optional` 直接读取、算术或比较会报错。
- `%` 仅支持整数；除零、整数溢出会报错。
- 数组索引必须为 `int`，越界读取或负数索引会报错；索引等于长度时表示追加。
- 字符串未知转义（如 `\x`）保持字面量并产生警告；需要包含引号时也可使用另一种引号包裹。
- `??` 只在 `None` / 空 `optional` 时触发，`0`、`""`、`false` 不会；`??` 与 `&&`/`||` 混用必须加括号。
- 函数调用的参数最多支持 100 个，超出会解析失败。
- 不同模块注册的 `GUIManager::value` / `request` / `callback` ID 各有不同，具体以对应模块的 `.lcui` 文件为准。

> 脚本语言的常见错误及解决方案，请参见 [常见错误含义](./errors.md)。
