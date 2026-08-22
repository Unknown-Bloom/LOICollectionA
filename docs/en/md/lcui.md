# LCUI Script Syntax

> [!NOTE]
> The following content is taken from the `.lcui` script language implementation of LOICollectionA 1.15.0 and may differ in later versions.

## Overview

`.lcui` is the script language used by LOICollectionA to write in-game GUIs. All built-in interfaces of modules (such as `blacklist.lcui`, `statistics.lcui`) as well as Menu/Shop's `menu.lcui` and `shop.lcui` are written in this language.

The complete processing flow of a script is:

```text
Lexer (lexical analysis) -> Parser (syntax analysis) -> SemanticAnalyzer (semantic analysis) -> Compiler (bytecode compilation) -> Optimizer (optimization) -> VM (execution)
```

- When the plugin starts or after the `/xxx reload` command is executed, `GUIManager::load` reads the `.lcui` file, completes the compilation flow above, and caches the bytecode.
- When a player opens a GUI, `GUIManager::open(id, formId, type[, ctx])` executes the cached script for that player; forms created in the script with `new CustomForm(...)`, `new PaginatedForm(...)`, etc. are registered with `GUIManager`, and then the form corresponding to `formId` is switched to.

> For the methods and lifecycle of the native form classes, see [Native UI](./native-ui.md); for Menu/Shop-specific forms and default variables, see [LOICollectionAPI](./api.md).

## A Minimal Script

```lcui
form = new CustomForm("example.main", {tr("example.gui.title")});
form.label({tr("example.gui.hello")}, new TextOptions());
form.button({tr("generic.gui.close")}, func () -> void {
    form.close();
}, new ButtonOptions());
form.closeButton();
form.show();
```

## Comments, Literals, and Statement Separation

### Comments

Line comments `//` and block comments `/* ... */` are supported:

```lcui
// This is a line comment
/* This is a
   block comment */
```

### Literals

| Literal | Example | Description |
| --- | --- | --- |
| Integer | `42`, `-1` | 32-bit signed integer |
| Float | `3.14`, `.5`, `5.` | The leading `0` before the decimal point may be omitted |
| String | `"hello"`, `'world'` | Wrapped in single or double quotes |
| Boolean | `true`, `false` | — |
| Null | `None` | Only usable in an `optional` context |
| Array | `[1, "a", true]` | Element types may be mixed |

Strings have no escape characters; the content inside the quotes is preserved as-is. If a string needs to contain a certain kind of quote, wrap it with the other kind of quote, e.g. `'say "hi"'`; strings may also span multiple lines.

### Statement Separation

Statements can be separated with `;` or with line breaks. Multiple statements on the same line must be separated with `;`; it is recommended to end every statement with `;`:

```lcui
a = 1; b = 2;   // A semicolon is required on the same line

c = 3           // The semicolon can be omitted with a line break
d = 4;
```

## Variables and Types

### Dynamic Variables

Assigning a value directly creates a variable; without a type annotation it is dynamically typed and can be assigned values of other types at any time:

```lcui
a = 1;
a = "test";     // Valid: dynamically typed
b = [1, 2, 3];
```

### Type Annotations

Use `variableName: type = value` to declare a typed variable; a type declaration must also provide an initial value:

```lcui
count: int = 1;
name: string = "LOICollection";
flag: bool = true;
score: float = 1.5;
```

Type annotations are checked on assignment; assigning a value of a mismatched type, or assigning a value of another type after declaration, raises an error.

### Built-in Types

| Type | Description |
| --- | --- |
| `int` | Integer |
| `float` | Float |
| `string` | String |
| `bool` | Boolean |
| `void` | Used when a function has no return value |
| `ClassName` | A custom class or native class type |
| `optional<T>` | Optional value; may be empty |
| `variant<T1, T2, ...>` | Union value; can only hold one of the declared types |

Function parameters, return values, and variables without type annotations are all dynamically typed. `None` is a null literal, not a writable type name (when `optional` is empty, its `.type` returns `none`).

### optional (Optional Values)

`optional<T>` can hold a value of type `T`, or the null value `None`:

```lcui
value: optional<int> = 1;
empty: optional<string> = None;

value.has_value;    // true
value.value;        // 1
empty.has_value;    // false
empty.value;        // Error: optional is empty
```

`optional` supports three special members:

| Member | Description |
| --- | --- |
| `has_value` | Whether it has a value (boolean) |
| `value` | The current value; accessing it when empty raises an error |
| `type` | The type name of the current value; `none` when empty |

`None` can only appear in an `optional` context (e.g. the initial value of an optional variable, optional parameters, optional return values); using it elsewhere raises an error. An empty `optional` directly participating in operations such as arithmetic or comparison also raises an error; it is recommended to check with `if (value)` or `has_value` first.

### variant (Union Values)

`variant<T1, T2, ...>` can only hold one of the types in the declaration list; assignment is validated:

```lcui
data: variant<string, bool, int> = 1;
data.type;      // "int"
data.value;     // 1
data = "test";  // Valid
data = 1.5;     // Error: float is not in the declaration list
```

`variant` requires at least two type parameters (e.g. `variant<int>` is invalid).

### Type Aliases with using

`using` can define an alias for a type; it is only allowed at the top level of a script:

```lcui
using Value = variant<string, bool, int>;
using Count = int;

a: Value = 1;
b: Count = 3;
```

Aliases can be chained (e.g. `using B = A;`), but cannot be self-referencing or defined more than once.

### Arrays

Arrays are created with `[element1, element2, ...]` and support arbitrary mixed types:

```lcui
items = [1, "a", true];
items[0];       // 1
items.length;   // 3

items.push(2);  // Appends an element and returns the new length
items[1] = "b"; // Modifies an existing element (assigning at index == length also appends)
```

Array indices start at `0`; out-of-bounds reads and negative indices raise an error. Array `==`/`!=` compares references (whether they are the same array), not contents. An array field of a class is isolated between instances (it is copied when an instance is created); modifying the array of one instance does not affect other instances.

## Operators

### Arithmetic Operators

| Operator | Description |
| --- | --- |
| `+` | Addition / string concatenation |
| `-` | Subtraction |
| `*` | Multiplication |
| `/` | Division (dividing two integers also produces a float) |
| `%` | Modulo (integers only) |
| `^` | Power (right-associative) |
| `+`/`-` (unary) | Positive sign / negative sign |

```lcui
1 + 2 * 3;   // 7
2 ^ 3 ^ 2;   // 512 (right-associative)
10 / 3;      // 3.33333...
10 % 3;      // 1
"Hello " + "World";  // "Hello World"
```

When `+` is used with strings and numeric, boolean, and other types, everything is converted to string concatenation. `%` can only be used with integers; division by zero, integer overflow, and modulo on non-integer types all raise errors.

### Comparison Operators

`==`, `!=`, `>`, `<`, `>=`, `<=` can be used for numeric, string, and boolean comparisons; numeric `int`/`float` values can be compared directly. String comparison rules roughly follow C++. `==`/`!=` on mismatched types raises an error (e.g. `1 == "1"`).

Objects, arrays, and functions support `==`/`!=`, but comparison is by reference:

```lcui
a = [1];
b = [1];
a == b;   // false: same contents but not the same array
c = a;
a == c;   // true
```

`None == None` is `true`.

### Logical Operators

`&&` and `||` support short-circuit evaluation: `false && anyExpression` does not evaluate the right side, and `true || anyExpression` likewise does not evaluate the right side. `!` is logical negation.

### Truthiness Rules

Conditional checks use the following truthiness rules:

| Value | Truthiness |
| --- | --- |
| `int` | Truthy if not `0` |
| `float` | Truthy if the absolute value is greater than `0` |
| `string` | `""`, `"false"`, `"FALSE"` (case-insensitive) are falsy; all other non-empty strings are truthy |
| `bool` | Itself |
| `array` | Truthy if non-empty |
| Object / function | Always truthy |
| `None` | Always falsy |

## Flow Control

### if / else

`if` uses square brackets `[ ... ]` as the branch block, `:` separates the else branch, and the else branch may be omitted:

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

`if` itself is also an expression; the last value in a branch is returned as the result and can take part in operations or assignments:

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

