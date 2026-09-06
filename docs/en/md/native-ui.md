# Native UI

> [!NOTE]
> The following content is taken from the native UI structure of LOICollectionA 1.17.0 and may differ in later versions.

## Overview

Native UI is a collection of classes in the script language (`.lcui`) that directly wraps LeviLamina's native form and interface capabilities, including:

- Forms: `CustomForm`, `MessageBox`, `PaginatedForm`
- Observable data: `ObservableString`, `ObservableNumber`, `ObservableBoolean`, `ObservableUIRawMessage`
- Raw message: `UIRawMessage`
- Control options: `TextOptions`, `ButtonOptions`, `TextFieldOptions`, `DropdownOptions`, `DropdownItem`, `SliderOptions`, `ToggleOptions`, `DividerOptions`, `SpacingOptions`

Unlike business forms such as `MenuForm` and `ShopForm`, native UI does not include business logic such as permissions or Score; it provides the lowest-level form construction capabilities. The built-in interfaces of all modules (such as `blacklist.lcui` and `statistics.lcui`) as well as the `menu.lcui` and `shop.lcui` of Menu/Shop are all built on top of native UI.

## Form Lifecycle and GUIManager

When a form is created (e.g. `new CustomForm(id, title)`), the form is registered in `GUIManager` with `id` as the key, per player UUID. The form is automatically unregistered after it is closed; if the same player creates a form with the same `id` again, the old instance is overwritten.

| Function | Description |
| --- | --- |
| `GUIManager::open(id, formId, type[, ctx])` | Executes the loaded lcui script and opens the `formId` form registered under `id` |
| `GUIManager::switchTo(id, type)` | Switches directly to the form registered under `id` without re-executing the script |
| `GUIManager::value(id)` | Gets the GUI value registered by the plugin (returns an array) |
| `GUIManager::request(id, args)` | Requests the GUI data registered by the plugin (returns an array) |
| `GUIManager::callback(id, args)` | Calls the GUI callback registered by the plugin |

`type` in `GUIManager::open` and `GUIManager::switchTo` is an integer:

| type | Form type |
| --- | --- |
| 1 | CustomForm (Custom Form) |
| 2 | MessageBox (Dialog) |
| 3 | PaginatedForm (Paginated Form) |
| 4 | ScriptForm (Script Form, MenuForm/ShopForm, etc.) |

`GUIManager::open` accepts an optional `ctx` array; in the script, the corresponding element is retrieved via `new CtxValue(index)`.

> [!TIP]
> Plugin modules register their respective `value`, `request`, and `callback` to GUIManager at startup, e.g. `blacklist.players`, `market.items`, etc. Refer to the lcui file of the corresponding module for the specific registered IDs.

## Common Conventions

The text, number, and boolean parameters of native UI support direct values or observable objects:

| Parameter type | Accepted values |
| --- | --- |
| Text (TextValue) | `string`, `UIRawMessage`, `ObservableString`, `ObservableUIRawMessage` |
| Number (NumberValue) | `int`, `float`, `ObservableNumber` |
| Boolean (BooleanValue) | `bool`, `ObservableBoolean` |

Option classes are all created via `new XxxOptions()`; the fields are optional, and unset fields use native default values.

After a form is closed, the `show` callback receives the close result; all `show` callbacks must accept exactly one parameter, and button callbacks cannot accept parameters.

> [!WARNING]
> When the close result is `none` (no close reason), comparing it directly with a number triggers the `Cannot compare an empty optional value` error. It is recommended to first check the truthiness with `if (value)`, or compare only when the value is confirmed to exist.

### Close Reason (closeReason)

The close reason is taken from Minecraft's native `DataDrivenScreenClosedReason`:

| Value | Meaning |
| --- | --- |
| 0 | Program actively closes (ProgrammaticClose) |
| 1 | Program closes all (ProgrammaticCloseAll) |
| 2 | Player cancels/closes the interface (ClientCanceled) |
| 3 | Player is busy (UserBusy) |
| 4 | Form is invalid (InvalidForm) |

## CustomForm (Custom Form)

