# LCUI 语言服务

> [!NOTE]
> 以下内容取自 LOICollectionA 1.17.0 的代码结构，对于后续版本可能会有所不同。

## 定位

`src/LOICollectionA/frontend/lsp/` 是 `.lcui` 脚本的**语言服务核心**：它复用编译器前端（Lexer → Parser → ComponentExpander → SemanticAnalyzer）产出诊断、符号、补全、悬停与跳转定义，并按 LSP 协议封装成 JSON-RPC 消息。

它**只做协议与分析**，不做 I/O：

- 不监听端口、不读写 stdio、不启动线程
- 输入是一段字节流，输出是待发回的字节流
- 传输层（stdio、管道、WebSocket、游戏内命令）由调用方决定

这样设计有两个原因：插件本体不需要为了编辑器支持常驻一个服务端；而语言核心能被 gtest 直接驱动（`tests/common/frontend/LspTest.cpp`），无需任何进程间通信。

## 模块结构

| 文件 | 职责 |
| --- | --- |
| `Protocol.h` / `.cpp` | LSP 数据类型（`Position` / `Range` / `Diagnostic` / `CompletionItem` / `Hover` / `Location`）、JSON 序列化、JSON-RPC 消息构造、`Content-Length` 分帧与解帧 |
| `Analysis.h` / `.cpp` | 对一段脚本文本跑完整前端，输出 `DocumentAnalysis`（诊断 + 符号表） |
| `LanguageServer.h` / `.cpp` | 文档同步（全量同步）、请求分发、能力声明，以及 `completionsAt` / `hoverAt` / `definitionAt` 三个纯函数 |

## 能力清单

| LSP 方法 | 行为 |
| --- | --- |
| `initialize` | 返回 `textDocumentSync: 1`（全量同步）、`completionProvider`（触发字符 `.`）、`hoverProvider`、`definitionProvider` |
| `textDocument/didOpen` | 记录文档，立即推送 `publishDiagnostics` |
| `textDocument/didChange` | 整体替换文本（取最后一条 `contentChanges`），重新推送诊断 |
| `textDocument/didClose` | 移除文档并清空该文档的诊断 |
| `textDocument/completion` | 见「补全规则」 |
| `textDocument/hover` | 返回光标处标识符的声明签名 |
| `textDocument/definition` | 返回声明处的 `Location` |
| `shutdown` / `exit` | 正常结束 |

未实现的方法返回 `-32601 Method not found`，`$/...` 通知静默忽略。

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

`analyze` 在语义分析**之后**遍历 AST，因此 `impl` 块合并进目标类的方法、`component` 展开生成的节点都会被纳入：

| 来源 | 符号类型 | 归属（container） |
| --- | --- | --- |
| `class` | Class | 全局 |
| 类字段 / `static` 字段 | Field / StaticField | 类名 |
| 类方法 / `static` 方法 | Method / StaticMethod | 类名 |
| `trait` | Interface | 全局 |
| `trait` 内的方法 | Method | trait 名 |
| `impl` 内的方法与关联常量 | Method / StaticMethod / Constant | impl 目标类型名 |
| 顶层 `func` | Function | 全局 |
| 顶层 `let` / `const` | Variable / Constant | 全局 |

`detail` 为可读签名，例如 `size() -> int`、`add(x: int, y: int) -> int`。

## 补全规则

**触发 `.` 时**（成员补全）：取 `.` 之前的标识符作为接收者。

- `this` → 取光标前最近一次 `class <Name>` 声明的类
- 已知类名 → 该类的静态成员
- 带类型标注的变量（`let deck: Deck = ...`）→ 其标注类型的成员
- 无法推断 → 返回空列表（不做"猜一个"式的误报）

**其他位置**（全局补全）：全局符号 + 关键字 + 基础类型 + 内置函数（`tr` / `score` / `entity` / `print` / `println` / `error`）+ 表单类（`CustomForm` / `MessageBox` / `PaginatedForm` / `ScriptForm`）。

## 已知限制

- 局部变量（函数参数、块内 `let`）不进符号表，因此不参与补全与跳转
- 不解析 `import`，跨文件符号需要调用方先把导入图展开后逐文档喂入
- 每次请求都会重新跑一遍完整前端，未做增量分析；单文件脚本规模下开销可忽略
- 诊断区间长度为 1 个字符（诊断引擎只记录起点，不记录词法单元长度）