`for (;;)` allows all three clauses to be omitted, and is used together with `break`.

### break / continue

`break` exits the current loop, and `continue` skips the remainder of the current iteration; both can only appear inside a loop, and with nested loops they only affect the innermost one.

## Functions

### Named Functions

`func` defines a function; parameters and return values may be annotated with types or omitted (omitted means dynamically typed):

```lcui
func add(a: int, b: int) -> int {
    return a + b;
}

func pick(x) {
    return x;
}

result = add(1, 2);
```

Functions support recursion and mutual recursion, and also support overloading by parameter type:

```lcui
func id(x: int) -> int {
    return x;
}

func id(x: string) -> string {
    return x;
}

id(7);      // Calls the int version
id("s");    // Calls the string version
```

Function and class definitions can only appear at the top level of a script; they cannot be nested inside functions or other blocks. `return` can only appear in a function body; a function without an explicit `return` returns the null value.

### Anonymous Functions (Lambdas)

`func (parameters) -> returnType { ... }` defines an anonymous function; it can be assigned to a variable, passed as a parameter, and captures outer local variables, parameters, and `this`:

```lcui
double = func (x: int) -> int {
    return x * 2;
};

form.button("Button", func () -> void {
    GUIManager::callback("example.submit", [ double(21) ]);
}, new ButtonOptions());
```

## Classes and Inheritance

### Defining Classes

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

### Fields and Methods

- A field can have a default value, a type annotation, both, or neither (dynamically typed).
- Fields without a default value must be assigned in the constructor.
- The constructor's name must match the class name, and each class can have at most one constructor; when no constructor is defined, the default constructor is used.
- Methods are defined with `func methodName(parameters) -> returnType { ... }` and support overloading.

### Access Control

Use `public:` and `private:` to declare access sections; the default (before the first access section) is public:

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
box.secret;         // Error: private field
```

Private members are not inherited; a subclass cannot access the base class's private members via `this` or bare names.

### Static Members

`static` declares a static field or static method, accessed via `ClassName.member`:

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

Static fields are shared across all instances; `this` cannot be used in a static method, and instance objects cannot call static methods directly. Static members support inheritance and overriding.

### Inheritance

`extends` inherits from a base class:

```lcui
class Dog extends Animal {
    func speak() -> string {
        return "Woof";
    }
}

