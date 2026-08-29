# LOICollectionAPI

> [!NOTE]
> The following content is taken from the `LOICollectionAPI` structure of LOICollectionA 1.15.0; the `LOICollectionAPI` structure may differ in later versions.

## Default Variables

| LOICollectionAPI | Description | Type |
| --- | --- | --- |
| {version_mc} | Current server Minecraft version | string |
| {version_ll} | Current server LeviLamina version | string |
| {version_protocol} | Current server protocol version | string |
| {player} | Current player name | string |
| {player_language} | Gets the language ID used by the player | string |
| {player_language_name} | Gets the name of the language used by the player | string |
| {player_title} | Gets the title currently equipped by the player | string |
| {player_title_time} | Gets the remaining time the player holds the title | string |
| {player_mute} | Whether the player is muted | boolean |
| {player_pvp} | Whether the player has PVP enabled | boolean |
| {player_statistcs_onlinetime} | Player online time | string |
| {player_statistcs_kills} | Number of mobs killed by the player | int |
| {player_statistcs_deaths} | Number of player deaths | int |
| {player_statistcs_place} | Number of blocks placed by the player | int |
| {player_statistcs_destroy} | Number of blocks destroyed by the player | int |
| {player_statistcs_respawn} | Number of player respawns | int |
| {player_statistcs_join} | Number of times the player joined the server | int |
| {player_gamemode} | Current game mode of the player | string |
| {player_pos} | Coordinates of the player | string |
| {player_pos_x} | X coordinate of the player | int |
| {player_pos_y} | Y coordinate of the player | int |
| {player_pos_z} | Z coordinate of the player | int |
| {player_pos_respawn} | Respawn coordinates of the player | string |
| {player_pos_respawn_x} | Respawn X coordinate of the player | int / string(None) |
| {player_pos_respawn_y} | Respawn Y coordinate of the player | int / string(None) |
| {player_pos_respawn_z} | Respawn Z coordinate of the player | int / string(None) |
| {player_pos_block} | Block coordinates of the player | string |
| {player_pos_lastdeath} | Coordinates of the player's last death | string |
| {player_realname} | Real name of the player | string |
| {player_xuid} | XUID string of the player | string |
| {player_uuid} | UUID string of the player | string |
| {player_is_op} | Whether the player is an OP | boolean |
| {player_can_fly} | Whether the player can fly | boolean |
| {player_health} | Current health of the player | int |
| {player_max_health} | Maximum health of the player | int |
| {player_hunger} | Current hunger value of the player | int / string(None) |
| {player_max_hunger} | Maximum hunger value of the player | int / string(None) |
| {player_saturation} | Current saturation of the player | int / string(None) |
| {player_max_saturation} | Maximum saturation of the player | int / string(None) |
| {player_speed} | Current speed of the player | float |
| {player_direction} | Current facing direction of the player | string |
| {player_dimension} | Current dimension ID of the player | int |
| {player_ip} | Connection IP of the player | string |
| {player_exp_xp} | Current experience of the player | int / string(None) |
| {player_exp_level} | Current level of the player | int / string(None) |
| {player_exp_level_next} | Experience required for the player's next level | int |
| {player_handitem} | Name of the item held by the player | string |
| {player_offhand} | Name of the item in the player's offhand | string |
| {player_os} | Device name of the player | string |
| {player_ms} | Network latency of the player (ms) | int |
| {player_ms_avg} | Average network latency of the player (ms) | int |
| {player_packet} | Network packet loss rate of the player (%) | int |
| {player_packet_avg} | Average network packet loss rate of the player (%) | int |
| {server_tps} | Gets the current server TPS | float |
| {server_mspt} | Gets the current server MSPT | float |
| {server_time} | Current time | string |
| {server_player_max} | Maximum number of players | int |
| {server_player_online} | Number of online players | int |
| {server_entity} | Number of entities on the current server | int |
| {score(ScoreboardName)} | Scoreboard score of the player | int |
| {tr(languageId)} | Gets the translation text of the specified ID in the player's current language | string |
| {tr(langcode, languageId)} | Gets the translation text of the specified ID in the specified language | string |
| {entity(typeid)} | Gets the number of entities with the specified ID | int |

> [!TIP]
> Due to how the parser works, any string passed should be wrapped with `"` or `'`.

---

## Blocking Identifier - $

> The blocking identifier is used to prevent variable replacement, i.e. the original variable content is kept as-is and is not parsed into other content.

```text
${variable}
```

| Parameter | Description |
| --- | --- |
| variable | Variable name |

> [!TIP]
> The blocking identifier is usually placed before a variable, and the identifier is removed after the replacement is complete.

