# LCUI Language Service

> [!NOTE]
> The following content is taken from the code structure of LOICollectionA 1.17.3 and may differ in later versions.

## Purpose

`src/LOICollectionA/frontend/lsp/` is the **language service core** for `.lcui` scripts. It reuses the compiler front end (Lexer → Parser → ComponentExpander → SemanticAnalyzer) to produce diagnostics, symbols, completions, hovers, navigation, rename edits, semantic highlighting and inlay hints, and wraps them into JSON-RPC messages per the LSP specification.

`LanguageServer` does **protocol and analysis only**, never I/O:

- no listening sockets, no stdio access, no threads
- input is a chunk of bytes, output is the bytes to send back
- the transport (stdio, pipes, WebSocket, an in-game command) is the caller's choice

The repository also ships an optional `LspServer`: disabled by default, it listens on `127.0.0.1` with TCP when `"LanguageServer": { "enabled": true, "port": 9257 }` is configured and carries the same JSON-RPC frames. Keeping the core separate from the transport lets the plugin expose editor support only when asked, while gtest drives the core directly (`tests/common/frontend/LspTest.cpp`) with no inter-process communication.

## Module Layout

| File | Responsibility |
| --- | --- |
| `Protocol.h` / `.cpp` | LSP data types (`Position` / `Range` / `Diagnostic` / `CompletionItem` / `Hover` / `Location` / `SemanticTokens` / `InlayHint`), JSON conversion, JSON-RPC message construction, `Content-Length` framing and deframing |
| `Analysis.h` / `.cpp` | Runs the full front end over a script text and returns a `DocumentAnalysis` (diagnostics + symbols + imported symbols + inlay hints) |
| `LanguageServer.h` / `.cpp` | Document sync (incremental and full), request dispatch, capability declaration, plus pure functions for completion / hover / navigation |
| `Server.h` / `.cpp` | Optional loopback TCP transport backed by an asio event loop |

## Supported Capabilities

| LSP method | Behavior |
| --- | --- |
| `initialize` | Declares `textDocumentSync: 2` (incremental sync), completion trigger characters `.` and `:`, hover, definition, type definition, references, document highlights, rename, document symbols, signature help, semantic tokens (full), inlay hints, formatting and code actions |
| `textDocument/didOpen` | Registers the document and immediately publishes diagnostics |
| `textDocument/didChange` | Applies `contentChanges` in order: entries with a `range` replace that span, entries without a `range` replace the whole text; diagnostics are republished |
| `textDocument/didClose` | Removes the document and clears its diagnostics |
| `textDocument/completion` | See "Completion Rules" |
| `textDocument/hover` | Returns the declaration signature of the symbol under the cursor, a native member signature, or a namespace description |
| `textDocument/definition` / `declaration` | Returns the declaration `Location`; cross-file results carry the imported document URI |
| `textDocument/typeDefinition` | Jumps from a variable / field to the class, enum or trait named by its annotation or inferred type |
| `textDocument/references` | Returns every reference that resolves to the same symbol |
| `textDocument/documentHighlight` | Marks read / write occurrences of the same symbol (`2` read, `3` write) |
| `textDocument/rename` | Returns every `TextEdit` whose occurrence resolves to the target; the caller can wrap them in a `WorkspaceEdit` |
| `textDocument/documentSymbol` | Returns a flat list of non-local symbols |
| `textDocument/signatureHelp` | See "Signature Help Rules" |
| `textDocument/semanticTokens/full` | Returns semantic tokens for the whole document; `full/delta` currently returns the full result |
| `textDocument/inlayHint` | Returns type hints inside the requested range |
| `textDocument/formatting` | Re-indents by `{}` / `[]` nesting and returns a whole-document edit |
| `textDocument/codeAction` | Returns a `quickfix` for diagnostics inside the requested range |
| `shutdown` / `exit` | Terminates cleanly |

Unimplemented methods return `-32601 Method not found`; `$/...` notifications are ignored silently.

## Quick Fix Rules

`textDocument/codeAction` re-runs the analysis and keeps only diagnostics that intersect the requested range and contain `is not declared`: it inserts `let ` before the identifier and titles the action `Declare '<name>' with let`. The insertion point comes from the diagnostic start and is checked against the document text, so a shifted diagnostic cannot rewrite the wrong location.

## Wiring It Up

`LanguageServer::handle` accepts an arbitrary chunk of bytes and returns the bytes to send back to the client (possibly several messages, possibly empty). The caller only has to feed in what it reads and write out what it gets:

```cpp
lsp::LanguageServer server;

std::string outbound = server.handle(bytesReadFromClient);
if (!outbound.empty())
    writeToClient(outbound);
```

Incomplete messages are buffered internally: a frame split across several reads is neither lost nor mis-framed. A single frame larger than 64 MiB, or one that fails JSON parsing, is dropped so the buffer cannot grow without bound.