The custom form is the most commonly used native form, supporting controls such as title, text, input field, dropdown, toggle, slider, and button.

### CustomForm Construction

```lcui
form = new CustomForm(id, title);
```

Here `id` is the registration ID and `title` is the form title (supports string or UIRawMessage/ObservableString).

### CustomForm Methods

| Method | Description |
| --- | --- |
| `header(text, options)` | Adds a header text, `options` is `TextOptions` |
| `label(text, options)` | Adds a label text, `options` is `TextOptions` |
| `divider(options)` | Adds a divider, `options` is `DividerOptions` |
| `spacer(options)` | Adds blank spacing, `options` is `SpacingOptions` |
| `textField(label, observable, options)` | Adds an input field, `observable` is `ObservableString`, `options` is `TextFieldOptions` |
| `dropdown(label, observable, items, options)` | Adds a dropdown, `observable` is `ObservableNumber` (index of the selected item), `items` is a `DropdownItem` array, `options` is `DropdownOptions` |
| `toggle(label, observable, options)` | Adds a toggle, `observable` is `ObservableBoolean`, `options` is `ToggleOptions` |
| `slider(label, observable, min, max, options)` | Adds a slider, `observable` is `ObservableNumber`, `options` is `SliderOptions` |
| `button(label, callback, options)` | Adds a button, `callback` is a function with no parameters, `options` is `ButtonOptions` |
| `closeButton()` | Adds a close button |
| `show(callback)` | Shows the form, `callback` receives one parameter (`closeReason`, type int or `none`) |
| `close()` | Actively closes the form |
| `isShowing()` | Returns whether the form is currently showing |

### CustomForm Example

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
> To be honest, the world is really beautiful. Go out for a walk more often and you will see a different world o(*￣▽￣*)o

## MessageBox (Dialog)

The dialog is used to show a piece of content to the player and let the player choose between Button 1 / Button 2.

### MessageBox Construction

```lcui
box = new MessageBox(id, title);
```

### MessageBox Methods

| Method | Description |
| --- | --- |
| `body(text)` | Sets the body content of the dialog |
| `button1(label[, tooltip])` | Sets the text and optional tooltip of Button 1 |
| `button2(label[, tooltip])` | Sets the text and optional tooltip of Button 2 |
| `show(callback)` | Shows the dialog, `callback` receives a `MessageBoxResult` parameter |
| `close()` | Actively closes the dialog |
| `isShowing()` | Returns whether the dialog is currently showing |

`MessageBoxResult` fields:

| Field | Description |
| --- | --- |
| `closeReason` | The close reason (see the value table above) |
| `selection` | The selected button: `0` is Button 1, `1` is Button 2, `none` when nothing was selected |

### MessageBox Example

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
> The truthiness of `selection` is false when it is `0` (Button 1), true when it is `1` (Button 2), and false when it is `none` (nothing selected); `none` cannot be compared directly with a number.

## PaginatedForm (Paginated Form)

The paginated form is used to display a large number of entries, showing them page by page, and provides the ability to go to the previous page, the next page, and jump to a page number.

### PaginatedForm Construction

```lcui
form = new PaginatedForm(guiId, title, elements[, pageSize]);
```

Here `elements` is a string array (each string is one entry), `pageSize` is the number of entries per page, default `10`, minimum `1`.

### PaginatedForm Methods

| Method | Description |
| --- | --- |
| `previous()` | Switches to the previous page |
| `next()` | Switches to the next page |
| `choose(page)` | Jumps to the specified page |
| `previousButton(text)` | Adds a previous page button (custom text) |
| `nextButton(text)` | Adds a next page button (custom text) |
| `chooseButton(text, placeholder)` | Adds a page number jump button and input field placeholder |
| `button(label, callback, options)` | Adds a normal button |
| `closeButton()` | Adds a close button |
| `divider(options)` | Adds a divider |
| `dropdown(label, observable, items, options)` | Adds a dropdown |
| `header(text, options)` | Adds a header text |
| `label(text, options)` | Adds a label text |
| `slider(label, observable, min, max, options)` | Adds a slider |
| `spacer(options)` | Adds blank spacing |
| `textField(label, observable, options)` | Adds an input field |
| `toggle(label, observable, options)` | Adds a toggle |
| `show(callback)` | Shows the form, `callback` receives a `PaginatedFormResult` parameter |
| `close()` | Actively closes the form |
| `isShowing()` | Returns whether the form is currently showing |