### **Concrete Usage Example of the Blocking Identifier**  

The following shows the real name of the player without parsing other content

```text
{player_realname} + '->' + ${team}
```

- Returned result: `player -> {team}`

---

## Conditional Statement - if

> The conditional statement is used to check whether a condition holds. If it holds, the content of `result_yes` replaces the original statement; otherwise, the content of `result_no` replaces the original statement  

```text
if (condition)[result_yes : result_no]
```

| Parameter | Description |
| --- | --- |
| condition | Condition to check |
| result_yes | Content to replace with when the condition holds |
| result_no | Content to replace with when the condition does not hold |

> [!TIP]  
> Nested `if` statements are supported in conditional statements, but it is recommended not to nest too deeply, otherwise the statement becomes hard to read  

### **Concrete Usage Example of the Conditional Statement**  

The following shows the average network latency of the player in different colors

> [!WARNING]
> Note that the original `result` of the statement cannot parse special characters, so they need to be combined into a string type

```text
if({player_ms_avg} <= 50)["§a" : if( {player_ms_avg} > 50 && {player_ms_avg} <= 250)["§e" : "§c"]] + {player_ms_avg} + "§bms"
```

- **When the condition `{player_ms_avg} <= 50` is met, it returns "§a {player_ms_avg}§bms"**  
- **When the condition `{player_ms_avg} > 50 && {player_ms_avg} <= 250` is met, it returns "§e {player_ms_avg}§bms"**  
- **When none of the above conditions are met, it returns "§c {player_ms_avg}§bms"**

---

## Operators - operator

> Operators are tools used in raw statements for more convenient and faster processing, including but not limited to `+`, `-`, `*`, `/`, etc.

| Operator 1 | Description | Operator 2 | Description |
| --- | --- | --- | --- |
| + | Addition | - | Subtraction |
| * | Multiplication | / | Division |
| % | Modulo | ^ | Power |
| ! | Logical NOT | && | Logical AND |
| \|\| | Logical OR | == | Equal to |
| != | Not equal to | > | Greater than |
| < | Less than | >= | Greater than or equal to |
| <= | Less than or equal to | ... | ... |

### **Concrete Usage Example of Operators**  

Most of the above are self-explanatory and can be used directly, so they will not be elaborated further.  
Next, some `special usages` will be shown.

1. String concatenation

    ```text
    "Hello" + "World"
    ```

    - Returned result: "HelloWorld"

2. String comparison

    ```text
    "Hello" <= "World"
    ```

    - Returned result: false

    > [!NOTE]
    > The comparison rules here can roughly refer to the comparison rules of `C++`

---

## Function - function

> Functions are a collection of commonly used features that help us perform certain operations more conveniently.  
> Some built-in functions are provided below; more functions can be extended by yourself.

```text
namespaces::function_name(parameter1, parameter2,...)
```

| Parameter | Description |
| --- | --- |
| namespaces | Namespace, used to distinguish different features |
| function_name | Function name |
| parameter1 | Parameter 1 |
| parameter2 | Parameter 2 |
| ... | ... |