dog = new Dog("dog");
dog instanceof Animal;  // true
```

Related keywords:

| Keyword | Description |
| --- | --- |
| `extends` | Declares the base class |
| `super(...)` | Calls the base class constructor |
| `super.method(...)` | Calls a base class method |
| `instanceof` | Checks whether an object is an instance of the specified class (including derived classes) |
| `this` | The current object |

Construction rules:

- If a subclass constructor does not explicitly call `super(...)`, the base class's parameterless constructor (or default constructor) is called implicitly.
- When the base class only has a parameterized constructor, the subclass constructor must explicitly call `super(...)`, otherwise an error is raised.
- A class name may be declared before or after use; multi-level inheritance and polymorphism are supported. Circular inheritance, duplicate class names, and unknown base classes raise errors.

Object `==`/`!=` is also a reference comparison.

## Built-in Namespaces and Macros

### Built-in Namespaces

| Namespace | Functions | Description |
| --- | --- | --- |
| `math` | `abs`, `min`, `max`, `sqrt`, `pow`, `log`, `sin`, `cos`, `random` | Math functions |
| `string` | `length`, `upper`, `lower`, `substr`, `trim`, `replace` | String functions |
| `std` | `format(formatStr, array)` | Formats a string with the values in an array (fmt style) |
| `mc` | `runCmd(command)` | Executes a command as the current player |
| `GUIManager` | `value(id)`, `request(id, args)`, `callback(id, args)`, `open(id, formId, type[, ctx])`, `switchTo(id, type)` | Interacts with GUI data |

The call format is `Namespace::function(parameters)`, for example:

```lcui
mc::runCmd("say hello");
GUIManager::callback("example.submit", [ "data" ]);
text = std::format({tr("example.gui.info")}, [ 1, "two" ]);
```

> The `type` in `GUIManager::open` and `GUIManager::switchTo` is an integer: `1` is CustomForm, `2` is MessageBox, `3` is PaginatedForm, `4` is ScriptForm.

### Macros `{...}`

Call a macro with `{name}` or `{name(parameters)}`, for example `{tr("language.gui.title")}`. `tr` returns the translated text for the current player's language; default variables such as `{player}`, `{server_tps}`, and `{score(name)}` are also used in macro form; see [LOICollectionAPI](./api.md) for the full list.

```lcui
title = {tr("example.gui.title")};
label = {player} + " -> " + {tr("example.gui.hello")};
```

### Passthrough and Blocking with `$`

`$` is used to pass through raw text: the content between `$` and `}` (or `;`) is kept as-is and does not participate in parsing:

```lcui
raw = $custom content};
```

The `${variable}` form can block variable/macro replacement, keeping the content in literal form; for example, `${team}` in `{player_realname} + '->' + ${team}` is output verbatim as `{team}`.

## Native Classes

The following native classes can be directly `new`ed in a script (overview):

| Class | Purpose | Detailed docs |
| --- | --- | --- |
| `GlobalValue` | Global value container, field `value` | [api.md](./api.md) |
| `CtxValue` | Reads the `ctx` array passed to `GUIManager::open`, field `value` | See below |
| `ObservableString` / `ObservableNumber` / `ObservableBoolean` / `ObservableUIRawMessage` | Observable data for two-way binding of controls | [native-ui.md](./native-ui.md) |
| `UIRawMessage` | Constructs rich text / translated text | [native-ui.md](./native-ui.md) |
| `CustomForm` / `MessageBox` / `PaginatedForm` | Native forms | [native-ui.md](./native-ui.md) |
| `TextOptions` / `ButtonOptions` / `TextFieldOptions` / `DropdownOptions` / `DropdownItem` / `SliderOptions` / `ToggleOptions` / `DividerOptions` / `SpacingOptions` | Control options | [native-ui.md](./native-ui.md) |
| `MenuData` / `MenuItemData` / `MenuControlData` / `MenuForm` / `MenuMessageBox` | Menu data and forms | [api.md](./api.md) |
| `ShopData` / `ShopItemData` / `ShopForm` | Shop data and forms | [api.md](./api.md) |
| `ScoreRequirement` | Score requirement, fields `objective`, `value` | [api.md](./api.md) |

### CtxValue

When a script is opened via `GUIManager::open(id, formId, type, ctx)` and a `ctx` array is passed in, `new CtxValue(index)` can be used to read the corresponding element:

```lcui
detail = new CtxValue(0);
form.label(detail.value, new TextOptions());
```

Using `CtxValue` with an out-of-bounds index or when no `ctx` was passed in raises an error.

## Complete Example

A comprehensive example using variables, types, functions, classes, flow control, and native forms:

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

form = new CustomForm("example.shop", {tr("example.shop.title")});
form.label({tr("example.shop.list")}, new TextOptions());
form.button(items[0].format(), func () -> void {
    selected.value = items[0].name;
    form.close();
}, new ButtonOptions());
form.button(items[1].format(), func () -> void {
    selected.value = items[1].name;
    form.close();
}, new ButtonOptions());
form.closeButton();
form.show();
```

## Common Constraints and Pitfalls

- Classes, named functions, and `using` can only appear at the top level of a script; they cannot be nested.
- `return` can only be used inside a function; `break`/`continue` can only be used inside a loop.
- A type declaration (`x: int`) must also provide an initial value; class fields without a default value must be assigned in the constructor.
- `None` can only be used in an `optional` context; directly reading, performing arithmetic on, or comparing an empty `optional` raises an error.
- `%` only supports integers; division by zero and integer overflow raise errors.
- Array indices must be `int`; out-of-bounds reads and negative indices raise errors; an index equal to the length appends an element.
- Strings have no escape characters; use the other kind of quote to include a quote character.
- A function call supports at most 100 arguments; exceeding this fails to parse.
- The `GUIManager::value` / `request` / `callback` IDs registered by different modules differ; refer to the `.lcui` file of the corresponding module.

> For common errors of the script language and their solutions, see [Common Error Meanings](./errors.md).
