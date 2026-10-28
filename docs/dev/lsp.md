# LCUI 语言服务

> [!NOTE]
> 以下内容取自 LOICollectionA 1.18.0 的代码结构，对于后续版本可能会有所不同。

## 定位

`src/LOICollectionA/frontend/lsp/` 是 `.lcui` 脚本的**语言服务核心**：它复用编译器前端（Lexer → Parser → ComponentExpander → SemanticAnalyzer）产出诊断、符号、补全、悬停、跳转、重命名、语义高亮与内联提示，并按 LSP 协议封装成 JSON-RPC 消息。

`LanguageServer` 只做协议与分析，不做 I/O：

- 不监听端口、不读写 stdio、不启动线程
- 输入是一段字节流，输出是待发回的字节流
- 传输层（stdio、管道、WebSocket、游戏内命令）由调用方决定

仓库另提供可选的 `LspServer`：默认关闭，在配置 `"LanguageServer": { "enabled": true, "port": 9257 }` 时监听 `127.0.0.1`，用 TCP 承载同样的 JSON-RPC 帧。核心与传输层分离，既能让插件按需提供编辑器服务，也能让 gtest 直接驱动语言核心（`tests/common/frontend/LspTest.cpp`），无需进程间通信。

## 模块结构

| 文件 | 职责 |
| --- | --- |
| `Protocol.h` / `.cpp` | LSP 数据类型（`Position` / `Range` / `Diagnostic` / `CompletionItem` / `Hover` / `Location` / `SemanticTokens` / `InlayHint`）、JSON 序列化、JSON-RPC 消息构造、`Content-Length` 分帧与解帧 |
| `Analysis.h` / `.cpp` | 对一段脚本文本跑完整前端，输出 `DocumentAnalysis`（诊断 + 符号 + 导入符号 + 内联提示） |
| `LanguageServer.h` / `.cpp` | 文档同步（增量与全量）、请求分发、能力声明，以及补全 / 悬停 / 跳转等纯函数 |
| `Server.h` / `.cpp` | 可选的回环 TCP 传输层，内部持有 `LanguageServer` 与 asio 事件循环 |

## 能力清单

| LSP 方法 | 行为 |
| --- | --- |
| `initialize` | 声明 `textDocumentSync: 2`（增量同步）、补全触发字符 `.` 与 `:`、悬停、定义、类型定义、引用、文档高亮、重命名、文档符号、签名帮助、语义令牌（full）、内联提示、格式化与快速修复 |
| `textDocument/didOpen` | 记录文档，立即推送 `publishDiagnostics` |
| `textDocument/didChange` | 按 `contentChanges` 顺序应用编辑：带 `range` 的条目按行/列替换，不带 `range` 的条目整体替换；随后重新推送诊断 |
| `textDocument/didClose` | 移除文档并清空该文档的诊断 |
| `textDocument/completion` | 见「补全规则」 |
| `textDocument/hover` | 返回光标处符号的声明签名、原生成员签名或命名空间说明 |
| `textDocument/definition` / `declaration` | 返回声明处的 `Location`，跨文件时带导入文档的 URI |
| `textDocument/typeDefinition` | 由变量 / 字段的类型标注或推断结果跳转到类、枚举或 trait 声明 |
| `textDocument/references` | 返回解析后属于同一符号的全部引用 |
| `textDocument/documentHighlight` | 标记同一符号的读 / 写出现位置（`2` 读，`3` 写） |
| `textDocument/rename` | 返回与目标符号解析结果一致的全部 `TextEdit`；调用方可用 `WorkspaceEdit` 包装 |
| `textDocument/documentSymbol` | 返回非局部符号的扁平列表 |
| `textDocument/signatureHelp` | 见「签名帮助规则」 |
| `textDocument/semanticTokens/full` | 返回整篇文档的语义令牌；`full/delta` 当前按全量结果返回 |
| `textDocument/inlayHint` | 返回请求区间内的类型提示 |
| `textDocument/formatting` | 基于 `{}` / `[]` 嵌套重排缩进，返回整篇替换编辑 |
| `textDocument/codeAction` | 为请求区间内的诊断返回 `quickfix`，见「快速修复规则」 |
| `shutdown` / `exit` | 正常结束 |

未实现的方法返回 `-32601 Method not found`，`$/...` 通知静默忽略。

## 快速修复规则

`textDocument/codeAction` 会重新跑一遍分析，只保留与请求区间相交、且消息含 `is not declared` 的诊断：在标识符前插入 `let `，标题为 `Declare '<name>' with let`。插入点取自诊断起点，并用文档内容校验该位置确实是同名标识符，避免诊断定位偏移时改错位置。

## 接线方式

`LanguageServer::handle` 接收任意一段字节流，返回需要发回客户端的字节流（可能为多条消息，也可能为空）。调用方只需把读到的数据喂进去、把返回值写出去：

```cpp
lsp::LanguageServer server;

std::string outbound = server.handle(bytesReadFromClient);
if (!outbound.empty())
    writeToClient(outbound);
```

内部会自行缓冲不完整的消息：一帧被拆成多次到达时不会丢数据，也不会错帧；单帧超过 64 MiB 或 JSON 解析失败时丢弃该帧，不会让缓冲区无限增长。