> [!WARNING]
> In a function call, the maximum number of parameters is `100`; exceeding that will cause parsing to fail. For details, see the [Too many args in function call](./errors.md#too_many_arguments_in_function_call) error

### **Concrete Usage Example of Functions**

The following shows calculating the maximum of `cos(sqrt(100))` and `sin(sqrt(100))`, and taking its absolute value.

> [!TIP]
> Note that function calls need to be separated with `::`, and function parameters need to be separated with `,`.

```text
math::abs(math::max(math::cos(math::sqrt(100)), math::sin(math::sqrt(100))))
```

- Returned result: 0.544021

> [!WARNING]
> Every expression must be followed by the separator `;`

---

## Script Language Features (.lcui)

Since 1.15.0, the UI data of Menu and Shop has been changed to `.lcui` script files (see [Data Files](./data.md)), and the built-in UIs of other modules are also written in the same scripting language. The script supports variables, arrays, strings, numbers, booleans, and `none`, as well as flow control such as `if`, `while`, `for`, `break`, `continue`, and `return`.

### Function Definition and Anonymous Functions

Use `func` to define named functions; parameter and return value types can be specified (dynamic when unspecified):

```lcui
func add(a: int, b: int) -> int {
    return a + b;
}

result = add(1, 2);
```

Anonymous functions (Lambdas) are defined with `func (parameters) -> return type { ... }`, and can be assigned to variables or passed directly as parameters:

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
> Did you know: the sky is blue...... although sometimes it isn't (￣y▽￣)╭  

### Classes and Inheritance

The scripting language supports classes, inheritance, access control, and static members:

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

Class-related keywords and operators:

| Keyword/Operator | Description |
| --- | --- |
| `class` | Defines a class |
| `extends` | Inherits a base class |
| `new` | Creates an object |
| `this` | Current object |
| `super` | Calls base class methods; `super(...)` calls the base class constructor |
| `instanceof` | Checks whether an object is an instance of the specified class |
| `public:` / `private:` | Access control section |
| `static` | Static member or static method |
| `using Alias = Type;` | Type alias |

### Built-in Namespaces and Functions

| Namespace | Function | Description |
| --- | --- | --- |
| `math` | `abs`, `max`, `min`, `sqrt`, `pow`, `log`, `sin`, `cos`, `random` | Math functions |
| `string` | `length`, `upper`, `lower`, `substr`, `trim`, `replace` | String functions |
| `std` | `format(formatStr, array)` | Formats a string using values from an array |
| `mc` | `runCmd(command)` | Executes a command as the current player |
| `GUIManager` | `value(id)` | Gets a GUI registered value |
| `GUIManager` | `request(id, args)` | Requests GUI registered data |
| `GUIManager` | `callback(id, args)` | Calls a GUI registered callback |
| `GUIManager` | `open(id, formId, type[, ctx])` | Opens a GUI of the specified type |
| `GUIManager` | `switchTo(id, type)` | Switches the current GUI to the specified type |

> [!TIP]
> The `type` in `GUIManager::open` and `GUIManager::switchTo` is an integer: `1` is CustomForm, `2` is MessageBox, `3` is PaginatedForm, and `4` is ScriptForm.

> [!NOTE]
> When `GUIManager::open` navigates to **another script**, the target script id must be declared in `scripts.<id>.gui.navigations` in `gui/permission.json`, otherwise the call is denied. Opening a script's own form needs no declaration.

### Common Built-in Classes

| Class | Description |
| --- | --- |
| `MenuData` | Menu data, containing `id`, `type`, `title`, `content`, `permission`, `exitCommand`, `scoreCommand`, `permissionCommand`, `items`, `controls`, `confirm`, `cancel`, `run`, `submit` |
| `MenuItemData` | Menu button data, containing `type`, `title`, `id`, `run`, `permission`, `scores` |
| `MenuForm` | Menu form, `new MenuForm(id, title)`; methods: `header`, `label`, `divider`, `spacer`, `textField`, `dropdown`, `toggle`, `slider`, `button`, `closeButton`, `show`; callback results are `closeReason`, `actionIndex`, `action` |
| `MenuMessageBox` | Menu message box, `new MenuMessageBox(id, title)`; methods: `body`, `button1`, `button2`, `show`; callback results are `closeReason`, `selection`, `action` |
| `ShopData` | Shop data, containing `id`, `type`, `title`, `content`, `exitCommand`, `scoreCommand`, `titleCommand`, `itemCommand`, `items` |
| `ShopItemData` | Shop item data, containing `type`, `title`, `introduce`, `number`, `id`, `nbt`, `confirmButton`, `cancelButton`, `time`, `scores` |
| `ShopForm` | Shop form, `new ShopForm(id, data)`; methods: `label`, `divider`, `previousButton`, `nextButton`, `chooseButton`, `closeButton`, `show`; callback results are `closeReason`, `resultCode`, `shop`, `itemIndex`, `item`, `fromId` |
| `ScoreRequirement` | Score requirement, containing `objective`, `value` |
| `PaginatedForm` | Paginated form, `new PaginatedForm(guiId, title, elements[, pageSize])`; methods: `previous`, `next`, `choose`, `previousButton`, `nextButton`, `chooseButton`, `button`, `closeButton`, `divider`, `dropdown`, `header`, `label`, `slider`, `spacer`, `textField`, `toggle`, `show`, `close`, `isShowing`; callback results are `closeReason`, `selection`, `selectionIndex`, `page` |
| `ObservableString` / `ObservableNumber` / `ObservableBoolean` | Observable data, constructor parameters are (initial value, whether the client can write); methods: `getData`, `setData`, `subscribe`, `unsubscribe`, `isClientWritable` |
| `TextOptions` / `ButtonOptions` / `TextFieldOptions` / `DropdownOptions` / `DropdownItem` / `SliderOptions` / `ToggleOptions` / `DividerOptions` / `SpacingOptions` | Option classes for form controls |
| `GlobalValue` | Global value container, containing a `value` field |

---

> For common errors and solutions of the scripting language, see [Common Error Meanings](./errors.md).
> For the complete class and method documentation of the native UI, see [Native UI](./native-ui.md).
