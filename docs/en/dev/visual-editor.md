# Visual Form Editor

LCUI Studio is a `.lcui` visual form editor shipped with the documentation site as static assets. It runs directly in any phone or desktop browser. Besides the link at the bottom of this page, the **LCUI Studio** group in the site sidebar provides a direct entry: [Open the editor](https://loitietu.github.io/LOICollectionA/lcui-studio/).

## Scope

- All three form kinds (`CustomForm` / `MessageBox` / `PaginatedForm`) and every native control (`label` / `header` / `textField` / `dropdown` / `toggle` / `slider` / `button` / `divider` / `spacer` / pagination buttons / message box buttons)
- A WYSIWYG canvas previewing the form in the bedrock look, with an inspector that separates direct arguments from `*Options` fields
- Value-bearing controls automatically emit `ObservableString` / `ObservableBoolean` / `ObservableNumber` helper variables and dropdown options emit a `DropdownItem` sequence, matching hand-written script conventions
- Undo/redo, live validation, autosave and one-click `.lcui` export
- **Block-based callback editor**: write logic for button `on:` callbacks and the form `show(func(result))` callback without hand-writing after export

## Block Callback Editor

After selecting a control with a callback (e.g. a button) or the form itself, click **Edit on callback / Edit show callback** in the inspector to open the block workspace:

- The block library on the left is grouped by `Form Navigation / Data / Control Flow / Operators / Variables`; tap or drag blocks onto the canvas on the right
- Statement blocks chain head-to-tail into an execution sequence; C-shaped blocks such as `if / while / count loop / for-in loop` nest child statements; expression sockets (rounded boxes) accept operator blocks
- Click a block to select it, then delete, duplicate, move up/down, or indent/outdent; on mobile, tap-to-add and buttons provide a fallback (drag works too)
- Every change syncs live to the `.lcui` source preview and is covered by undo/redo and autosave

## Import Contract

The editor re-opens scripts it generated itself: top-level `let` helper declarations, a single declarative form block, callback bodies produced by the block editor, and a trailing `show(func(result) -> void { … })`. Hand-written scripts (with `import`, `tr()`, or unsupported syntax) are rejected with line-numbered diagnostics — exported files remain the starting point for hand-writing.

## Deployment

Drop the exported `.lcui` file into `plugins/LOICollectionA/gui/` on the server and reload via `/xxx reload`.