> [!WARNING]
> 消息编码使用 `dump(-1, ' ', false, error_handler_t::replace)`。脚本源码可能含非法 UTF-8（模糊测试已覆盖该路径），默认的 `strict` 处理器会抛异常，而本项目以 `set_exceptions("none")` 编译，异常即终止进程。新增序列化点时请沿用同样的参数。

## 符号收集规则

`analyze` 在语义分析**之后**遍历 AST，因此 `impl` 块合并进目标类的方法、`component` 展开生成的节点都会被纳入。符号同时记录声明偏移、作用域起止与 `local` 标记，供补全、跳转和解析校验使用：

| 来源 | 符号类型 | 归属（container） | 局部 |
| --- | --- | --- | --- |
| `class` | Class | 全局 | 否 |
| 类字段 / `static` 字段 | Field / StaticField | 类名 | 否 |
| 类方法 / `static` 方法 | Method / StaticMethod | 类名 | 否 |
| `trait` | Interface | 全局 | 否 |
| `trait` 内的方法 | Method | trait 名 | 否 |
| `impl` 内的方法与关联常量 | Method / StaticMethod / Constant | impl 目标类型名 | 否 |
| `enum` | Enum | 全局 | 否 |
| 枚举变体 | EnumMember | 枚举名 | 否 |
| `component` | Component | 全局 | 否 |
| `component` 参数 | Parameter | component 名 | 是 |
| `import` | Module | 全局 | 否 |
| 顶层 `func` | Function | 全局 | 否 |
| 顶层 `let` / `const` | Variable / Constant | 全局 | 否 |
| 函数 / 方法 / lambda 参数 | Parameter | 所属函数或类 | 是 |
| 块内 `let`、`for`-`in` 变量、`match` 绑定、`if let` / `while let` 绑定 | Variable | 全局 | 是 |

`detail` 为可读签名，例如 `size() -> int`、`add(x: int, y: int) -> int`、`Some(int)`、`component card(title, body)`。

## 导入解析

`ImportResolver` 把 `import` 路径映射为 `{ uri, text }`。`LanguageServer` 在收到 `didOpen` 后自动构建解析器：先按导入文件的 URI 做相对解析，再在当前打开的文档中按完整路径或后缀匹配，因此只要依赖文件已经打开，跨文件补全、悬停、定义、引用与重命名都能工作。

- 解析深度上限为 8，出现循环导入时按读取栈去重
- 仅导入声明的符号时过滤非目标符号；`import "path";` 导入该文件的全部导出
- 解析不到的文件被静默跳过，诊断仍以当前文档为准
- 纯函数接口可传入自定义 `ImportResolver`，由调用方从磁盘或虚拟文件系统读取

## 补全规则

**触发 `.` 时**（成员补全）：取 `.` 之前的标识符作为接收者。

- `this` → 取光标前最近一次 `class <Name>` 声明的类
- 已知类名 → 该类的静态成员
- 变量 / 字段 / 参数 → 其标注类型，或在 `new` 表达式、参数类型、返回值类型中推断出的类
- 枚举名 → 该枚举的变体（如 `Suit.He`）
- 原生类：`String` / `Array` / `Map` 等通过 ABI 反射列出字段、方法与签名；字符串与数组的接收者也会映射到对应原生类
- `std::`、`math::`、`mc::`、`GUIManager::`、`storage::`、`playerdata::` 等命名空间 → 已注册函数的重载列表
- 无法推断 → 返回空列表（不做"猜一个"式的误报）

**其他位置**（全局补全）：当前作用域可达的局部符号 + 导入符号 + 全局符号 + 枚举变体 + 关键字 + 基础类型 + 全部已注册原生函数 / 宏 / 类。

## 签名帮助规则

`signatureHelpAt` 从光标向前匹配最内层未闭合的 `(`，识别 `命名空间::函数`、`接收者.方法`、`类名(...)`、枚举变体与宏，并从以下来源收集签名：

- 当前文档与导入文档中的方法 / 函数声明
- ABI 反射得到的原生函数重载与方法重载
- 原生类构造器
- 枚举变体的载荷签名
- 宏注册表

`activeParameter` 按顶层逗号计数，嵌套的 `()` / `[]` / `{}` 不计入。找不到签名时返回 `null`。

## 语义令牌与内联提示

语义令牌的图例顺序为：

```text
namespace, type, class, enum, interface, struct, typeParameter, parameter,
variable, property, enumMember, event, function, method, macro, keyword,
modifier, comment, string, number, regexp, operator
```

令牌长度取自真实词法单元：标识符与字符串覆盖完整源码片段，跨行字符串与块注释按行拆分；注释单独扫描补充。文档内的声明符号优先于启发式分类，因此局部变量、参数、枚举、组件与导入模块都能得到准确类型。

内联提示目前只生成类型提示（kind `1`）：未显式标注类型的函数参数与局部 `let` 在名称后显示 `: 类型`。类型来自语义分析器的推断；`unknown` 与 `void` 不生成提示。

## 已知限制

- 每次请求都会重新跑一遍完整前端，未做增量解析；增量同步只减少文本传输量，单文件脚本规模下开销可忽略
- 诊断区间优先扩展为完整标识符或字符串，其他位置仍可能只有 1 个字符
- 快速修复目前只覆盖「变量未声明」一类，且只插入 `let`，不推断初始值
- 语义令牌只支持全量请求；`full/delta` 不返回增量编辑
- 跨文件解析依赖调用方提供 `ImportResolver`；内置 TCP 服务只解析已打开的文档
