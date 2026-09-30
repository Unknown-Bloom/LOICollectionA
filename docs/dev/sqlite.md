# SQLite 层使用教程

> [!NOTE]
> 本文基于 LOICollectionA 1.17.0 的数据层代码（`src/LOICollectionA/data/sqlite/`），后续版本可能调整。示例沿用项目既有的块存储（BlockStore）与类型化表（TypedTable）约定，可直接照搬到模块开发中。

数据层是 LOICollectionA 全部持久化的底座。它在 SQLite 之上做了一层**块存储抽象**，再用模板把列名提升到**编译期**，让业务代码以类型安全的方式读写，而不必手写 SQL、也不必担心 schema 漂移。

本教程面向需要在模块里持久化数据的开发者，按"概念 → API 速查 → 实战"的顺序展开。如果你只想快速上手，直接跳到 [定义一张表](#5-定义一张表schema) 与 [完整示例](#13-完整示例一个模块)。

## 1. 它解决什么问题

普通 SQLite 用法有两个长期痛点：

- **schema 漂移**：手写的 `CREATE TABLE` 与业务代码里"第几列是什么"靠人脑对齐，列一增删就错位、踩坑。
- **SQL 注入与拼接**：运行时拼字符串 SQL，既易错又有安全风险。

LOICollectionA 的答案是：

- 用**块模型**把数据拆成可寻址的"块"，schema 与业务数据解耦；
- 用 `TypedTable<E, Version>` 把"列"变成枚举，列名在编译期解析，拼错即报编译错误；
- 单元格统一编解码（`CellCodec`），字符串/整型/浮点/bool 自动映射 SQLite 亲和性；
- 全程返回 `ll::Expected<T>`，用 `and_then` / `transform` / `or_else` 链式处理，无异常。

## 2. 分层结构

```txt
TypedTable<E, Version>        # 编译期列名 + 版本化表结构（业务唯一入口）
        |  读写 / 事务
BlockRepository               # 门面：open / fromStore / store() / meta* / exec
        |
BlockStore                    # 块模型持久化（dict / block / prop / link / meta）
WriteBatch                    # 原子写事务
        |
ConnectionPool               # SQLite 连接池（WAL 模式）
```

| 层 | 位置 | 职责 |
| --- | --- | --- |
| `ConnectionPool` | `data/sqlite/connection/` | SQLite 连接管理：`journal_mode=WAL` + `synchronous=NORMAL` + `temp_store=MEMORY` + `cache_size=8096` + `busy_timeout=5000` |
| `BlockStore` | `data/sqlite/block/` | 把数据拆成可寻址的"块"，按 key 增量读写，五张系统表：`dict` / `block` / `prop` / `link` / `meta` |
| `WriteBatch` | 同上 | 一批修改在一次 `commit()` 中原子落盘，失败 `rollback()` |
| `BlockRepository` | 同上 | 对外只剩：`open` / `fromStore` / `store()` / `metaGet\|Set\|Del` / `exec` |
| `TypedTable` | 同上 | 把列名提升到编译期；`open` 时把 `版本 + 类型名 + 列数 + 索引掩码` 写入 `schema:<表名>`，不一致返回 `SchemaMismatch` |

业务代码**只应该接触 `TypedTable`**（极少数场景才直接碰 `BlockRepository`）。

## 3. 核心概念

### 3.1 块模型

`BlockStore` 把整库抽象成一颗以 `BlockId` 为节点的树：

- `block`：节点本体（id、parent、kind、name、state、payload）
- `dict`：父节点下 `name → BlockId` 的索引，行键（rowKey）由此定位
- `prop`：节点附加属性（用于可 SQL 查询的"镜像列"）
- `link`：节点之间的有向边
- `meta`：库级键值（schema 指纹、side 指纹就存在这里）

每个 `TypedTable` 的逻辑"行"是一个以 `rowKey`（字符串，通常是玩家 uuid 或名字）命名的子块；具体单元格既可能压在 `block.payload` 里，也可能镜像到 **side 表** `col_<rootId>` 中以供 SQL 查询。side 表由插件自动维护，你无需感知。

> [!TIP]
> `block.payload` 是块的原值（任意 JSON 化的单元格集合），side 表是它为可查询列额外建的镜像表。读已建索引的列走 side 表，读未索引列回退到 payload——这一切换层自动完成。

### 3.2 列即枚举

表的所有列集中声明为一个 `enum class`，**枚举顺序即物理列号**：

```cpp
enum class XxxCol { id, name, time };   // id=0, name=1, time=2
```

因此：

- **新增列只能追加在枚举末尾**，否则与已落盘的数据错位。
- 列名既可以是枚举值（`XxxCol::name`），也可以是字符串字面量（`"name"`）——字面量在**编译期**解析成枚举，拼错即编译失败。
- 运行期得到的 `std::string` 列名请用 `parseColumn()`，不要用字面量形式。

### 3.3 类型与亲和性

列的物理亲和性由 `TypedColumn<E>::Types`（一个 `std::tuple`）推导，长度必须与枚举个数一致：

| C++ 类型 | SQLite 亲和性 |
| --- | --- |
| 整型（`int`/`long long`/…） | `INTEGER` |
| 浮点（`double`/`float`） | `REAL` |
| `bool` | `INTEGER`（`1` / `0`） |
| 其余（`std::string`、自定义类型经 `CellCodec`） | `TEXT` |

```cpp
enum class XxxCol { id, name, time };

namespace LOICollection::data {
    template <>
    struct TypedColumn<LOICollection::server::Plugins::XxxCol> {
        using Types = std::tuple<std::string, std::string, long long>;
    };
}
```

> [!TIP]
> 数值字段尽量声明成 `long long` / `double` 而非 `std::string`：前者落 `INTEGER`/`REAL`，体积小、可比较、能做范围查询；后者一律 `TEXT`，比较时按字典序，易出错。

### 3.4 模式指纹与版本

`open` 会在 `schema:<表名>` 写入指纹：`版本 \x1e 类型名 \x1e 列数 \x1e 索引掩码`。

- 版本号或类型名不一致 → 立即返回 `BlockError::SchemaMismatch`（拒绝打开，绝不静默错配）。
- 列数或索引掩码变化 → 标记为 `schemaDirty`，自动重建 side 表并把存量 payload 回填进去（**不会迁移旧版数据，只重建当前块的镜像**）。

版本号是你主动递增的升级旋钮：结构变了就 `+1`，`open` 会安全地重建镜像，而不是读坏数据。

## 4. 分层 API 速查

### 4.1 ConnectionPool（内部）

业务不直接使用。每个库以 `WAL` 模式打开，`busy_timeout=5s` 让并发写自动等待而非立即失败。库文件是 `plugins/LOICollectionA/data/*.db`。

### 4.2 BlockRepository（门面）

```cpp
#include "LOICollectionA/data/sqlite/block/BlockRepository.h"

// 打开（或新建）一个库，connections 默认为 4
auto db = BlockRepository::open((dataPath / "xxx.db").string(), 4);
if (!db) return ll::makeStringError(db.error().message());
std::shared_ptr<BlockRepository> repo = std::move(db.value());

// 复用已有 store（例如全局设置库 SettingsDB 用同一文件时）
// auto repo = BlockRepository::fromStore(existingStore);

repo->store();                       // 返回底层 BlockStore&，TypedTable/WriteBatch 基于它
repo->exec("VACUUM;");               // 执行原生 SQL（自动提交，见第 9 节）
repo->metaGet("sidecol:Xxx");        // ll::Expected<std::optional<std::string>>
repo->metaSet("key", "value");       // ll::Expected<void>
repo->metaDel("key");
```

| 方法 | 说明 |
| --- | --- |
| `open(path, connections=4)` | 打开（或新建）数据库，返回 `shared_ptr<BlockRepository>` |
| `fromStore(store)` | 基于已有的 `BlockStore` 构造门面（同文件多表共享连接池时用） |
| `store()` | 返回底层 `BlockStore&` |
| `exec(sql)` | 执行原生 SQL（自动提交） |
| `metaGet/metaSet/metaDel` | 读写库级元信息（`TypedTable` 用它保存 schema / side 指纹） |

### 4.3 WriteBatch（事务）

```cpp
#include "LOICollectionA/data/sqlite/block/WriteBatch.h"

auto b = WriteBatch::begin(repo->store());
if (!b) return ll::makeStringError(b.error().message());
auto& tx = *b;

tx.exec("UPDATE ...");                       // 原生 SQL（DML）
tx.execCells("key", "INSERT ... VALUES(?,?)", params); // 带参数的原生 SQL
tx.upsertRow(parentId, rowKey);              // 写入一行（不存在则建块）
tx.append(parentId, kind, name, payload);    // 追加子块
tx.setProp(id, key, ival / rval / tval);     // 设置节点属性
tx.control(id, BlockLifecycle::Deleted);     // 标记删除（软删除）

auto ok = tx.commit();                       // ll::Expected<bool>
if (!ok) { tx.rollback(); /* ... */ }
```

| 方法 | 说明 |
| --- | --- |
| `begin(store)` | 开启事务，返回 `unique_ptr<WriteBatch>` |
| `exec(sql)` / `execCells(key, sql, params)` | 执行 SQL（带 / 不带参数） |
| `upsertRow(parent, name, payload={})` | 写入一行，返回 `BlockId` |
| `append(parent, kind, name, payload)` | 追加子块，返回 `BlockId` |
| `setProp(id, key, ...)` | 设置节点属性（三种重载：整型 / 浮点 / 文本） |
| `control(id, lifecycle)` | 改变节点状态（如 `Deleted` 软删除） |
| `commit()` / `rollback()` | 提交（返回 `Expected<bool>`）/ 回滚 |

> [!WARNING]
> `WriteBatch` **不可拷贝、不可移动**。它持有 `StorageTransaction`，只能以 `unique_ptr` 在作用域内使用。业务代码通常不需要直接创建它——`TypedTable` 的 `tx()` 已经替你包好了一层 `Batch`。

### 4.4 TypedTable（业务入口）

```cpp
#include "LOICollectionA/data/sqlite/block/TypedTable.h"
using LOICollection::data::TypedTable;
using LOICollection::data::FindMode;

// 打开（或新建）一张表
auto table = XxxTable::open(*repo, "Xxx");
if (!table) return ll::makeStringError(table.error().message());
XxxTable t = std::move(table.value());
```

`Key` 是列参数的统一类型：枚举值或字符串字面量都行，字面量在编译期解析。

| 方法 | 说明 |
| --- | --- |
| `open(repo, name)` | 打开（或新建）表并返回句柄，句柄只持有 `repo` 引用，可随意移动 |
| `get<T>(row, col, def)` | 读单列，行不存在时返回 `def`（字符串列名仅支持字面量） |
| `set<T>(row, col, value)` | 写单列，行不存在时自动创建 |
| `getRow(row)` / `setRow(row, map)` | 整行读写（列名→字符串值的映射） |
| `has(row)` / `del(row)` / `list()` | 判断存在、删除、列出所有 rowKey |
| `find(FindMode, conds)` | 按条件查询匹配的 rowKey 列表（`FindMode::And` / `FindMode::Or`） |
| `findFirst(FindMode, conds)` | 同上，只取第一个 |
| `findValues<T>(col, FindMode, conds)` | 投影出某列的值列表 |
| `tx()` | 开启批写事务，返回 `Batch` |
| `parseColumn(s)` | 运行期把字符串解析成枚举列（静态方法） |
| `clearTypedTable(repo, name)` | 清空一张表的所有行（独立函数） |

## 5. 定义一张表（Schema）

所有表的列集中声明在 `src/LOICollectionA/include/server/Plugins/types/<模块>/<模块>Schema.h`，分三段：**枚举 + 类型特化 + 别名**。

```cpp
// include/server/Plugins/types/xxx/XxxSchema.h
#pragma once

#include "LOICollectionA/data/sqlite/block/TypedTable.h"

namespace LOICollection::server::Plugins {
    using LOICollection::data::TypedTable;

    enum class XxxCol { id, name, time, score };
}

namespace LOICollection::data {
    template <>
    struct TypedColumn<LOICollection::server::Plugins::XxxCol> {
        // 必须列数一致；整型→INTEGER，浮点→REAL，其余→TEXT
        using Types = std::tuple<std::string, std::string, long long, long long>;
    };
}

namespace LOICollection::server::Plugins {
    // 第二个模板参数是 schema 版本号，结构变更时递增
    using XxxTable = TypedTable<XxxCol, 1>;
}
```

这就完成了。无需手写任何 `CREATE TABLE`——`open` 会按枚举和亲和性自动建好 side 表。

## 6. 典型读写

```cpp
// 写入（行不存在自动创建）
t.set(uuid, XxxCol::name, player.getRealName());
t.set(uuid, "time", now);          // 字符串字面量列名也可，编译期解析
t.set(uuid, XxxCol::score, 100LL);

// 读取：行不存在返回默认值；拼错列名编译期报错
auto name = t.get<std::string>(uuid, XxxCol::name, "");
name.and_then([&](std::string const& n) -> ll::Expected<void> {
    // 使用 n ...
    return {};
}).or_else(modules::defaultErrorHandler<XxxPlugin>);

// 整行读写（用于表单/编辑器）
auto row = t.getRow(uuid);            // ll::Expected<std::unordered_map<std::string, std::string>>
t.setRow(uuid, { {"name", "Steve"}, {"score", "100"} });

// 存在性、删除、列举
bool exists = t.has(uuid).value_or(false);
t.del(uuid);                          // 软删除
auto keys = t.list();                 // 所有 rowKey
```

> [!IMPORTANT]
> `get<T>` 的默认值 `def` 也是 `T` 类型。`get<long long>` 与 `get<int>` 是**不同**的重载，务必和 schema 里声明的亲和性一致，否则会触发解码错误。

## 7. 条件查询 find

`find` 在 side 表上做 SQL 查询，条件是 `std::pair<Key, std::string>` 列表——注意**值一律是字符串**，框架会按列亲和性自动推断参数类型（整型/浮点能解析就走数值比较，否则按文本）。

```cpp
// 查 name == "Steve" 且 score == "100" 的玩家（And）
auto ids = t.find(FindMode::And, {
    { XxxCol::name, "Steve" },
    { XxxCol::score, "100" },
});

// 查 name == "Steve" 或 name == "Alex" 的玩家（Or）
auto ids2 = t.find(FindMode::Or, {
    { XxxCol::name, "Steve" },
    { XxxCol::name, "Alex" },
});

// 只取第一个
auto first = t.findFirst(FindMode::And, { { XxxCol::name, "Steve" } });

// 投影：把匹配行的某列取出来
auto scores = t.findValues<long long>(XxxCol::score, FindMode::And, { { XxxCol::name, "Steve" } });
```

被查询的列若声明为可索引（`kIndexed` 默认全 true），框架会按需自动建索引（`CREATE INDEX`）并缓存，后续同列查询走索引。

> [!WARNING]
> `find` 的值参数是字符串。即使列是 `INTEGER`，传入 `"100"` 也能正确比较；但传入格式非法的数值字符串会退化为文本比较，结果不可预期——请保证传入的值格式正确。

## 8. 批量事务 tx()

一次 `set` 内部已经是一个事务，但频繁的单单元格写会在每个单元格上各开一次事务，开销可观。**原子地改动多行，或一次改一行多列，请用 `tx()` 取 `Batch`，最后统一 `commit()`。**

```cpp
auto batch = t.tx();
if (!batch) return ll::makeStringError(batch.error().message());
auto& b = batch.value();

b.set(uuid, XxxCol::name, "Steve");
b.set(uuid, XxxCol::score, 200LL);
b.set(other, XxxCol::name, "Alex");

// Batch 内的读能看到本事务的未提交写入
auto justSet = b.get<std::string>(uuid, XxxCol::name, "");

auto ok = b.commit();                 // 一次原子提交
if (!ok) {
    b.rollback();                     // 失败回滚
    return ll::makeStringError(ok.error().message());
}
```

`Batch` 支持 `set` / `get` / `has` / `del` / `commit` / `rollback`；`get` 优先读本事务的待提交缓冲，再回退到已落盘数据。

## 9. 事务、DDL 与并发（重要）

### 9.1 并发模型

库以 `WAL` 模式打开：`busy_timeout=5s` 让写操作在锁竞争时**自动等待**而非立刻失败；读者与写者互不阻塞（写时不阻塞读）。日常读写无需加锁，照常调用即可。

在此之上，连接池还在**池层**做了一次写串行化（`tryAcquireWrite`，默认 5s 超时）：**全库同一时刻只有一个写者**。多个线程同时 `WriteBatch::begin()` 时，只有一个拿到写锁，其余最多等待 5s，而不是各自占一条连接去抢写锁、撞出 `SQLITE_BUSY`（`database is locked`）。读连接（`acquire()`）走的是另一把锁，不受影响，仍然并发。换言之：`WAL + busy_timeout` 解决"单连接内写被其它连接阻塞"，写串行化解决"多写事务各占一条连接互相排斥"——两者叠加，并发写不再丢事务。

### 9.2 DDL、自动提交与事务内的连接归属

SQLite 的 DDL 是**事务性**的：在 `BEGIN` 里执行 `CREATE TABLE` / `DROP TABLE` / `CREATE INDEX` **不会**隐式提交，它们和 DML 一起原子提交或一起回滚。所以"DDL 一律不能在事务里"是错的。真正要守的是下面两条：

1. **少数语句无法在事务内执行**——最典型的是 `VACUUM`（SQLite 直接报 `cannot VACUUM from within a transaction`），以及切换 `journal_mode` 这类 `PRAGMA`。它们必须走自动提交：`repo->exec("VACUUM;")`。
2. **事务开着时不要调用 `repo->exec()`**——`exec()` 是自动提交路径，会从池里取**另一条**连接；而你的事务此时持有写锁，第二条连接会被挡住，最多等 `busy_timeout`（5s）后报 `SQLITE_BUSY`。

事务内的读取（`load` / `records` / `children` / `withQuery` / `metaGet`）会**复用事务自己的连接**，不会再向池子申请第二条，因此不存在"事务内等第二条连接"的死锁。

正确写法——DDL 与 DML 一起，全部走同一条事务连接：

```cpp
auto b = WriteBatch::begin(repo->store());
(*b)->exec("DROP TABLE IF EXISTS t");   // 走事务连接，与下面的 DML 同原子
(*b)->exec("CREATE TABLE t(a INTEGER)");
(*b)->exec("INSERT INTO t VALUES(1)");
(*b)->commit();                         // 一次提交，全成功或全回滚
```

错误写法——事务开着却去用自动提交通道：

```cpp
auto b = WriteBatch::begin(repo->store());
repo->exec("DROP TABLE IF EXISTS t");   // 第二条连接 → 撞上本事务持有的写锁 → SQLITE_BUSY
(*b)->commit();
```

> [!TIP]
> 项目里 `TypedTable::ensureSide`、`ensureColumnIndex` 与 `BlockRepository` 的初始化都遵循这一约定：DDL 与 DML 分属两条路径，各自只用自己那条连接，互不交叉。**你自己的代码也请照此办理**。
>
> `repo->exec("VACUUM;")`（在 `unregistry` 里回收空间）必须在没有打开事务的时候调用。

### 9.3 优先用 tx() 而非逐单元格写

每条 `set` 自带一次事务提交。一次性改多列/多行时务必用 `tx()` 合并成一次提交，既保证原子性又显著降低 I/O。

### 9.4 嵌套事务（SAVEPOINT）

当**当前线程已经持有活跃事务**时，再开一个 `WriteBatch` 会自动**嵌套为 SAVEPOINT**，而不是另起一条连接、再抢一次写锁：

```cpp
auto outer = WriteBatch::begin(repo->store());
// … 在外层事务里又开一个 batch …
auto inner = WriteBatch::begin(repo->store());  // 同一线程已有活跃事务 → 嵌套为 SAVEPOINT
// inner 的改动属于 outer；inner.commit() == RELEASE SAVEPOINT，不结束 outer
// inner.rollback() == ROLLBACK TO SAVEPOINT，只撤销 inner 的改动
```

要点：

- 嵌套发生的前提是"同一线程已持有活跃事务"（活动事务是 `thread_local`，所以**不会**跨线程嵌套）。嵌套 batch 复用外层事务的连接与写锁，**不会**再申请写锁，因此不会与 9.1 的写串行化死锁。
- 嵌套 batch 的 `commit()` 执行 `RELEASE SAVEPOINT`，`rollback()` 执行 `ROLLBACK TO SAVEPOINT`；两者都**不**结束外层事务。
- 若外层事务在嵌套 batch 结束前被提前结束（连接已释放），该 savepoint 随之失效，嵌套 batch 再 `commit`/`rollback` 会安全跳过，不会凭空操作一条已不属于它的连接。

> [!TIP]
> 事务内的读取会返回当前线程持有的事务连接（`activeTransaction()`），所以嵌套与外层始终共享同一条连接——这正是 SAVEPOINT 能生效的前提。不要在事务内用 `repo->exec()` 另开连接（见 9.2）。

### 9.5 进程内缓存与未提交数据

`BlockStore` 在进程内维护 `mBlockCache` / `mNameCache` 两级缓存，跨事务加速读取。为杜绝**幻读**，缓存只装"已提交的可见状态"：

- `mayCache()` 在**当前线程持有活跃事务**时为 `false`。因此事务内的读走事务自己的连接（能看到本事务的未提交写入），但**不会**回填进程缓存。
- 事务**提交**后，线程不再持有事务，后续读正常回填缓存（此时数据已是持久化快照）。
- 事务**回滚**后，未提交的写入**从未进入过缓存**，自然不会留下幽灵行。

> [!IMPORTANT]
> 代价：持有写事务的那个线程，在事务存续期间不享受缓存加速（每次读都直接走库）。这是用一点性能换取"进程缓存永不含未提交数据"的正确性。若你的代码在事务内反复读同一行，请把该读移出事务，或容忍这次未命中。

> [!TIP]
> 这也意味着：事务内读到的未提交数据只在本事务连接内有效，回滚后即消失，绝不会"污染"其它线程、也不会污染本线程事务外的后续读。

## 10. 错误处理

数据层不使用异常（构建选项 `set_exceptions("none")`），每一步都返回 `ll::Expected<T>`：

| 场景 | 写法 |
| --- | --- |
| 有值才继续 | `.and_then([](T v) -> ll::Expected<U> { ... })`（`Expected<void>` 的 lambda 不带参数） |
| 只想转换形态 | `.transform([](T const& v) -> U { ... })` |
| 记录日志后吞掉 | `.or_else(modules::defaultErrorHandler<XxxPlugin>)` |
| 自己收尾 | `.or_else([this](ll::Error e) -> ll::Expected<void> { e.log(*logger); return {}; })` |

`defaultErrorHandler<Module>` 在模块提供 `getShared()->getLogger()` 时替你调用 `e.log(...)`。

> [!IMPORTANT]
> `or_else` 的返回类型必须与链条**当前**的值类型一致。`Batch::commit()` 返回 `Expected<bool>`，配到链尾时要么让 `or_else` 返回 `Expected<bool>`，要么先用 `.transform([](bool) {})` 收成 `Expected<void>`，否则会报 "no matching function for call to `or_else`"。

## 11. 性能与最佳实践

- **批量写用 `tx()`**：避免每行/每列一次事务提交。
- **数值字段声明成 `long long` / `double`**：落 `INTEGER`/`REAL`，比 `TEXT` 更小、可比较、可建索引。
- **被查询的列保持可索引**：默认全列索引；只在 `find` 里用到的列会自动建索引并缓存，无需手管。
- **rowKey 用稳定标识**（玩家 uuid / xuid），不要用会变的名字作主键。
- **别在热路径里 `list()` + 逐个 `get`**：要按条件取数用 `find` / `findValues`，让 SQL 在 side 表上完成筛选。
- **定期 `VACUUM`**：WAL 模式下空间不会自动回收，`unregistry` 时 `repo->exec("VACUUM;")` 一次即可（注意它不能在事务内执行，且调用时本仓库不能有打开的事务）。

## 12. 常见错误码速查

错误统一来自 `BlockError` 类别，日志里以 `BlockError: <message>` 呈现：

| 错误码 | 枚举 | 含义 | 典型成因 |
| --- | --- | --- | --- |
| 1 | `PoolTimeout` | 连接池超时 | 并发写过多、单事务过久、磁盘慢 |
| 2 | `CreateFailed` | 块创建失败 | 底层 SQLite 写失败 |
| 3 | `LoadFailed` | 块读取失败 | 记录损坏 |
| 4 | `NotFound` | 块不存在 | 读取未写入的 rowKey（多数情况用 `def`/ `has` 避免） |
| 5 | `NoConnection` | 无可用连接 | 连接池耗尽 |
| 6 | `CommitFailed` | 事务提交失败 | 事务内执行了 `VACUUM` 等不允许在事务中的语句（见 9.2）；或磁盘满 |
| 7 | `RollbackFailed` | 回滚失败 | 连接已断 |
| 8 | `DuplicateName` | 父节点下重名 | 同一 rowKey 重复 upsert（一般用 upsert 不会出现） |
| 9 | `InvalidState` | 块状态非法 | 对已删除块做非法操作 |
| 10 | `SchemaMismatch` | 表结构版本/列不匹配 | 版本号或类型名与存量 `schema:<表名>` 不一致；切勿手改表结构 |

## 13. 完整示例（一个模块）

把以上串起来：一张表 + 一个模块的 `registry` 中打开、读写、查询与批量提交。

```cpp
// include/server/Plugins/types/xxx/XxxSchema.h
#pragma once
#include "LOICollectionA/data/sqlite/block/TypedTable.h"
namespace LOICollection::server::Plugins {
    using LOICollection::data::TypedTable;
    enum class XxxCol { id, name, time, score };
}
namespace LOICollection::data {
    template <> struct TypedColumn<LOICollection::server::Plugins::XxxCol> {
        using Types = std::tuple<std::string, std::string, long long, long long>;
    };
}
namespace LOICollection::server::Plugins {
    using XxxTable = TypedTable<XxxCol, 1>;
}
```

```cpp
// modules/server/Plugins/XxxPlugin.cpp（节选）
ll::Expected<bool> XxxPlugin::registry() {
    if (!this->mImpl->options.ModuleEnabled)
        return false;

    // 1. 打开表
    return XxxTable::open(*this->mImpl->db, "Xxx")
        .and_then([this](XxxTable table) -> ll::Expected<void> {
            this->mImpl->xxx.emplace(std::move(table));
            return {};
        })
        .and_then([this]() -> ll::Expected<void> {
            auto& t = *this->mImpl->xxx;
            std::string uuid = "player-001";

            // 2. 单写
            t.set(uuid, XxxCol::name, "Steve");
            t.set(uuid, XxxCol::score, 100LL);

            // 3. 条件查询
            auto ids = t.find(FindMode::And, { { XxxCol::name, "Steve" } });
            if (!ids) return ll::makeStringError(ids.error().message());

            // 4. 批量事务
            auto batch = t.tx();
            if (!batch) return ll::makeStringError(batch.error().message());
            auto& b = batch.value();
            b.set(uuid, XxxCol::score, 200LL);
            b.del("player-999");
            auto ok = b.commit();
            if (!ok) { b.rollback(); return ll::makeStringError(ok.error().message()); }

            return {};
        })
        .transform([this]() -> bool {
            this->registeryCommand();
            this->listenEvent();
            return true;
        });
}
```

> [!NOTE]
> 模块未启用时 `registry` 直接返回 `false`；数据库在 `load` 阶段用 `BlockRepository::open` 打开（见 [模块开发指南](./module.md)）。本例聚焦数据层 API，生命周期细节请结合模块指南阅读。

---

如需了解数据层在整体架构中的位置与数据库文件分布，参见 [架构概览](./architecture.md)；模块接入方式见 [模块开发指南](./module.md)。