> [!WARNING]
> `previousButton`, `nextButton`, and `chooseButton` can each be added only once per form; pagination buttons must be added before `show`.

`PaginatedFormResult` fields:

| Field | Description |
| --- | --- |
| `closeReason` | The close reason (see the value table above) |
| `selection` | The content of the entry clicked by the player (`""` when nothing was clicked) |
| `selectionIndex` | The index of the clicked entry among all entries (starting from 0) |
| `page` | The page number where the clicked entry is located |

### PaginatedForm Example

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

## Observable Series (Observable Data)

Observable data is used for two-way data binding of form controls. After a control modifies the data, `getData()` returns the latest value; changes can also be subscribed to.

### Observable Construction

```lcui
str = new ObservableString("default", true);
num = new ObservableNumber(0, false);
flag = new ObservableBoolean(false, true);
raw = new ObservableUIRawMessage(UIRawMessage.text("hello"), true);
```

The construction parameters are (initial value, client writable). When `clientWritable` is `true`, the player can modify the data in the interface.

### Observable Methods

| Method | Description |
| --- | --- |
| `isClientWritable()` | Returns whether the client is writable |
| `getData()` | Gets the current value |
| `setData(value)` | Sets the current value |
| `subscribe(callback)` | Subscribes to changes, `callback` receives one parameter (the new value after the change), returns the subscription ID |
| `unsubscribe(id)` | Unsubscribes by subscription ID |

> [!NOTE]
> The parameter type currently registered for `ObservableNumber::setData` is inconsistent with its implementation (registered as boolean type); if the call behaves abnormally, refer to the source code.

### Observable Example

```lcui
text = new ObservableString("", true);
text.subscribe(func (value) -> void {
    mc::runCmd("say changed to: " + value);
});

text.setData("new value");
```

## UIRawMessage (Raw Message)

`UIRawMessage` is used to construct rich text/translated text and can be passed as the text parameter (TextValue) of a form.

| Static method | Description |
| --- | --- |
| `UIRawMessage.text(string)` | Creates a plain text message |
| `UIRawMessage.translate(key)` | Creates translated text |
| `UIRawMessage.translate(key, array)` | Creates translated text with replacement parameters, `array` is a string array |
| `UIRawMessage.translate(key, uiRawMessage)` | Creates translated text with nested messages |
| `UIRawMessage.rawText(array)` | Combines multiple `UIRawMessage`s into raw text |

```lcui
title = UIRawMessage.translate("chat.gui.title");
form = new CustomForm("raw", title);
```

## Option Classes

Option classes are used to control the display and behavior of the corresponding controls; they are created with `new XxxOptions()` and then the fields are set.

| Class | Fields |
| --- | --- |
| `TextOptions` | `visible` |
| `ButtonOptions` | `disabled`, `tooltip`, `visible` |
| `TextFieldOptions` | `description`, `disabled`, `visible` |
| `DropdownOptions` | `description`, `disabled`, `visible` |
| `DropdownItem` | `label` (required), `value` (required), `description` |
| `SliderOptions` | `description`, `disabled`, `step`, `visible` |
| `ToggleOptions` | `description`, `disabled`, `visible` |
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

## Common Issues

- **Wrong number of parameters in the `show` callback**: the `show` callbacks of `CustomForm`, `MessageBox`, and `PaginatedForm` must accept exactly one parameter, otherwise `show callback must take exactly one parameter` is reported.
- **Button callback with parameters**: the `button` callback cannot accept parameters, otherwise `button callback must not take any arguments` is reported.
- **Adding pagination buttons repeatedly**: adding `previousButton`, `nextButton`, or `chooseButton` repeatedly reports an error (e.g. `previousButton has already been added`).
- **Form cannot be shown again after closing**: after a form is closed, it is automatically unregistered from GUIManager; the lcui script must be re-executed to create a new instance.
