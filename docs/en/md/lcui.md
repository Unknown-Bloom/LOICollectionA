# LCUI Script Syntax

> [!NOTE]
> The following content is taken from the `.lcui` script language implementation of LOICollectionA 1.15.0 and may differ in later versions.

## Overview

`.lcui` is the script language used by LOICollectionA to write in-game GUIs. All built-in interfaces of modules (such as `blacklist.lcui`, `statistics.lcui`) as well as Menu/Shop's `menu.lcui` and `shop.lcui` are written in this language.

The complete processing flow of a script is:

```text
ScriptLoader (import resolution and merging) -> Lexer (lexical analysis) -> Parser (syntax analysis) -> SemanticAnalyzer (semantic analysis) -> Compiler (bytecode compilation) -> Optimizer (optimization) -> VM (execution)
```

- When the plugin starts or after the `/xxx reload` command is executed, `GUIManager::load` reads the `.lcui` file, first resolves the `import` graph (see "Multi-file Imports"), and then completes the compilation flow above; besides being cached in memory, the compiled bytecode is also written to disk as a `.lcp` file (see "Bytecode Package").
- When a player opens a GUI, `GUIManager::open(id, formId, type[, ctx])` executes the cached script for that player; forms created in the script with `new CustomForm(...)`, `new PaginatedForm(...)`, etc. are registered with `GUIManager`, and then the form corresponding to `formId` is switched to.

> For the methods and lifecycle of the native form classes, see [Native UI](./native-ui.md); for Menu/Shop-specific forms and default variables, see [LOICollectionAPI](./api.md).

## A Minimal Script

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

## Declarative UI Blocks

When creating a form, a `{ ... }` block may follow the constructor call to declare controls in display order. Method calls without a receiver inside the block are automatically applied to the form under construction. The minimal script above is fully equivalent to the manually expanded imperative form:

```lcui
form = new CustomForm("example.main", {tr("example.gui.title")});
form.label({tr("example.gui.hello")}, new TextOptions());
form.button({tr("generic.gui.close")}, on: func () -> void {
    form.close();
}, new ButtonOptions());
form.closeButton();
form.show();
```

Key points:

- **Receiver**: in `var = new FormClass(args) { ... }`, bare calls inside the block (e.g. `label(...)`) desugar to `var.label(...)`. The receiver variable is bound before the block body runs, so lambdas defined inside the block (such as button handlers) may reference the form under construction by name, like `form.close()` above.
- **`on:` named argument**: inside a block (and only inside a block), methods that take a handler such as `button` accept the handler via `on: handler`, in the same argument position as the explicit form.
- **Control flow**: statements such as `if` are allowed inside the block; which controls get added follows the actual execution order. For example, in the built-in `market_store.lcui`, the audit button is only added for admins when reviews are enabled:

```lcui
mineForm = new CustomForm("market.store.mine", {tr("market.gui.title")}) {
    /* ... other buttons ... */
    if (GUIManager::request("market.isAdmin", [])[0] && GUIManager::request("market.store.review.enabled", [])[0]) [
        button({tr("market.gui.store.mine.audit")}, on: func () -> void {
            navigateAudit.value = true;

            mineForm.close();
        }, auditOption);
    ]

    closeButton();
};
```

- **Form classes only**: declarative blocks are only allowed on `CustomForm`, `MessageBox`, `PaginatedForm`, and `ScriptForm`; using one on another class (e.g. `new GlobalValue() { ... }`) is a compile error.
- **Plain function fallback**: bare calls inside the block that are not form methods fall back to ordinary function resolution, so imported helpers (e.g. `pagePrevious()`, `saveLabel()`) can be called directly inside a block.

## Components

`component` abstracts repeated form-control combinations into reusable fragments. A component is expanded at its use site as a compile-time macro (hygienic expansion): parameters bind by name, there is zero runtime cost, and bare method calls inside the component body inherit the implicit form receiver of the use site:

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

At compile time the above is equivalent to writing directly inside the block:

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

Rules:

| Rule | Description |
| --- | --- |
| Definition site | Components may only be defined at script top level; defining one inside a function body or block is a compile error |
| Use site | A component call may only appear as a statement inside a declarative UI block; calling it outside a block or using it as an expression is a compile error |
| Parameter binding | The number of call arguments must match the component parameters; they bind positionally to the parameter names |
| Hygienic expansion | Local variables declared inside the component body are automatically renamed; they neither capture nor shadow same-named outer variables at the use site, and each use site expands independently |
| No recursion | A component calling itself (directly or indirectly) is a compile error |
| Block capabilities | Control flow and local variables (block-scoped) are allowed inside the component body, with exactly the same meaning as if written by hand inside the declarative block |
| Receiver inheritance | Expansion happens at the use site, so bare calls inside the component body act on the form receiver of the use site (`form` above) |
| Cross-file reuse | Component definitions are top-level definitions; they may live in an `import`-ed file (e.g. `generic.lcui`) and be shared by many scripts |

The shared control combinations of the built-in scripts are defined in `generic.lcui`: `PageControls()` (previous/next/jump pagination buttons), `SectionHeader()` (spacer + divider), and `ConfirmBar(confirmText, onConfirm)` (confirm button + close button).

## Multi-file Imports

The top of a script may use `import` to pull in the top-level definitions (`class` / `func` / `using` / `component`) of other `.lcui` files for cross-file reuse. The shared GUI vocabulary of the built-in scripts (pagination buttons, save/cancel labels, etc.) is extracted into `generic.lcui`:

```lcui
import "generic.lcui";

quote = new PaginatedForm("market.quote", {tr("market.gui.title")}, GUIManager::value("market.quote.items"), 10) {
    label({tr("market.gui.quote.list.label")}, new TextOptions());
    PageControls();
    closeButton();
    show();
};
```

