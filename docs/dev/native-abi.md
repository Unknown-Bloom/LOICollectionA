# 核心原生 ABI

> 由 `FunctionCall` / `ClassCall` 的 `exportShape()` 生成，请勿手工编辑。
> 范围是跨平台的运行时核心（`math`、`std`、`storage`、`playerdata` 与核心值类）；平台命名空间与 Bedrock UI 类见各自的文档。

## 函数

### `math`

| 函数 | 参数 |
| --- | --- |
| `cos` | `float` |
| `cos` | `int` |
| `log` | `float` |
| `log` | `int` |
| `pow` | `float, float` |
| `pow` | `int, int` |
| `random` | `float, float` |
| `random` | `int, int` |
| `sin` | `float` |
| `sin` | `int` |
| `sqrt` | `float` |
| `sqrt` | `int` |

### `playerdata`

| 函数 | 参数 |
| --- | --- |
| `clear` | - |
| `get` | `string` |
| `has` | `string` |
| `keys` | - |
| `remove` | `string` |
| `set` | `string, array` |
| `set` | `string, bool` |
| `set` | `string, float` |
| `set` | `string, int` |
| `set` | `string, object` |
| `set` | `string, string` |

### `std`

| 函数 | 参数 |
| --- | --- |
| `format` | `string, array` |

### `storage`

| 函数 | 参数 |
| --- | --- |
| `clear` | - |
| `get` | `string` |
| `has` | `string` |
| `keys` | - |
| `remove` | `string` |
| `set` | `string, array` |
| `set` | `string, bool` |
| `set` | `string, float` |
| `set` | `string, int` |
| `set` | `string, object` |
| `set` | `string, string` |

## 原生类

### `Array`

| 类型 | 成员 | 参数 |
| --- | --- | --- |
| 值方法 | `contains` | `array` |
| 值方法 | `contains` | `bool` |
| 值方法 | `contains` | `float` |
| 值方法 | `contains` | `function` |
| 值方法 | `contains` | `int` |
| 值方法 | `contains` | `object` |
| 值方法 | `contains` | `string` |
| 值方法 | `indexOf` | `array` |
| 值方法 | `indexOf` | `bool` |
| 值方法 | `indexOf` | `float` |
| 值方法 | `indexOf` | `function` |
| 值方法 | `indexOf` | `int` |
| 值方法 | `indexOf` | `object` |
| 值方法 | `indexOf` | `string` |
| 值方法 | `join` | `string` |
| 值方法 | `length` | - |
| 值方法 | `pop` | - |
| 值方法 | `push` | `array` |
| 值方法 | `push` | `bool` |
| 值方法 | `push` | `float` |
| 值方法 | `push` | `function` |
| 值方法 | `push` | `int` |
| 值方法 | `push` | `object` |
| 值方法 | `push` | `string` |
| 值方法 | `slice` | `int, int` |
| 值方法 | `sort` | - |
| 值方法 | `sort` | `function` |

### `CtxValue`

字段：`value`

| 类型 | 成员 | 参数 |
| --- | --- | --- |
| 构造函数 | `new` | `int` |

### `GlobalValue`

字段：`value`

| 类型 | 成员 | 参数 |
| --- | --- | --- |
| 构造函数 | `new` | - |

### `Map`

字段：`keys`

