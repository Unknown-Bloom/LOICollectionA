# Core Native ABI

> Generated from `exportShape()` of `FunctionCall` / `ClassCall`; do not edit by hand.
> Scope is the platform-neutral runtime core (`math`, `std`, `storage`, `playerdata`, and core value classes); platform namespaces and Bedrock UI classes are documented separately.

## Functions

### `math`

| Function | Parameters |
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

| Function | Parameters |
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

| Function | Parameters |
| --- | --- |
| `format` | `string, array` |

### `storage`

| Function | Parameters |
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

## Native Classes

### `Array`

| Kind | Member | Parameters |
| --- | --- | --- |
| Value method | `contains` | `array` |
| Value method | `contains` | `bool` |
| Value method | `contains` | `float` |
| Value method | `contains` | `function` |
| Value method | `contains` | `int` |
| Value method | `contains` | `object` |
| Value method | `contains` | `string` |
| Value method | `indexOf` | `array` |
| Value method | `indexOf` | `bool` |
| Value method | `indexOf` | `float` |
| Value method | `indexOf` | `function` |
| Value method | `indexOf` | `int` |
| Value method | `indexOf` | `object` |
| Value method | `indexOf` | `string` |
| Value method | `join` | `string` |
| Value method | `length` | - |
| Value method | `pop` | - |
| Value method | `push` | `array` |
| Value method | `push` | `bool` |
| Value method | `push` | `float` |
| Value method | `push` | `function` |
| Value method | `push` | `int` |
| Value method | `push` | `object` |
| Value method | `push` | `string` |
| Value method | `slice` | `int, int` |
| Value method | `sort` | - |
| Value method | `sort` | `function` |

### `CtxValue`

Fields: `value`

| Kind | Member | Parameters |
| --- | --- | --- |
| Constructor | `new` | `int` |

### `GlobalValue`

Fields: `value`

| Kind | Member | Parameters |
| --- | --- | --- |
| Constructor | `new` | - |

### `Map`

Fields: `keys`

| Kind | Member | Parameters |
| --- | --- | --- |
| Constructor | `new` | - |
| Method | `element` | `int` |
| Method | `get` | `bool` |
| Method | `get` | `float` |
| Method | `get` | `int` |
| Method | `get` | `string` |
| Method | `has` | `bool` |
| Method | `has` | `float` |
| Method | `has` | `int` |
| Method | `has` | `string` |
| Method | `length` | - |
| Method | `remove` | `bool` |
| Method | `remove` | `float` |
| Method | `remove` | `int` |
| Method | `remove` | `string` |
| Method | `set` | `bool, array` |
| Method | `set` | `bool, bool` |
| Method | `set` | `bool, float` |
| Method | `set` | `bool, function` |
| Method | `set` | `bool, int` |
| Method | `set` | `bool, object` |
| Method | `set` | `bool, string` |
| Method | `set` | `float, array` |
| Method | `set` | `float, bool` |
| Method | `set` | `float, float` |
| Method | `set` | `float, function` |
| Method | `set` | `float, int` |
| Method | `set` | `float, object` |
| Method | `set` | `float, string` |
| Method | `set` | `int, array` |
| Method | `set` | `int, bool` |
| Method | `set` | `int, float` |
| Method | `set` | `int, function` |
| Method | `set` | `int, int` |
| Method | `set` | `int, object` |
| Method | `set` | `int, string` |
| Method | `set` | `string, array` |
| Method | `set` | `string, bool` |
| Method | `set` | `string, float` |
| Method | `set` | `string, function` |
| Method | `set` | `string, int` |
| Method | `set` | `string, object` |
| Method | `set` | `string, string` |

### `ObservableBoolean`

Operators: `!=`, `==`

| Kind | Member | Parameters |
| --- | --- | --- |
| Constructor | `new` | `bool, bool` |
| Method | `getData` | - |
| Method | `isClientWritable` | - |
| Method | `setData` | `bool` |
| Method | `subscribe` | `function` |
| Method | `unsubscribe` | `int` |

### `ObservableNumber`

Operators: `!=`, `%`, `*`, `+`, `-`, `/`, `<`, `<=`, `==`, `>`, `>=`, `^`

| Kind | Member | Parameters |
| --- | --- | --- |
| Constructor | `new` | `float, bool` |
| Constructor | `new` | `int, bool` |
| Method | `getData` | - |
| Method | `isClientWritable` | - |
| Method | `setData` | `float` |
| Method | `setData` | `int` |
| Method | `subscribe` | `function` |
| Method | `unsubscribe` | `int` |

### `ObservableString`

Operators: `!=`, `+`, `<`, `<=`, `==`, `>`, `>=`

| Kind | Member | Parameters |
| --- | --- | --- |
| Constructor | `new` | `string, bool` |
| Method | `getData` | - |
| Method | `isClientWritable` | - |
| Method | `setData` | `string` |
| Method | `subscribe` | `function` |
| Method | `unsubscribe` | `int` |

### `Proc`

| Kind | Member | Parameters |
| --- | --- | --- |
| Static method | `cancel` | `int` |
| Static method | `join` | `int` |
| Static method | `recv` | - |
| Static method | `send` | `int, array` |
| Static method | `send` | `int, bool` |
| Static method | `send` | `int, float` |
| Static method | `send` | `int, function` |
| Static method | `send` | `int, int` |
| Static method | `send` | `int, object` |
| Static method | `send` | `int, string` |
| Static method | `sleep` | `int` |
| Static method | `spawn` | `function` |
| Static method | `yield` | - |

### `String`

Operators: `!=`, `+`, `<`, `<=`, `==`, `>`, `>=`

| Kind | Member | Parameters |
| --- | --- | --- |
| Value method | `charAt` | `int` |
| Value method | `contains` | `string` |
| Value method | `endsWith` | `string` |
| Value method | `indexOf` | `string` |
| Value method | `lastIndexOf` | `string` |
| Value method | `length` | - |
| Value method | `lower` | - |
| Value method | `replace` | `string, string` |
| Value method | `split` | `string` |
| Value method | `startsWith` | `string` |
| Value method | `substring` | `int, int` |
| Value method | `toFloat` | - |
| Value method | `toInt` | - |
| Value method | `trim` | - |
| Value method | `trimEnd` | - |
| Value method | `trimStart` | - |
| Value method | `upper` | - |