Rules:

| Rule | Description |
| --- | --- |
| Path resolution | `import` paths are resolved relative to the directory of the entry script |
| Merge order | The import graph is expanded dependencies-first (topological order); each file's definitions are merged exactly once. Diamond imports (A and B both import C) are legal, and C appears once |
| Definition conflicts | Same-named `class` / `func` / `using` / `component` in different files raise an error listing the file and line of both definitions |
| Definitions only | Imported files may only contain `class` / `func` / `using` / `component` / `import` — no executable statements (imports must not have side effects); the entry file is exempt |
| Circular imports | Circular imports (e.g. A -> B -> A) raise an error with the full cycle path |
| Missing files | An import whose file is missing or unreadable raises an error |

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

## Execution Budget and Call Depth

To keep runaway scripts from hanging the server, the VM enforces two hard limits on every script execution:

| Limit | Cap | Behavior on breach |
| --- | --- | --- |
| Instructions | 1,000,000 | Execution aborts with `Execution budget exhausted` |
| Call depth | 256 call frames | Execution aborts with `Call stack depth limit exceeded` |

Ordinary GUI scripts stay far below both caps; a loop that forgot its step (e.g. `while (true)`) or an unbounded recursion is aborted in time instead of dragging the server down.

## Bytecode Optimization

After compilation and before execution, the bytecode passes through an optimizer. Optimization only changes *how* something is computed, never *what* is computed — runtime errors still fire exactly as written, with their original source locations intact. Everything below is transparent to script authors, but knowing it helps write scripts that naturally benefit:

| Optimization | Example | Effect |
| --- | --- | --- |
| Constant folding | `1 + 2 * 3` | Folded to `PUSH_INT 7` at compile time; nothing computed at runtime |
| Variable forwarding | `x = 1; if (x == 1) [...]` | Later reads of `x` are replaced by the known value, and the branch folds away |
| Pure-function folding | `math::abs(-3)`, `math::pow(2, 10)` | `math::` pure functions with all-constant arguments evaluate at compile time; impure ones like `math::random` never fold |
| Super-instructions | `x = 1` (`DUP` + `STORE_VAR`) | Hot instruction pairs fuse into a single `DUP_STORE` / `DUP_IS_NONE`, doing two jobs in one dispatch |
| Jump peephole | A conditional jump immediately followed by an unconditional jump to the same target | The double jump collapses and the unreachable block in between is removed |
| Dead-code elimination | `while (false) [...]`, `if (false) [...]` | Unreachable branches are removed wholesale |

The optimizer iterates these passes until the bytecode stops changing (a fixed point), so opportunities exposed by one rewrite (e.g. variable forwarding turning a condition into a constant) are picked up by the next round.

At runtime there is an additional layer of **inline caching**: when a script calls the same native function or native method repeatedly (e.g. `math::abs(...)` or `arr.push(...)` inside a loop), the VM remembers the previous resolution and skips signature matching and registry lookups on a hit. This affects speed only, never behavior — on invalidation (e.g. a plugin reload mutating the registry) the VM falls back to the full lookup.

## Bytecode Package

After compiling a script, `GUIManager::load` serializes the bytecode to a `<script>.lcp` file next to the source (e.g. `market.lcui` maps to `market.lcui.lcp`):

- Subsequent loads read the `.lcp` package first; on a hit, lexing/parsing/semantic analysis and compilation are skipped and the bytecode is reused directly.
- The package header records the script id and an **ABI fingerprint** — a hash over the shape of every registered native class, function and macro. A plugin upgrade that changes any native signature alters the fingerprint, which invalidates old packages and triggers recompilation.
- When the source is present, the package additionally records the SHA-256 of the entry file and of every file in the import graph; a change to any of them triggers recompilation.
- The serialized data carries a checksum; a corrupted package is discarded and recompiled.
- A package is self-contained: with the `.lcui` source removed, the script still loads as long as the ABI fingerprint matches. This is how closed-source scripts are distributed.
- Debug information lives in a separate `.lcp.dbg` file; when it is missing the bytecode still executes, only line/column information is lost.

The current package format version is v5.

## Common Constraints and Pitfalls

- Classes, named functions, `using`, and components can only appear at the top level of a script; they cannot be nested.
- Declarative UI blocks are only allowed on form classes (`CustomForm` / `MessageBox` / `PaginatedForm` / `ScriptForm`); the `on:` named argument is only available inside a block.
- Component calls may only appear as statements inside a declarative UI block; component recursion (direct or indirect) is a compile error.
- Imported files may only contain top-level definitions (`class` / `func` / `using` / `component` / `import`); same-named definitions in different files conflict and raise an error.
- Every script execution is capped at 1,000,000 instructions and 256 call frames; exceeding either aborts the execution.
- `return` can only be used inside a function; `break`/`continue` can only be used inside a loop.
- A type declaration (`x: int`) must also provide an initial value; class fields without a default value must be assigned in the constructor.
- `None` can only be used in an `optional` context; directly reading, performing arithmetic on, or comparing an empty `optional` raises an error.
- `%` only supports integers; division by zero and integer overflow raise errors.
- Array indices must be `int`; out-of-bounds reads and negative indices raise errors; an index equal to the length appends an element.
- Strings have no escape characters; use the other kind of quote to include a quote character.
- A function call supports at most 100 arguments; exceeding this fails to parse.
- The `GUIManager::value` / `request` / `callback` IDs registered by different modules differ; refer to the `.lcui` file of the corresponding module.

> For common errors of the script language and their solutions, see [Common Error Meanings](./errors.md).