| 类型 | 成员 | 参数 |
| --- | --- | --- |
| 构造函数 | `new` | - |
| 方法 | `element` | `int` |
| 方法 | `get` | `bool` |
| 方法 | `get` | `float` |
| 方法 | `get` | `int` |
| 方法 | `get` | `string` |
| 方法 | `has` | `bool` |
| 方法 | `has` | `float` |
| 方法 | `has` | `int` |
| 方法 | `has` | `string` |
| 方法 | `length` | - |
| 方法 | `remove` | `bool` |
| 方法 | `remove` | `float` |
| 方法 | `remove` | `int` |
| 方法 | `remove` | `string` |
| 方法 | `set` | `bool, array` |
| 方法 | `set` | `bool, bool` |
| 方法 | `set` | `bool, float` |
| 方法 | `set` | `bool, function` |
| 方法 | `set` | `bool, int` |
| 方法 | `set` | `bool, object` |
| 方法 | `set` | `bool, string` |
| 方法 | `set` | `float, array` |
| 方法 | `set` | `float, bool` |
| 方法 | `set` | `float, float` |
| 方法 | `set` | `float, function` |
| 方法 | `set` | `float, int` |
| 方法 | `set` | `float, object` |
| 方法 | `set` | `float, string` |
| 方法 | `set` | `int, array` |
| 方法 | `set` | `int, bool` |
| 方法 | `set` | `int, float` |
| 方法 | `set` | `int, function` |
| 方法 | `set` | `int, int` |
| 方法 | `set` | `int, object` |
| 方法 | `set` | `int, string` |
| 方法 | `set` | `string, array` |
| 方法 | `set` | `string, bool` |
| 方法 | `set` | `string, float` |
| 方法 | `set` | `string, function` |
| 方法 | `set` | `string, int` |
| 方法 | `set` | `string, object` |
| 方法 | `set` | `string, string` |

### `ObservableBoolean`

运算符：`!=`, `==`

| 类型 | 成员 | 参数 |
| --- | --- | --- |
| 构造函数 | `new` | `bool, bool` |
| 方法 | `getData` | - |
| 方法 | `isClientWritable` | - |
| 方法 | `setData` | `bool` |
| 方法 | `subscribe` | `function` |
| 方法 | `unsubscribe` | `int` |

### `ObservableNumber`

运算符：`!=`, `%`, `*`, `+`, `-`, `/`, `<`, `<=`, `==`, `>`, `>=`, `^`

| 类型 | 成员 | 参数 |
| --- | --- | --- |
| 构造函数 | `new` | `float, bool` |
| 构造函数 | `new` | `int, bool` |
| 方法 | `getData` | - |
| 方法 | `isClientWritable` | - |
| 方法 | `setData` | `float` |
| 方法 | `setData` | `int` |
| 方法 | `subscribe` | `function` |
| 方法 | `unsubscribe` | `int` |

### `ObservableString`

运算符：`!=`, `+`, `<`, `<=`, `==`, `>`, `>=`

| 类型 | 成员 | 参数 |
| --- | --- | --- |
| 构造函数 | `new` | `string, bool` |
| 方法 | `getData` | - |
| 方法 | `isClientWritable` | - |
| 方法 | `setData` | `string` |
| 方法 | `subscribe` | `function` |
| 方法 | `unsubscribe` | `int` |

### `Proc`

| 类型 | 成员 | 参数 |
| --- | --- | --- |
| 静态方法 | `cancel` | `int` |
| 静态方法 | `join` | `int` |
| 静态方法 | `recv` | - |
| 静态方法 | `send` | `int, array` |
| 静态方法 | `send` | `int, bool` |
| 静态方法 | `send` | `int, float` |
| 静态方法 | `send` | `int, function` |
| 静态方法 | `send` | `int, int` |
| 静态方法 | `send` | `int, object` |
| 静态方法 | `send` | `int, string` |
| 静态方法 | `sleep` | `int` |
| 静态方法 | `spawn` | `function` |
| 静态方法 | `yield` | - |

### `String`

运算符：`!=`, `+`, `<`, `<=`, `==`, `>`, `>=`

| 类型 | 成员 | 参数 |
| --- | --- | --- |
| 值方法 | `charAt` | `int` |
| 值方法 | `contains` | `string` |
| 值方法 | `endsWith` | `string` |
| 值方法 | `indexOf` | `string` |
| 值方法 | `lastIndexOf` | `string` |
| 值方法 | `length` | - |
| 值方法 | `lower` | - |
| 值方法 | `replace` | `string, string` |
| 值方法 | `split` | `string` |
| 值方法 | `startsWith` | `string` |
| 值方法 | `substring` | `int, int` |
| 值方法 | `toFloat` | - |
| 值方法 | `toInt` | - |
| 值方法 | `trim` | - |
| 值方法 | `trimEnd` | - |
| 值方法 | `trimStart` | - |
| 值方法 | `upper` | - |

