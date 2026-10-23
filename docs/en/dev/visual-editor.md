# Visual Form Editor

LCUI Studio is a `.lcui` visual form editor shipped with the documentation site as static assets. It runs directly in any phone or desktop browser: [Open the editor](/LOICollectionA/lcui-studio/index.html).

## Scope

- All three form kinds (`CustomForm` / `MessageBox` / `PaginatedForm`) and every native control (`label` / `header` / `textField` / `dropdown` / `toggle` / `slider` / `button` / `divider` / `spacer` / pagination buttons / message box buttons)
- A WYSIWYG canvas previewing the form in the bedrock look, with an inspector that separates direct arguments from `*Options` fields
- Value-bearing controls automatically emit `ObservableString` / `ObservableBoolean` / `ObservableNumber` helper variables and dropdown options emit a `DropdownItem` sequence, matching hand-written script conventions
- Undo/redo, live validation, autosave and one-click `.lcui` export

## Import Contract

The editor only re-opens scripts it generated itself: top-level `let` helper declarations, a single declarative form block, empty callback placeholders and a trailing `show();`. Hand-written scripts (with `import`, `tr()`, control flow, and so on) are rejected with line-numbered diagnostics — exported files are the starting point for hand-writing, not a reverse-engineering entry for arbitrary scripts.

## Deployment

Drop the exported `.lcui` file into `plugins/LOICollectionA/gui/` on the server and reload via `/xxx reload`. Button `on:` callback bodies are filled in after export; the editor owns structure and appearance only.
