# LCUI Script Syntax

> [!NOTE]
> The following content is taken from the `.lcui` script language implementation of LOICollectionA 1.17.3 and may differ in later versions.

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
let form = new CustomForm("example.main", {tr("example.gui.title")}) {
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
let form = new CustomForm("example.main", {tr("example.gui.title")});
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
let mineForm = new CustomForm("market.store.mine", {tr("market.gui.title")}) {
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

let form = new CustomForm("wallet.main", {tr("wallet.gui.title")}) {
    label({tr("wallet.gui.label")}, new TextOptions());
    ConfirmBar(saveLabel(), func () -> void {
        form.close();
    });
    show();
};
```

At compile time the above is equivalent to writing directly inside the block:

```lcui
let form = new CustomForm("wallet.main", {tr("wallet.gui.title")}) {
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

`import` paths are resolved relative to the file that contains the `import` statement (for an entry script, that is its own directory); this also keeps `std/` modules stable when they reference each other.

The top of a script may use `import` to pull in the top-level definitions (`class` / `func` / `using` / `component` / `enum`) of other `.lcui` files for cross-file reuse. The shared GUI vocabulary of the built-in scripts (pagination buttons, save/cancel labels, etc.) is extracted into `generic.lcui`:

```lcui
import "generic.lcui";

let quote = new PaginatedForm("market.quote", {tr("market.gui.title")}, GUIManager::value("market.quote.items"), 10) {
    label({tr("market.gui.quote.list.label")}, new TextOptions());
    PageControls();
    closeButton();
    show();
};
```

A file can also be imported selectively: append `{ name, ... }` to the path and only the listed definitions are merged (referencing an unlisted definition raises an undefined error):

```lcui
import "generic.lcui" { PageControls, closeButton };
```

Rules:

| Rule | Description |
| --- | --- |
| Path resolution | `import` paths are resolved relative to the directory of the **importing file**; `std/` modules follow the same rule when they reference each other |
| Merge order | The import graph is expanded dependencies-first (topological order); each file's definitions are merged exactly once. Diamond imports (A and B both import C) are legal, and C appears once |
| Selective imports | `import "path" { a, b };` merges only the listed definitions; multiple import sites union their symbols; omitting the list still imports everything |
| Definition conflicts | Same-named `class` / `func` / `using` / `component` / `enum` in different files raise an error listing the file and line of both definitions |
| Definitions only | Imported files may only contain `class` / `func` / `using` / `component` / `enum` / `import` — no executable statements (imports must not have side effects); the entry file is exempt |
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

Strings support the escape sequences `\n` (newline), `\t` (tab), `\r` (carriage return), `\\` (backslash), `\"` (double quote), and `\'` (single quote). Unknown escapes such as `\x` keep their literal form and produce a compile-time warning. If a string needs to contain a certain kind of quote, wrap it with the other kind of quote, e.g. `'say "hi"'`; strings may also span multiple lines.

### Statement Separation

Statements can be separated with `;` or with line breaks. Multiple statements on the same line must be separated with `;`; it is recommended to end every statement with `;`:

```lcui
let a = 1; let b = 2;   // A semicolon is required on the same line

let c = 3           // The semicolon can be omitted with a line break
let d = 4;
```

## Variables and Types

### Variable Declarations (let)

Every variable binding must be declared with `let`; the keyword-less implicit creation via a bare assignment is no longer supported:

```lcui
let a = 1;          // mutable variable, dynamically typed
let count: int = 1; // mutable variable with a type annotation
```

A `let` variable can be reassigned at any time and its type may still change dynamically. `const` is not a statement-level keyword — it only declares associated constants inside `trait` / `impl` bodies (for example `const G = 9.8;`). This rule only constrains local and global variable bindings — function parameters, class fields, and `optional`/`variant` members keep their original structural declaration syntax.

### Dynamic Variables

A `let` declaration without a type annotation is dynamically typed and can be assigned values of other types at any time:

```lcui
let a = 1;
a = "test";     // Valid: dynamically typed
let b = [1, 2, 3];
```

### Type Annotations

Use `let variableName: type = value` to declare a typed variable; a type declaration must also provide an initial value:

```lcui
let count: int = 1;
let name: string = "LOICollection";
let flag: bool = true;
let score: float = 1.5;
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
let value: optional<int> = 1;
let empty: optional<string> = None;

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
let data: variant<string, bool, int> = 1;
data.type;      // "int"
data.value;     // 1
data = "test";  // Valid
data = 1.5;     // Error: float is not in the declaration list
```

`variant` requires at least two type parameters (e.g. `variant<int>` is invalid).

### Enums (enum)

`enum` declares a tagged union and may only appear at script top level. Variant names are unique across the whole program. A payload-free variant may omit parentheses in the declaration, but construction and pattern matching must write `Variant()`:

```lcui
enum Option {
    Some(int)
    Nothing
}

let a = Some(42);
let b = Nothing();

a == Some(42);   // true (compares the tag and every payload)
a == Nothing();  // false
```

Variants may carry several payloads; construct them by calling the variant name, and the argument count must match the declaration:

```lcui
enum Shape {
    Point(int, int)
    Empty
}

let p = Point(3, 4);
let label = match (p) [
    Point(x, y) [ "(" + x + ", " + y + ")" ]
    Empty() [ "empty" ]
];
```

Enum values compare by content and cannot participate in arithmetic. Destructure them with `match`, `if let`, or `while let` (see "match (Pattern Matching)"). A variant belongs to the enum that declares it; matching it against another enum's value is a compile error.

### Type Aliases with using

`using` can define an alias for a type; it is only allowed at the top level of a script:

```lcui
using Value = variant<string, bool, int>;
using Count = int;

let a: Value = 1;
let b: Count = 3;
```

Aliases can be chained (e.g. `using B = A;`), but cannot be self-referencing or defined more than once.

### Arrays

Arrays are created with `[element1, element2, ...]` and support arbitrary mixed types:

```lcui
let items = [1, "a", true];
items[0];        // 1
items.length();  // 3

items.push(2);  // Appends an element and returns the new length
items[1] = "b"; // Modifies an existing element (assigning at index == length also appends)
```

Array indices start at `0`; out-of-bounds reads and negative indices raise an error. Array `==`/`!=` compares references (whether they are the same array), not contents. An array field of a class is isolated between instances (it is copied when an instance is created); modifying the array of one instance does not affect other instances.

### Array Methods

Arrays provide the following methods (available directly as `array.method(...)`):

| Method | Signature | Description |
| --- | --- | --- |
| `push` | `arr.push(v)` | Appends an element and returns the new length |
| `pop` | `arr.pop()` | Removes and returns the last element; an empty array raises an error |
| `contains` | `arr.contains(v)` | Whether an equal element exists (`==` semantics) |
| `indexOf` | `arr.indexOf(v)` | Index of the first occurrence; `-1` when absent |
| `join` | `arr.join(sep)` | Converts every element to a string and joins them with `sep` |
| `slice` | `arr.slice(start, end)` | Half-open slice returning a new array; out-of-range bounds are clamped |
| `sort` | `arr.sort([cmp])` | Sorts in place; `cmp` is `func(a, b) -> int`; without it every pair of elements must be comparable |

```lcui
let items = [3, 1, 2];
items.push(4);        // items = [3, 1, 2, 4], returns 4
items.sort();         // items = [1, 2, 3, 4]
let names = items.join(",");   // "1,2,3,4"

let words = ["b", "a"];
words.sort(func (a, b) -> int {
    return if (a < b) [ -1 : 1 ];
});
```

The argument-less `sort` raises a runtime error when a pair of elements cannot be compared (e.g. numbers mixed with strings); `slice` raises an error for negative indices.

### String Methods

Strings are value types and also expose methods as `string.method(...)`:

| Method | Signature | Description |
| --- | --- | --- |
| `length` | `s.length()` | Character count (Unicode code points) |
| `charAt` | `s.charAt(i)` | The `i`-th character; returns an empty string when out of range or negative |
| `substring` | `s.substring(start, end)` | Half-open substring counted in code points; bounds are clamped |
| `upper` / `lower` | `s.upper()` | ASCII case conversion |
| `trim` / `trimStart` / `trimEnd` | `s.trim()` | Strips leading/trailing, leading only, or trailing only whitespace |
| `replace` | `s.replace(from, to)` | Replaces every occurrence; returns the input unchanged when `from` is empty |
| `split` | `s.split(sep)` | Splits into an array; an empty `sep` splits into characters |
| `contains` | `s.contains(sub)` | Whether the substring exists |
| `startsWith` / `endsWith` | `s.startsWith(sub)` | Prefix / suffix test |
| `indexOf` / `lastIndexOf` | `s.indexOf(sub)` | Character index of the first / last occurrence; `-1` when absent |
| `toInt` / `toFloat` | `s.toInt()` | Parses to `optional<int>` / `optional<float>`; empty on failure |

Strings also support `[]` indexing, returning a one-character `string`; out-of-range or negative indices raise an error:

```lcui
"hello"[0];   // "h"
"hello"[4];   // "o"
"hello"[-1];  // Error: negative index
```

```lcui
"a,b,c".split(",");         // ["a", "b", "c"]
"hello".startsWith("he");   // true

let value: optional<int> = "42".toInt();
value.has_value;            // true
value.value;                // 42

let bad: optional<int> = "abc".toInt();
bad.has_value;              // false
```

### Map

`Map` is an insertion-ordered key-value container. Keys may be `int` / `float` / `string` / `bool` (equal values are the same key):

```lcui
let m = new Map();
m.set("apple", 3);
m.set("banana", 5);
m.get("apple");         // 3
m.has("banana");        // true
m.remove("apple");
m.length();             // 1
m.keys;                 // ["banana"], in insertion order

for (k in m.keys) [
    println(k + " = " + m.get(k));
]
```

`get` returns `None` when the key is absent, which works well with the null-safe operators (see below).

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

### Compound Assignment and Increment / Decrement

`=` can be combined with the arithmetic operators; the left-hand side is evaluated only once (important for `arr[i] += 1` and `obj.field += 1`):

```lcui
let a = 10;
a += 5;    // 15
a -= 3;    // 12
a *= 2;    // 24
a /= 4;    // 6
a %= 4;    // 2

let arr = [1, 2, 3];
arr[1] += 10;   // arr = [1, 12, 3]
```

`++` / `--` increment / decrement and behave the same in prefix and postfix position, equivalent to `+= 1` / `-= 1`:

```lcui
let i = 0;
i++;      // i = 1
++i;      // i = 2
i--;      // i = 1
```

> [!WARNING]
> When `++` / `--` is used as an expression, it returns the **new, updated value** (prefix semantics), unlike C/C++ postfix `i++`, which returns the old value: `let i = 5; let j = i++;` gives `j == 6`, not `5`. Save the old value before incrementing if you need it.

> In older versions `++i` was parsed as two unary plus operators (leaving the value unchanged); introducing prefix increment changed that. It is a known breaking change, though the form is almost never written in real scripts.

### Null-Safe Operators

`??` (null coalescing) yields the right-hand side when the left side is `None` or an empty `optional`, otherwise the left side. `0`, `""`, and `false` do **not** trigger coalescing (this is unrelated to truthiness):

```lcui
let name = value ?? "default";
let count = 0 ?? 10;    // 0: 0 is not None, so no coalescing
```

`?.` (safe access) short-circuits the whole access chain and yields `None` when the left side is `None` or an empty `optional`; both field and index forms are supported:

```lcui
let len = maybeArr?.length ?? 0;
let item = obj?.data?.[0];
```

`??` binds tighter than assignment but looser than comparison, and cannot be mixed with `&&` / `||` without parentheses (that is an error, to avoid ambiguity). The result of `x?.y` may be `None`, so assigning it to a variable annotated `T` is an error; combine it with `??` or use `optional<T>`.

### Error Propagation with `?`

Appending `?` to an `optional` value means "take the value if present, otherwise return `None` from the current function early". It flattens a chain of null checks into one line; the scope is the **nearest function boundary** (inside a closure, it returns to the closure's caller):

```lcui
func head(a) -> optional<int> {
    if (a.length() == 0) [ return None; ]
    return a[0];
}

func firstPlus(a) -> optional<int> {
    return head(a)? + 1;      // if head(a) is empty, firstPlus returns None directly
}

firstPlus([]) ?? -1;          // -1
firstPlus([5]) ?? -1;         // 6
```

Both sides are checked at compile time: the expression decorated with `?` must be `optional<T>`, and the enclosing function's return type must also be `optional<...>`. If either condition fails, compilation raises an error, so an empty value can never silently continue as an ordinary value. `?` only handles `None`, not `false` / `0` / `""`.

### Comparison Operators

`==`, `!=`, `>`, `<`, `>=`, `<=` can be used for numeric, string, and boolean comparisons; numeric `int`/`float` values can be compared directly. String comparison rules roughly follow C++. `==`/`!=` on mismatched types raises an error (e.g. `1 == "1"`).

Objects, arrays, and functions support `==`/`!=`, but comparison is by reference:

```lcui
let a = [1];
let b = [1];
a == b;   // false: same contents but not the same array
let c = a;
a == c;   // true
```

`None == None` is `true`.

### Operator Overloading

Script classes can overload operators by implementing the built-in operator `trait`s, letting custom objects support `+`, `-`, `==`, unary minus, and so on (Rust style: each operator maps to a trait with an `op_xxx` method). The built-in operator traits are:

| Operator | Trait | Method | Parameters |
| --- | --- | --- | --- |
| `+` | `Add` | `op_add(rhs)` | 1 |
| `-` | `Sub` | `op_sub(rhs)` | 1 |
| `*` | `Mul` | `op_mul(rhs)` | 1 |
| `/` | `Div` | `op_div(rhs)` | 1 |
| `%` | `Mod` | `op_mod(rhs)` | 1 |
| `^` | `Pow` | `op_pow(rhs)` | 1 |
| `-` (unary) | `Neg` | `op_neg()` | 0 |
| `==` | `Eq` | `op_eq(rhs)` | 1 |
| `!=` | `Ne` | `op_ne(rhs)` | 1 |
| `<` | `Lt` | `op_lt(rhs)` | 1 |
| `<=` | `Le` | `op_le(rhs)` | 1 |
| `>` | `Gt` | `op_gt(rhs)` | 1 |
| `>=` | `Ge` | `op_ge(rhs)` | 1 |

Implement the trait for the class with `impl` and provide the `op_xxx` method; operator expressions then call it:

```lcui
class Vec2 {
    public:
    x = 0;
    y = 0;
}

impl Add for Vec2 {
    func op_add(rhs: Vec2) -> Vec2 {
        let r = new Vec2();
        r.x = this.x + rhs.x;
        r.y = this.y + rhs.y;
        return r;
    }
}

let a = new Vec2();
a.x = 3;
let b = new Vec2();
b.x = 4;
let c = a + b;      // calls Vec2.op_add, c.x == 7
```

Comparison operators return `bool` and can be used for equality tests:

```lcui
impl Eq for Money {
    func op_eq(rhs: Money) -> bool {
        return this.cents == rhs.cents;
    }
}

a == b;   // calls Money.op_eq
```

#### Operator Overloading Rules

- A binary operator uses the **left-hand operand** as the receiver (`this`) and calls the `op_xxx` implemented by the left class; overloading only triggers when the left side is a script class implementing the corresponding trait.
- `Neg` (unary minus) takes no parameters and uses the operand as the receiver.
- The **return type** of the overload is the result type of the operator expression and participates in type inference and later calls.
- With inheritance, the corresponding `op_xxx` is looked up along the base-class chain.
- Classes that do not implement the relevant trait keep the built-in behavior; native operations on `int`, `float`, `string`, and other primitives (including native `+` concatenation for `String`) are unaffected.

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
let result = if (count > 0) [ 10 : 20 ];
let total = if (flag) [ 1 : 2 ] + 5;
```

### while

```lcui
let i = 0;
while (i < 5) [
    i++;
]
```

### for

```lcui
let s = 0;
for (let i = 0; i < 10; i++) [
    if (i % 2 == 0) [ continue; ];
    s += i;
]
```

`for (;;)` allows all three clauses to be omitted, and is used together with `break`.

### for-in Loops

`for-in` iterates over an iterable and can yield both the index and the element:

```lcui
let items = ["a", "b", "c"];
for (item in items) [
    println(item);
]

for (i, item in items) [
    println(i + ": " + item);
]

for (c in "abc") [
    println(c);
]

let m = new Map();
m.set("a", 1);
for (k in m) [
    println(k + " = " + m.get(k));
]
```

Whether a value is iterable is decided by the **iteration protocol**, not by per-type special cases in the compiler:

| Iterable | Element | Index |
| --- | --- | --- |
| Array | Element value | Position (`int`) |
| String | One character, of type `string` | Position (`int`) |
| A class providing `length()` and `element(int)` | The return value of `element(i)` | Position (`int`) |
| A class providing `next()` returning `optional` (lazy iterator) | The inner type of `next()` | Round counter (`int`) |
| A `a..b` range | The `int` values in the range | Position (`int`) |

`Map` provides this protocol natively; `for (k in m)` iterates its keys (`m.keys` remains available). Any class — host-registered or script-defined — becomes iterable by providing `length()` and `element(int)`, with no compiler changes:

```lcui
class Squares {
    public:
    limit = 0;
    func length() -> int { return this.limit; }
    func element(i: int) -> int { return i * i; }
}

let s = new Squares();
s.limit = 4;
for (v in s) [
    println(v);            // 0 1 4 9
]
```

Protocol methods are looked up along the inheritance chain, so subclasses created with `extends` automatically inherit iterability. The element variable's type comes from the return type of `element(int)`, so a type mismatch is a compile-time error.

Besides the "known-length" indexing protocol, a class can implement a **lazy iterator**: provide a zero-argument `next()` method returning `optional<T>`. It is called once per round, and returning `None` ends the loop:

```lcui
class Nats {
    public:
    i = 0;
    func next() -> optional<int> {
        this.i = this.i + 1;
        return this.i;
    }
}

let total = 0;
for (v in new Nats()) [          // infinite sequence
    if (v > 5) [ break; ]        // break is the usual companion of lazy iterators
    total += v;
]
```

- The lazy protocol **pulls on demand**: it does not know the element count in advance and stops when `next()` returns `None`, so it can express infinite sequences, paged data, and other streams of unknown length.
- The element variable's type is the inner type `T` of `optional<T>` returned by `next()`; in `for (i, v in ...)`, `i` is the round counter (starting at 0), not internal state of the target object.
- When a class provides both protocols (`length()/element(int)` and `next()`), the indexing protocol wins.
- `next()` is also looked up along the inheritance chain. `continue` proceeds normally to the next pull.

- `for (item in arr)` iterates elements; `for (i, item in arr)` gives the index in `i` and the element in `item`.
- Strings are iterated by Unicode character, not by byte: `"你好".length()` returns `2`, and `for (c in "你好")` also runs twice. `indexOf` returns a character index, and the standard library `substr(s, start, count)` slices by character, consistent with `length()`.
- `continue` in the loop body does not skip the step; the length is re-read on every round, so elements appended during iteration are also visited.
- The element variable is an independent copy on each round: a lambda created inside the iteration captures that round's value.
- Ranges are a syntax-level construct and do not allocate an intermediate sequence when iterated.
- `in` is recognized as a keyword only after `for (`; elsewhere it remains an ordinary identifier.

### Ranges

`a..b` creates an integer range (only `int`), used with for-in. The range is half-open `[a, b)`, and both endpoints are evaluated once before the loop starts:

```lcui
for (i in 0..5) [
    println(i);      // 0, 1, 2, 3, 4
]

for (i in 5..0) [    // descending: 5, 4, 3, 2, 1
    println(i);
]
```

`5..5` is an empty range (the body never runs).

### break / continue

`break` exits the current loop, and `continue` skips the remainder of the current iteration; both can only appear inside a loop, and with nested loops they only affect the innermost one.

### panic (Abort)

`panic` immediately aborts execution of the entire script and carries an error message. It is a statement, not a function call, and the message is optional:

```lcui
let config = GUIManager::value("shop.items");
if (config.length() == 0) [
    panic("shop config is empty");     // abort: subsequent statements do not run
]

panic;                                 // also valid without a message
```

- `panic "msg";`, `panic("msg");`, and `panic;` are all valid; the message is an arbitrary expression evaluated at runtime.
- The abort behaves like a runtime error: the message carries a script-level location and the VM unwinds its stack frames; no later statement in the same run (including the rest of an outer function) executes.
- It suits "missing configuration / unsatisfied precondition, continuing is meaningless" cases; use `if` / `match` for recoverable branches.

### match (Pattern Matching)

`match (expression)` dispatches on the scrutinee by arm pattern, following the semantics of Rust's `match`. An arm is written as `pattern [ body ]`; the first matching arm wins and the value of the last expression in its body becomes the result:

```lcui
let name = match (code) [
    0 [ "zero" ]
    1 [ "one" ]
    n [ "many: " + n ]
];
```

| Pattern | Example | Notes |
| --- | --- | --- |
| Literal | `1`, `-2`, `3.5`, `"text"`, `true`, `None` | Matches by equality; `None` matches an empty `optional`; `int` / `float` literals match interchangeably |
| Type | `Circle c` | Dispatches via `instanceof`; `c` binds the matched object inside the arm |
| Enum variant | `Some(x)`, `Nothing()` | Matches the tag and binds payloads to the parameters; the variant must belong to the scrutinee's enum type |
| Bind | `n` | Matches any value and makes `n` available inside the arm |
| Wildcard | `_` | Matches any value without binding |
| Guard | `n if (n > 10)` | Extra test applied after the pattern matches; may reference the arm's binder |

```lcui
match (shape) [
    Circle c [ println("circle r=" + c.r); ]
    Square s if (s.s > 5) [ println("big square"); ]
    Square s [ println("square"); ]
    _ [ println("unknown"); ]
]
```

- `match` is an expression: it can initialize a `let`, take part in arithmetic, or feed a `return`; an arm body may also `return` early from the enclosing function.
- Guards are evaluated only after the pattern matches, so `Square s if (...)` never tests a non-`Square` value.
- Exhaustiveness is mandatory: an unguarded binding or `_` arm must exist (a `bool` scrutinee may instead be covered by both a `true` and a `false` arm), otherwise compilation fails.
- Binders are visible only inside their own arm and may shadow outer variables; type-pattern class names are checked at compile time.

### if let / while let

When only one enum variant needs handling, a `let` pattern can be written directly in an `if` / `while` condition:

```lcui
let v = Some(7);

if (let Some(x) = v) [
    println("got " + x);
]

while (let Some(x) = next()) [
    total = total + x;
]
```

`if let` binds the payload and runs its body when the tag matches, otherwise it skips the body; `while let` re-evaluates the right-hand side on every iteration and stops at the first mismatch. Binders are visible only inside the corresponding branch and never leak to the outer scope. Variant names and payload arity are checked at compile time.

## Standard Library (std)

The 13 `.lcui` modules under `assets/common/std/` form the standard library: everything that can be written in script is, leaving only VM value primitives, transcendental functions, and ll/ui classes in native code. An entry script imports relative to its own directory:

```lcui
import "std/iter.lcui";        // the whole module
import "std/array.lcui" { arrSum, arrZip };   // only the listed definitions
```

> Host scripts loaded from `assets/common/gui/` and similar directories use paths such as `import "../std/iter.lcui";`.

| Module | Contents |
| --- | --- |
| `iter.lcui` | Lazy iterators. `Fn1` / `Fn2` are single-method interfaces (`call`); lambdas and subclasses both work as mappers / predicates. Constructors: `range(a, b)` (half-open), `repeat(v)`. `Iter` combinators: `map`, `filter`, `take`, `takeWhile`, `skip`, `zip`, `chain`, `stepBy`, `enumerate`, `chunk`, `dedup`, `cycle`, `inspect`; terminals: `collect`, `fold`, `sum`, `count`, `forEach`, `any`, `all`, `find`, `position`, `last`, `max`, `min`. `next()` returns `optional`, so iterators also work directly with `for-in`. |
| `array.lcui` | Eager array functions: `arrMap`, `arrFilter`, `arrReverse`, `arrSum`, `arrProduct`, `arrAny`, `arrAll`, `arrFind`, `arrFindIndex`, `arrFirst`, `arrLast`, `arrDedup`, `arrZip`, `arrChunk`, `arrFlatten`, `arrFlatMap`, `arrTake`, `arrSkip`, `arrConcat`, `arrCount`, `arrReduce`, `arrMin`, `arrMax`, `arrMean`, `arrSortBy`, `arrGroupBy`, `arrPartition`, `arrWindow`, `arrUnion`, `arrIntersect`, `arrDifference`, `arrFill`, `arrRange`, `arrEqual`, `arrIsEmpty`, `arrIndexOfValue`, `arrRotate`, `arrShuffle`, `arrInterleave`, `arrIntersperse`, `arrUniqueBy`, `arrZipWith`, `arrScan`, `arrRemoveAt`, `arrInsertAt`, `arrSwap`, `arrFrequencies`, `arrMode`, `arrVariance`, `arrStdDev`, `arrCartesian`, `arrSymmetricDifference`, `arrSorted`, `arrCompact`, `arrPad`; the same file also provides `mapMapValues`, `mapMapKeys`, `mapFilter`, `mapFromArray`, `mapKeysWhere`, `mapForEach`, `mapUpdate`, and `setMap`, `setFilter`, `setCount`. |
| `string.lcui` | Queries: `length`, `charAt`, `slice`, `substr`, `startsWith`, `endsWith`, `startsWithAt`, `contains`, `indexOf`, `lastIndexOf`, `count`, `isEmpty`, `isBlank`, `isSpace`, `isUpper`, `isLower`, `isDigit`, `isAlpha`, `isAlphaNum`, `isAlnum`, `isLowerChar`, `wordCount`; transforms: `upper`, `lower`, `capitalize`, `swapCase`, `titleCase`, `trim`, `trimStart`, `trimEnd`, `reverse`, `reverseWords`, `snakeCase`, `kebabCase`, `normalizeSpaces`, `expandTabs`, `rotateStr`, `center`, `padLeft`, `padRight`, `truncate`, `truncateMiddle`, `mask`, `stripPrefix`, `stripSuffix`, `removeAt`, `insert`, `replace`, `repeat`, `repeatStr`; splitting and comparison: `split`, `join`, `words`, `lines`, `initials`, `toCharArray`, `fromChars`, `commonPrefix`, `commonSuffix`, `equalsIgnoreCase`, `compare`, `levenshtein`, `hamming`, `isPalindrome`, `isTitle`. |
| `math.lcui` | General math: `abs`, `min`, `max`, `clamp`, `lerp`, `sign`, `pi`, `tau`, `euler`, `sqrt`, `hypot`, `dist`, `trunc`, `floor`, `ceil`, `round`, `roundTo`, `roundHalfUp`, `mod`, `divmod`, `wrap`, `gcd`, `lcm`, `factorial`, `fib`, `isPrime`, `isEven`, `isOdd`, `isInteger`, `isPowerOfTwo`, `nextPow2`, `log10`, `log2`, `logn`, `exp`, `degToRad`, `radToDeg`, `mapRange`, `clamp01`, `approxEq`, `sum`, `product`, `mean`, `geometricMean`, `minOf`, `maxOf`, `median`, `lerpAngle`, `clampMagnitude`, `smoothstep`, `normalizeAngle`; transcendental functions remain in the native `math::` namespace. |
| `num.lcui` | Integer utilities (independent of `iter` / `string`): basics `numClamp`, `numSign`, `numAbs`, `numMin`, `numMax`, `numIntDiv`, `numPow`, `numLerp`, `numIsBetween`; number theory `numGcd`, `numGcd3`, `numLcm`, `numIsCoprime`, `numIsPrime`, `numPrimesUpTo`, `numFactorize`, `numDivisors`, `numCountDivisors`, `numSumOfDivisors`, `numSumOfProperDivisors`, `numTotient`, `numNextPrime`, `numPrevPrime`, `numIsPerfect`, `numIsAbundant`, `numIsDeficient`, `numIsEmirp`, `numIsMersenne`, `numIsHappy`; digit properties `numIsEven`, `numIsOdd`, `numIsPowerOfTwo`, `numGreatestPowerOfTwoLe`, `numIsSquare`, `numIsTriangular`, `numTriangular`, `numIsArmstrong`, `numIsHarshad`, `numIsPalindromeNum`, `numDigitCount`, `numDigits`, `numSumDigits`, `numReverse`, `numDigitalRoot`; sequences and radix conversion `numFactorial`, `numFib`, `numSumRange`, `numProductRange`, `numToBin`, `numToHex`, `numToRoman`, `numFromDigits`, `numManhattan`. |
| `map.lcui` | Helpers for `Map` and the "map as set" convention: `mapNew`, `mapSet`, `mapGet`, `mapGetOr`, `mapHas`, `mapSize`, `mapIsEmpty`, `mapRemove`, `mapKeys`, `mapValues`, `mapEntries`, `mapMerge`, `mapFromEntries`, `mapInvert`, `mapToArray`, `mapHasAny`, `mapHasAll`, `mapPick`, `mapOmit`, `mapEquals`, `mapIsSubmap`; set core: `setNew`, `setAdd`, `setHas`, `setRemove`, `setSize`, `setToArray`, `setFromArray`, `setUnion`, `setIntersect`, `setDifference`. |
| `set.lcui` | Set relations and transforms: `setIsSubset`, `setIsSuperset`, `setContainsAll`, `setIsDisjoint`, `setEquals`, `setSymmetricDifference`, `setToggle`, `setClear`, `setIsEmpty`, `setCopy`, `setMin`, `setMax`, `setPop`. |
| `json.lcui` | Read-only JSON parsing and escaping: `jsonParse(text)` turns objects into `Map`, arrays into arrays, numbers into `int` / `float`, and booleans into `bool`; JSON `null` becomes an empty string (the language cannot put an empty `optional` inside a native container). `jsonEscape(s)` escapes string content and `jsonQuote(s)` returns a quoted JSON string. |
| `random.lcui` | Reproducible 16-bit LCG (period 65536) whose state is carried explicitly by `Rng(seed)`: `nextU16()`, `nextUnit()`, `randInt`, `randFloat`, `randBool`, `randSign`, `randChoice`, `randShuffle`, `randSample`, `randWeighted`, `randDice`, `randInts`, `randGaussian`, `randId`. It never touches the native RNG, so the same seed produces identical results across builds. |
| `time.lcui` | Pure functions over integer seconds: `secondsToHms`, `formatClock`, `formatClockShort`, `formatDuration`, `formatDurationShort`, `formatCountdown`, `parseClock`, `parseDuration`, `dateParts`, `formatDate`, `formatDateTime`, `weekday`, `startOfDay`, `addDays`, plus calendar helpers `isLeapYear`, `daysInYear`, `daysInMonth`. |
| `vec.lcui` | 2D vectors represented as ordinary `[x, y]` arrays: `vAdd`, `vSub`, `vScale`, `vNeg`, `vPerp`, `vDot`, `vDotSelf`, `vLen`, `vDist`, `vNorm`, `vCross`, `vLerp`, `vMidpoint`, `vDistanceSq`, `vManhattan`, `vComponentSum`, `vIsZero`, `vEq`, `vAbs`, `vProject`, `vReflect`, `vClampLen`, `vResize`, `vFromAngle`, `vScaleAdd`. |
| `result.lcui` | Recoverable errors built on a native enum: `enum Result { Ok(int) Err(string) }` with `resultIsOk`, `resultIsErr`, `resultUnwrap`, `resultUnwrapOr`, `resultExpect`, `resultErrOr`, `resultMap`, `resultMapErr`, `resultAndThen`. The `Ok` payload is an `int` and the `Err` payload is a `string`; construct with `Ok(v)` / `Err(e)` and destructure with `match` or `if let`. |
| `proc.lcui` | Cooperative task wrappers: `spawn`, `yield`, `sleep`, `send`, `recv`, `cancel`, `join`, forwarding to the static methods of the native `Proc` class (see "Proc (Cooperative Tasks)" below). The `spawn` wrapper takes the function only; use a closure for arguments, or call `Proc::spawn(fn, ...)` directly. |

Example — a lazy pipeline:

```lcui
import "std/iter.lcui";

class Square extends Fn1 { func call(x) { return x * x; } }
class Big extends Fn1 { func call(x) { return x > 4; } }

let out = range(1, 10).map(new Square()).filter(new Big()).collect().join(",");
// "25,36,49,64,81"
```

Lambdas work directly, without defining an `Fn1` subclass first:

```lcui
let squares = range(1, 6).map(func (x) { return x * x; }).collect();
```

Example — `Result`:

```lcui
import "std/result.lcui";

func parsePositive(text) -> Result {
    let value = text.toInt();
    if (value <= 0) [ return Err("not positive"); ]
    return Ok(value);
}

match (parsePositive("7")) [
    Ok(v) [ println("ok: " + v); ]
    Err(e) [ println("err: " + e); ]
]
```

Example — `Proc`:

```lcui
import "std/proc.lcui";

let worker = spawn(func () {
    let msg = recv();
    println("worker got " + msg);
});

send(worker, "hello");
join(worker);
```

> **Optional value semantics**: `let v = it.next();` stores the raw value or `None` in the variable (a dynamic target does not unwrap automatically), so `if (v)`, `v.has_value`, `v.value`, and `??`/`?.` all behave consistently; only a target annotated `T` unwraps immediately on assignment.

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

let result = add(1, 2);
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
let double = func (x: int) -> int {
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

let dog = new Animal("dog");
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

let box = new SecretBox();
box.getSecret();    // 42
box.secret;         // Error: private field
```

Private members are not inherited; a subclass cannot access the base class's private members via `this` or bare names.

### Static Members

`static` declares a static field or static method, accessed via `ClassName::member` (instance members use `.`, static members use `::` to distinguish):

```lcui
class Counter {
public:
    static total = 0;

    static func inc() -> int {
        total += 1;
        return total;
    }
}

Counter::inc();  // 1
Counter::total;  // 1
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

let dog = new Dog("dog");
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

### Traits and Impls

A `trait` declares a set of method contracts, and `impl` supplies the implementation for a class, so a capability can be attached to a class without going through inheritance:

```lcui
trait Printable {
    func describe() -> string;
}

class Card {
    // ...
}

impl Printable for Card {
    func describe() -> string {
        return "card";
    }
}
```

- `impl Trait for Class` implements a trait for a class; every method declared by the trait must be implemented. An unknown trait, an `impl` target that is not a class, or a missing required method raises an error.
- `impl Class` is an inherent implementation (not bound to a trait) and also merges its methods into the target class.
- An `impl` block may contain `static` methods and `const` associated constants:

    ```lcui
    impl Card {
        const Kind = "card";

        static func create() -> Card {
            return new Card();
        }
    }
    ```

- The methods and constants of an `impl` are **merged into the target class**; they are called exactly like the class's own methods (`card.describe()`), with no extra dynamic dispatch.
- Colliding with a method of the same name and arity already defined in the class, or a duplicate associated constant, raises an error.
- Generic `impl` blocks support the `impl<T>` form. Type parameters are split into two positional groups:
    - The first `min(impl parameter count, class parameter count)` parameters **map positionally to the class type parameters**; the names need not match — `U` in `impl<U> ... for Box<T>` is rewritten to `T` during merging, and `U` in method signatures is the same variable as `T` in the class vocabulary.
    - Extra parameters (including `impl<T>` on a non-generic class) are injected into every merged method as **method-level generic variables**, expressing placeholders related to the method but not to the class.
    - Omitting the parameter list (`impl Box`) is also legal (`<T>` is optional).
- Blanket impls (`impl<T> Trait for T`, implementing a trait for every type) are **not supported** and raise an error.
- A typical use is the iteration protocol: giving a class `length()` and `element(int)` (either in the class body or in an `impl`) makes it usable with `for-in`; see "for-in Loops".

#### Example: Making a Custom Class Iterable

```lcui
trait Iterable {
    func length() -> int;
    func element(i: int) -> string;
}

class Deck {
    cards = [];
}

impl Iterable for Deck {
    func length() -> int {
        return this.cards.length();
    }

    func element(i: int) -> string {
        return this.cards[i];
    }
}

let deck = new Deck();
for (i, card in deck) [
    println(i + ": " + card);
]
```

`for-in` only checks whether the target provides `length()` and `element(int)`, no matter whether they are declared in the class body or in an `impl` block, so `Deck` becomes iterable without extending any base class.

#### Common Errors

| Error | Reason |
| --- | --- |
| `Cannot implement unknown trait 'X'` | The trait is not defined, or is defined after the `impl` |
| `impl of trait 'X' for 'Y' is missing required method 'Z'` | Not every method required by the trait is implemented |
| `Method 'Z' is already defined in class 'Y'` | Clashes with a method of the same name and arity in the class |
| `impl target 'Y' is not a class` | The `impl` target is not a class |

> [!TIP]
> `impl` merges its methods and constants into the target class during semantic analysis, so there is no trait object and no dynamic dispatch: the capability is attached at compile time and calls cost exactly the same as the class's own methods.
> Use `extends` inheritance when you need runtime polymorphism, and `impl` when you need to attach a capability to an existing class.

## Built-in Namespaces and Macros

### Built-in Namespaces

| Namespace | Functions | Description |
| --- | --- | --- |
| `math` | `sqrt`, `pow`, `log`, `sin`, `cos`, `random(min, max)` | Native transcendental functions; scriptable `abs` / `min` / `max` live in the `std/math.lcui` standard library |
| `std` | `format(formatStr, array)` | Formats a string with the values in an array (fmt style) |
| `mc` | `runCmd(command)` | Executes a command as the current player |
| `GUIManager` | `value(id)`, `request(id, args)`, `callback(id, args)`, `open(id, formId, type[, ctx])`, `switchTo(id, type)` | Interacts with GUI data |
| `storage` | `get(key)`, `set(key, value)`, `has(key)`, `remove(key)`, `keys()`, `clear()` | Persistent storage isolated per script |
| `playerdata` | `get(key)`, `set(key, value)`, `has(key)`, `remove(key)`, `keys()`, `clear()` | Persistent storage isolated per player and script |
| `Proc` | `spawn(fn[, ...])`, `yield()`, `sleep(ms)`, `send(taskId, message)`, `recv()`, `cancel(taskId)`, `join(taskId)` | Cooperative task scheduling (native static class; see "Native Classes") |

The call format is `Namespace::function(parameters)`, for example:

```lcui
mc::runCmd("say hello");
GUIManager::callback("example.submit", [ "data" ]);
let text = std::format({tr("example.gui.info")}, [ 1, "two" ]);
```

> The `type` in `GUIManager::open` and `GUIManager::switchTo` is an integer: `1` is CustomForm, `2` is MessageBox, `3` is PaginatedForm, `4` is ScriptForm. Built-in form scripts import `FORM_CUSTOM`, `FORM_MESSAGE`, `FORM_PAGINATED`, and `FORM_SCRIPT` from `generic.lcui` instead of using raw numbers.

> [!NOTE]
> Early docs mentioned `string::length` / `string::upper` namespace functions; they do not exist. Strings and arrays are value types and use member methods (see "String Methods" and "Array Methods"). Scope, paths, and limits of `storage` / `playerdata` are documented in [Data Files](./data.md#script-storage).

> [!NOTE]
> When `GUIManager::open` navigates to **another script**, the target script id must be declared in `scripts.<id>.gui.navigations` in `gui/permission.json`, otherwise the call is denied. Opening a script's own form needs no declaration.

### Macros `{...}`

Call a macro with `{name}` or `{name(parameters)}`, for example `{tr("language.gui.title")}`. `tr` returns the translated text for the current player's language; default variables such as `{player}`, `{server_tps}`, and `{score(name)}` are also used in macro form; see [LOICollectionAPI](./api.md) for the full list.

```lcui
let title = {tr("example.gui.title")};
let label = {player} + " -> " + {tr("example.gui.hello")};
```

### Passthrough and Blocking with `$`

`$` is used to pass through raw text: the content between `$` and `}` (or `;`) is kept as-is and does not participate in parsing:

```lcui
let raw = $custom content};
```

The `${variable}` form can block variable/macro replacement, keeping the content in literal form; for example, `${team}` in `{player_realname} + '->' + ${team}` is output verbatim as `{team}`.

## Native Classes

The following native classes are directly usable in scripts (overview; all except `Proc` are created with `new`):

| Class | Purpose | Detailed docs |
| --- | --- | --- |
| `GlobalValue` | Global value container, field `value` | [api.md](./api.md) |
| `CtxValue` | Reads the `ctx` array passed to `GUIManager::open`, field `value` | See below |
| `Map` | Insertion-ordered key-value container | See "Map" above |
| `Proc` | Cooperative task scheduling: `spawn` / `yield` / `sleep` / `send` / `recv` / `cancel` / `join` | See below |
| `ObservableString` / `ObservableNumber` / `ObservableBoolean` / `ObservableUIRawMessage` | Observable data for two-way binding of controls | [native-ui.md](./native-ui.md) |
| `UIRawMessage` | Constructs rich text / translated text | [native-ui.md](./native-ui.md) |
| `CustomForm` / `MessageBox` / `PaginatedForm` | Native forms | [native-ui.md](./native-ui.md) |
| `TextOptions` / `ButtonOptions` / `TextFieldOptions` / `DropdownOptions` / `DropdownItem` / `SliderOptions` / `ToggleOptions` / `DividerOptions` / `SpacingOptions` | Control options | [native-ui.md](./native-ui.md) |
| `MenuData` / `MenuItemData` / `MenuControlData` / `MenuForm` / `MenuMessageBox` | Menu data and forms | [api.md](./api.md) |
| `ShopData` / `ShopItemData` / `ShopForm` | Shop data and forms | [api.md](./api.md) |
| `ScoreRequirement` | Score requirement, fields `objective`, `value` | [api.md](./api.md) |

### Proc (Cooperative Tasks)

`Proc` is a native static class and is not instantiated with `new`. Every `Proc::spawn` creates an **isolated task** with its own VM, globals table, and execution budget; tasks can only communicate through mailboxes (`send` / `recv`). The scheduler time-slices tasks automatically, while `yield` and `sleep` voluntarily give up the remainder of a slice. `std/proc.lcui` forwards these methods through top-level functions.

| Static method | Description |
| --- | --- |
| `Proc::spawn(fn[, ...args])` | Creates a task and returns its task ID (`int`); the number of `args` must match the parameters of `fn` |
| `Proc::yield()` | Gives up the rest of the current time slice; only valid inside a task |
| `Proc::sleep(ms)` | Sleeps for the given number of milliseconds; only valid inside a task |
| `Proc::send(taskId, message)` | Posts a message to the target task; silently ignored when the task does not exist |
| `Proc::recv()` | Takes one message from the current task's mailbox; blocks while empty; only valid inside a task |
| `Proc::cancel(taskId)` | Requests cancellation; an unknown or finished task raises no error |
| `Proc::join(taskId)` | Waits for the target task to finish. Inside a task it only waits and returns an empty value; from the host it pumps the scheduler and returns the task result, or an error string when the task does not exist |

```lcui
let worker = Proc::spawn(func (name) {
    let msg = Proc::recv();
    println(name + " <- " + msg);
}, "worker");

Proc::send(worker, "ping");
Proc::join(worker);
```

> [!WARNING]
> Calling `yield` / `sleep` / `recv` from an ordinary script (not a `spawn` task) raises an error. `join` inside a task does not bring back the target task's result; pass results through `send` / `recv` instead.

### CtxValue

When a script is opened via `GUIManager::open(id, formId, type, ctx)` and a `ctx` array is passed in, `new CtxValue(index)` can be used to read the corresponding element:

```lcui
let detail = new CtxValue(0);
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

let items = makeItems();
let selected = new GlobalValue();
selected.value = "";

let form = new CustomForm("example.shop", {tr("example.shop.title")}) {
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

## Script permission: trust level and capabilities

The script sandbox authorizes sensitive calls through a **single gate**; the full field reference is in the `permission.json` section of [Data Files](./data.md). Each script first receives a set of **capabilities** from its **trust level** (coarse gate), then is matched against the per-script id allowlist under `scripts.<id>` (fine gate); both must pass.

- Trust levels are defined in `trustLevels` as capability-name arrays; `defaultTrust` is the fallback and a single script may override it with `trust`.
- Built-in levels today: `user` (only `GuiRead` / `GuiWrite`), and `admin` / `builtin` (also `ServerCommand` plus player-data / storage read-write).
- The old `Network` / `Filesystem` capabilities were never granted to any script and have been removed from the enum.

> [!NOTE]
> The coarse gate keeps scripts without a declared trust level to GUI only: even under `defaultPolicy: "allow"` they cannot run commands or read/write player data — least privilege by default.

## Execution Budget and Call Depth

To keep runaway scripts from hanging the server, the VM meters **fuel** on every script execution (including script re-entry such as form callbacks): each instruction costs 1 point, and wall clock, native calls, objects and memory each have their own quota. Exhausting any one of them aborts execution immediately with an error.

| Quota | Default | Allowed range | Behavior on breach |
| --- | --- | --- | --- |
| Instructions | 1,000,000 | 10,000 ~ 100,000,000 | Execution aborts with `Execution budget exhausted` |
| Wall clock | 1,000 ms | 10 ~ 30,000 ms | Execution aborts with `Execution timeout` |
| Call depth | 256 frames | 16 ~ 4,096 | Execution aborts with `Call stack depth limit exceeded` |
| Native calls | 100,000 | 1,000 ~ 10,000,000 | Execution aborts with `Execution budget exhausted` |
| Objects | 100,000 | 1,000 ~ 10,000,000 | same as above |
| Array elements | 100,000 | 1,000 ~ 10,000,000 | same as above |
| Single string | 1 MiB | 4 KiB ~ 256 MiB | Execution aborts with `String size budget exhausted` |
| Cumulative memory | 64 MiB | 1 MiB ~ 1 GiB | Execution aborts with `Total byte budget exhausted` |

Ordinary GUI scripts stay far below these caps; a loop that forgot its step (e.g. `while (true)`) or an unbounded recursion is aborted in time instead of dragging the server down.

### Tuning the budget per script

The defaults live in code and can be overridden per script in `gui/permission.json` — the root-level `budget` sets the global default, `scripts.<id>.budget` applies to that one script only, and any field left out falls back to the level above:

```json
{
    "defaultPolicy": "deny",
    "budget": { "maxInstructions": 250000, "maxWallTimeMs": 750 },
    "scripts": {
        "market": {
            "enabled": true,
            "budget": { "maxInstructions": 4000000 },
            "gui": { "values": [], "requests": [], "callbacks": [] }
        }
    }
}
```

- The keys are `maxInstructions`, `maxWallTimeMs`, `maxFrames`, `maxNativeCalls`, `maxObjectCount`, `maxArrayElements`, `maxStringBytes`, `maxTotalBytes`; only `maxWallTimeMs` carries the `Ms` suffix (milliseconds), everything else is a plain count.
- Values outside the allowed range are **clamped to the bound**: `0` does not switch the sandbox off, and `99999999` ms does not become 27 hours.
- Non-numeric or negative values (e.g. `"1000"`, `-5`) are ignored and fall back to the level above.
- An unknown script id uses the root-level `budget`.

> [!TIP]
> Prefer raising the budget of a **single** script over raising the global default — the point of a quota is to contain the script that runs away, and relaxing it globally loosens every script at once.

## Bytecode Optimization

After compilation and before execution, the bytecode passes through an optimizer. Optimization only changes *how* something is computed, never *what* is computed — runtime errors still fire exactly as written, with their original source locations intact. Everything below is transparent to script authors, but knowing it helps write scripts that naturally benefit:

| Optimization | Example | Effect |
| --- | --- | --- |
| Constant folding | `1 + 2 * 3`, `math::sqrt(9)` | Computed at compile time; the runtime loads a constant |
| Strength reduction | `x * 1`, `x + 0` | For numeric types the original operand is used directly, removing an arithmetic instruction |
| Cross-block propagation | Both branches assign the same constant to `x`, then `x` is read at the join | Known values flow forward over the CFG; the join intersects them, turning provable reads into constants |
| Variable forwarding | `let x = 1; if (x == 1) [...]` | After a known scalar is stored, reads are replaced by the constant and the branch folds away |
| Pure-function folding | `math::sqrt(9)`, `math::pow(2, 10)` | Native pure functions with all-constant arguments evaluate at compile time; impure ones like `math::random` never fold |
| Dead-store elimination | `x = 1;` followed by no reads | Removes assignments that can never be observed |
| Dead-value elimination | A computed result that is never used | Removes pure computations whose results are unused |
| Common subexpression elimination | The same expression computed in adjacent statements | Reuses the already-computed register |
| Loop-invariant code motion | A computation inside a loop that does not depend on the iteration | Hoisted out of the loop and computed once |
| Branch fusion | `CMP_*` immediately followed by `JMP_IF_FALSE` on its result | Fuses into one compare-and-branch instruction, saving a register read/write |
| Function inlining | A call to a small function | Expands the body and removes call overhead |
| Dead-code elimination | `while (false) [...]`, `if (false) [...]` | Unreachable branches are removed wholesale |

The optimizer iterates propagation, folding, dead-store/dead-value elimination, and CSE until the bytecode stops changing (a fixed point), so opportunities exposed by one rewrite are picked up by the next round; loop-invariant code motion and branch fusion run after convergence.

At runtime there is an additional layer of **inline caching**: when a script calls the same native function or native method repeatedly (e.g. `math::sqrt(...)` or `arr.push(...)` inside a loop), the VM remembers the previous resolution and skips signature matching and registry lookups on a hit. This affects speed only, never behavior — on invalidation (e.g. a plugin reload mutating the registry) the VM falls back to the full lookup.

## Bytecode Package

After compiling a script, `GUIManager::load` serializes the bytecode to a `<script>.lcp` file next to the source (e.g. `market.lcui` maps to `market.lcui.lcp`):

- Subsequent loads read the `.lcp` package first; on a hit, lexing/parsing/semantic analysis and compilation are skipped and the bytecode is reused directly.
- The package header records the script id and an **ABI fingerprint** — a hash over the shape of every registered native class, function and macro. A plugin upgrade that changes any native signature alters the fingerprint, which invalidates old packages and triggers recompilation.
- When the source is present, the package additionally records the SHA-256 of the entry file and of every file in the import graph; a change to any of them triggers recompilation.
- The serialized data carries a checksum; a corrupted package is discarded and recompiled.
- A package is self-contained: with the `.lcui` source removed, the script still loads as long as the ABI fingerprint matches. This is how closed-source scripts are distributed.
- Debug information lives in a separate `.lcp.dbg` file; when it is missing the bytecode still executes, only line/column information is lost.

The current package format version is v2.

## Common Constraints and Pitfalls

- Classes, named functions, `using`, and components can only appear at the top level of a script; they cannot be nested.
- Declarative UI blocks are only allowed on form classes (`CustomForm` / `MessageBox` / `PaginatedForm` / `ScriptForm`); the `on:` named argument is only available inside a block.
- Component calls may only appear as statements inside a declarative UI block; component recursion (direct or indirect) is a compile error.
- Imported files may only contain top-level definitions (`class` / `func` / `using` / `component` / `enum` / `import`); same-named definitions in different files conflict and raise an error.
- Every script execution is bound by the execution budget (1,000,000 instructions / 256 call frames by default); exceeding it aborts the execution. The budget is tunable per script via `budget` in `permission.json` — see [Execution Budget and Call Depth](#execution-budget-and-call-depth).
- `return` can only be used inside a function; `break`/`continue` can only be used inside a loop.
- A type declaration (`let x: int = 0;`) must also provide an initial value; class fields without a default value must be assigned in the constructor.
- `None` can only be used in an `optional` context; directly reading, performing arithmetic on, or comparing an empty `optional` raises an error.
- `%` only supports integers; division by zero and integer overflow raise errors.
- Array indices must be `int`; out-of-bounds reads and negative indices raise errors; an index equal to the length appends an element.
- Strings have no escape characters; use the other kind of quote to include a quote character.
- A function call supports at most 100 arguments; exceeding this fails to parse.
- The `GUIManager::value` / `request` / `callback` IDs registered by different modules differ; refer to the `.lcui` file of the corresponding module.

> For common errors of the script language and their solutions, see [Common Error Meanings](./errors.md).