> [!WARNING]
> Encoding uses `dump(-1, ' ', false, error_handler_t::replace)`. Script sources may contain invalid UTF-8 (the fuzz harness covers that path), and the default `strict` handler throws — this project compiles with `set_exceptions("none")`, where a throw terminates the process. Use the same arguments for any new serialization point.

## Symbol Collection Rules

`analyze` walks the AST **after** semantic analysis, so methods merged into their target class by `impl` blocks and nodes generated by `component` expansion are all included. Each symbol records its declaration offset, scope bounds and a `local` flag for completion, navigation and resolution checks:

| Source | Symbol kind | Container | Local |
| --- | --- | --- | --- |
| `class` | Class | global | no |
| class field / `static` field | Field / StaticField | class name | no |
| class method / `static` method | Method / StaticMethod | class name | no |
| `trait` | Interface | global | no |
| method inside a `trait` | Method | trait name | no |
| methods and associated constants in `impl` | Method / StaticMethod / Constant | impl target type name | no |
| `enum` | Enum | global | no |
| enum variant | EnumMember | enum name | no |
| `component` | Component | global | no |
| `component` parameter | Parameter | component name | yes |
| `import` | Module | global | no |
| top-level `func` | Function | global | no |
| top-level `let` / `const` | Variable / Constant | global | no |
| function / method / lambda parameter | Parameter | owning function or class | yes |
| block `let`, `for`-`in` variables, `match` bindings, `if let` / `while let` bindings | Variable | global | yes |

`detail` holds a readable signature, e.g. `size() -> int`, `add(x: int, y: int) -> int`, `Some(int)` or `component card(title, body)`.

## Import Resolution

`ImportResolver` maps an `import` path to `{ uri, text }`. After `didOpen`, `LanguageServer` builds one automatically: it first resolves relative to the importing document URI, then matches open documents by full path or suffix. As long as the dependency is open, cross-file completion, hover, definition, references and rename all work.

- Resolution depth is capped at 8; cyclic imports are deduplicated by the read stack
- Selective imports filter to the requested names; `import "path";` imports the whole file
- Unresolvable files are skipped silently, and diagnostics stay scoped to the current document
- The pure functions accept a custom `ImportResolver` backed by disk or a virtual file system

## Completion Rules

**After `.`** (member completion): the identifier before the dot is the receiver.

- `this` → the class of the nearest `class <Name>` declaration before the cursor
- a known class name → that class's static members
- a variable / field / parameter → its annotated type, or a class inferred from a `new` expression, parameter type or return type
- an enum name → that enum's variants (e.g. the completion form `Suit.He`)
- native classes: `String` / `Array` / `Map` etc. expose fields, methods and signatures through ABI reflection; string and array receivers map to their native class
- `std::`, `math::`, `mc::`, `GUIManager::`, `storage::`, `playerdata::` and other namespaces → overloads of registered functions
- unknown receiver → an empty list (no guessed suggestions)

**Anywhere else** (global completion): reachable local symbols + imported symbols + global symbols + enum variants + keywords + primitive types + every registered native function / macro / class.

## Signature Help Rules

`signatureHelpAt` scans backward to the innermost unclosed `(`, recognizes `namespace::function`, `receiver.method`, `ClassName(...)`, enum variants and macros, and collects signatures from:

- method / function declarations in the current and imported documents
- native function and method overloads from ABI reflection
- native class constructors
- enum variant payload signatures
- the macro registry

`activeParameter` counts top-level commas; nested `()` / `[]` / `{}` are ignored. It returns `null` when no signature is found.

## Semantic Tokens and Inlay Hints

The semantic token legend order is:

```text
namespace, type, class, enum, interface, struct, typeParameter, parameter,
variable, property, enumMember, event, function, method, macro, keyword,
modifier, comment, string, number, regexp, operator
```

Token lengths come from real lexemes: identifiers and strings cover the full source slice, while multi-line strings and block comments are split per line; comments are scanned separately. Declared symbols in the document take precedence over heuristic classification, so locals, parameters, enums, components and imported modules are typed accurately.

Inlay hints currently emit type hints only (kind `1`): unannotated function parameters and local `let` declarations show `: Type` after the name. Types come from the semantic analyzer; `unknown` and `void` produce no hint.

## Known Limitations

- Every request re-runs the whole front end — there is no incremental parsing; incremental sync only reduces the bytes transferred
- Diagnostic ranges prefer the full identifier or string token; other locations may still be one character long
- Quick fixes cover only "variable not declared" and insert `let` without inferring an initializer
- Semantic tokens support full requests only; `full/delta` returns no incremental edits
- Cross-file resolution needs an `ImportResolver`; the built-in TCP server resolves only open documents
