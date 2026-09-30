# SQLite Layer Tutorial

> [!NOTE]
> This article is based on the LOICollectionA 1.17.0 data layer (`src/LOICollectionA/data/sqlite/`). Later versions may differ. The examples follow the project's existing block-storage (BlockStore) and typed-table (TypedTable) conventions and can be dropped straight into module development.

The data layer is the foundation of all persistence in LOICollectionA. It adds a **block-storage abstraction** on top of SQLite, then lifts column names to **compile time** with templates, so business code reads and writes in a type-safe way—without hand-writing SQL or worrying about schema drift.

This tutorial is for developers who need to persist data inside a module. It goes "concepts → API reference → practice". If you just want to get started, jump to [Declaring a Table](#5-declaring-a-table-schema) and the [Complete Example](#13-complete-example-a-module).

## 1. What problem it solves

Plain SQLite usage has two long-standing pain points:

- **Schema drift**: hand-written `CREATE TABLE` and "which column is which" in business code are kept in sync by memory; adding or removing a column shifts everything and breaks silently.
- **SQL injection & string concatenation**: runtime SQL string building is error-prone and a security risk.

LOICollectionA's answer:

- A **block model** that splits data into addressable "blocks", decoupling schema from business data.
- `TypedTable<E, Version>` turns "columns" into an enum; column names are resolved at **compile time**, so a typo is a compile error.
- Cells are uniformly encoded/decoded (`CellCodec`); `string` / integer / floating / `bool` map automatically to SQLite affinity.
- Everything returns `ll::Expected<T>`, composed with `and_then` / `transform` / `or_else`; no exceptions.

## 2. Layered structure

```txt
TypedTable<E, Version>        # compile-time columns + versioned schema (the only business entry)
        |  read / write / transaction
BlockRepository               # facade: open / fromStore / store() / meta* / exec
        |
BlockStore                    # block-model persistence (dict / block / prop / link / meta)
WriteBatch                    # atomic write transaction
        |
ConnectionPool               # SQLite connection pool (WAL mode)
```

| Layer | Location | Responsibility |
| --- | --- | --- |
| `ConnectionPool` | `data/sqlite/connection/` | SQLite connection management: `journal_mode=WAL` + `synchronous=NORMAL` + `temp_store=MEMORY` + `cache_size=8096` + `busy_timeout=5000` |
| `BlockStore` | `data/sqlite/block/` | Splits data into addressable "blocks", reads/writes by key; five system tables: `dict` / `block` / `prop` / `link` / `meta` |
| `WriteBatch` | same | Flushes a batch of changes atomically in one `commit()`; `rollback()` on failure |
| `BlockRepository` | same | Public surface is only: `open` / `fromStore` / `store()` / `metaGet\|Set\|Del` / `exec` |
| `TypedTable` | same | Lifts columns to compile time; `open` writes `version + type name + column count + index mask` to `schema:<table>` and returns `SchemaMismatch` on disagreement |

Business code **should only touch `TypedTable`** (rare cases touch `BlockRepository` directly).

## 3. Core concepts

### 3.1 The block model

`BlockStore` abstracts the whole database as a tree of nodes keyed by `BlockId`:

- `block`: node body (id, parent, kind, name, state, payload)
- `dict`: `name → BlockId` index under a parent; this is how a rowKey is located
- `prop`: node attributes (used for the queryable "mirror columns")
- `link`: directed edges between nodes
- `meta`: database-level key/value (schema fingerprint and side fingerprint live here)

Each logical "row" of a `TypedTable` is a child block named by its `rowKey` (usually a player uuid or name). A cell may live in `block.payload` or be mirrored into the **side table** `col_<rootId>` for SQL queries. The side table is maintained automatically—you never deal with it directly.

> [!TIP]
> `block.payload` is the block's raw value (any JSON-ish cell set); the side table is an extra mirror it builds for queryable columns. Reads of indexed columns hit the side table; reads of non-indexed columns fall back to payload—the switch is automatic.

### 3.2 Columns are an enum

All columns of a table are declared as a single `enum class`, and the **enum order is the physical column id**:

```cpp
enum class XxxCol { id, name, time };   // id=0, name=1, time=2
```

Therefore:

- **New columns may only be appended at the end of the enum**, otherwise they misalign with already-persisted data.
- A column may be an enum value (`XxxCol::name`) or a string literal (`"name"`)—the literal is resolved to the enum at **compile time**, so a typo fails to compile.
- For a runtime `std::string` column name, use `parseColumn()`; do not use the literal form.

### 3.3 Types and affinity

A column's physical affinity is derived from `TypedColumn<E>::Types` (a `std::tuple`) whose length must equal the enum size:

| C++ type | SQLite affinity |
| --- | --- |
| Integral (`int` / `long long` / …) | `INTEGER` |
| Floating (`double` / `float`) | `REAL` |
| `bool` | `INTEGER` (`1` / `0`) |
| Everything else (`std::string`, custom types via `CellCodec`) | `TEXT` |

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
> Declare numeric fields as `long long` / `double` rather than `std::string`: the former become `INTEGER`/`REAL`—smaller, comparable, and usable for range queries—while the latter are always `TEXT` and compare lexicographically.

### 3.4 Schema fingerprint and version

`open` writes a fingerprint to `schema:<table>`: `version \x1e typeName \x1e columnCount \x1e indexMask`.

- Version or type name mismatch → immediately returns `BlockError::SchemaMismatch` (refuses to open, never silently misreads).
- Column count or index mask change → marked `schemaDirty`, the side table is rebuilt and existing payloads backfilled automatically (**it does not migrate older-version data, only rebuilds the current blocks' mirror**).

The version number is your manual upgrade knob: when the structure changes, bump it; `open` safely rebuilds the mirror instead of reading corrupt data.

## 4. Layered API reference

### 4.1 ConnectionPool (internal)

Not used directly by business code. Each database is opened in `WAL` mode with `busy_timeout=5s` so concurrent writes wait instead of failing immediately. Database files live at `plugins/LOICollectionA/data/*.db`.

### 4.2 BlockRepository (facade)

```cpp
#include "LOICollectionA/data/sqlite/block/BlockRepository.h"

// Open (or create) a database; connections default to 4
auto db = BlockRepository::open((dataPath / "xxx.db").string(), 4);
if (!db) return ll::makeStringError(db.error().message());
std::shared_ptr<BlockRepository> repo = std::move(db.value());

// Reuse an existing store (e.g. the global SettingsDB on the same file)
// auto repo = BlockRepository::fromStore(existingStore);

repo->store();                       // returns the underlying BlockStore&
repo->exec("VACUUM;");               // run raw SQL (autocommit, see section 9)
repo->metaGet("sidecol:Xxx");        // ll::Expected<std::optional<std::string>>
repo->metaSet("key", "value");       // ll::Expected<void>
repo->metaDel("key");
```

| Method | Description |
| --- | --- |
| `open(path, connections=4)` | Open (or create) a database, return `shared_ptr<BlockRepository>` |
| `fromStore(store)` | Build a facade from an existing `BlockStore` (share a pool across tables on one file) |
| `store()` | Return the underlying `BlockStore&` |
| `exec(sql)` | Run raw SQL (autocommit) |
| `metaGet/metaSet/metaDel` | Read/write database-level metadata (`TypedTable` stores its schema/side fingerprints here) |

### 4.3 WriteBatch (transaction)

```cpp
#include "LOICollectionA/data/sqlite/block/WriteBatch.h"

auto b = WriteBatch::begin(repo->store());
if (!b) return ll::makeStringError(b.error().message());
auto& tx = *b;

tx.exec("UPDATE ...");                       // raw SQL (DML)
tx.execCells("key", "INSERT ... VALUES(?,?)", params); // parameterized raw SQL
tx.upsertRow(parentId, rowKey);              // write a row (create block if absent)
tx.append(parentId, kind, name, payload);    // append a child block
tx.setProp(id, key, ival / rval / tval);     // set a node attribute
tx.control(id, BlockLifecycle::Deleted);     // mark deleted (soft delete)

auto ok = tx.commit();                       // ll::Expected<bool>
if (!ok) { tx.rollback(); /* ... */ }
```

| Method | Description |
| --- | --- |
| `begin(store)` | Begin a transaction, return `unique_ptr<WriteBatch>` |
| `exec(sql)` / `execCells(key, sql, params)` | Run SQL (with / without parameters) |
| `upsertRow(parent, name, payload={})` | Write a row, return `BlockId` |
| `append(parent, kind, name, payload)` | Append a child block, return `BlockId` |
| `setProp(id, key, ...)` | Set a node attribute (three overloads: int / double / text) |
| `control(id, lifecycle)` | Change node state (e.g. `Deleted` soft delete) |
| `commit()` / `rollback()` | Commit (returns `Expected<bool>`) / rollback |

> [!WARNING]
> `WriteBatch` is **non-copyable and non-movable**. It owns a `StorageTransaction` and may only be used as a `unique_ptr` within a scope. Business code usually does not create it directly—`TypedTable::tx()` already wraps it in a `Batch`.

### 4.4 TypedTable (business entry)

```cpp
#include "LOICollectionA/data/sqlite/block/TypedTable.h"
using LOICollection::data::TypedTable;
using LOICollection::data::FindMode;

// Open (or create) a table
auto table = XxxTable::open(*repo, "Xxx");
if (!table) return ll::makeStringError(table.error().message());
XxxTable t = std::move(table.value());
```

`Key` is the unified column parameter type: either an enum value or a string literal, with the literal resolved at compile time.

| Method | Description |
| --- | --- |
| `open(repo, name)` | Open (or create) a table and return a handle; the handle only holds a `repo` reference and is freely movable |
| `get<T>(row, col, def)` | Read one cell; returns `def` if the row is absent (string column name literals only) |
| `set<T>(row, col, value)` | Write one cell; creates the row if absent |
| `getRow(row)` / `setRow(row, map)` | Read/write a whole row (a `column name → value` map) |
| `has(row)` / `del(row)` / `list()` | Existence check, delete, list all rowKeys |
| `find(FindMode, conds)` | Query matching rowKeys (`FindMode::And` / `FindMode::Or`) |
| `findFirst(FindMode, conds)` | Same, but take only the first |
| `findValues<T>(col, FindMode, conds)` | Project a single column's values |
| `tx()` | Begin a batched write transaction, return `Batch` |
| `parseColumn(s)` | Resolve a runtime string to an enum column (static) |
| `clearTypedTable(repo, name)` | Clear all rows of a table (free function) |

## 5. Declaring a table (Schema)

All columns are declared centrally in `src/LOICollectionA/include/server/Plugins/types/<module>/<Module>Schema.h`, in three parts: **enum + type specialization + alias**.

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
        // Must match column count; integral→INTEGER, floating→REAL, else→TEXT
        using Types = std::tuple<std::string, std::string, long long, long long>;
    };
}

namespace LOICollection::server::Plugins {
    // The second template parameter is the schema version; bump it on structural change
    using XxxTable = TypedTable<XxxCol, 1>;
}
```

That's it. No hand-written `CREATE TABLE`—`open` builds the side table automatically from the enum and affinity.

## 6. Typical read/write

```cpp
// Write (row auto-created if absent)
t.set(uuid, XxxCol::name, player.getRealName());
t.set(uuid, "time", now);          // string-literal column name also works, compile-time resolved
t.set(uuid, XxxCol::score, 100LL);

// Read: returns default if row absent; typo on column name is a compile error
auto name = t.get<std::string>(uuid, XxxCol::name, "");
name.and_then([&](std::string const& n) -> ll::Expected<void> {
    // use n ...
    return {};
}).or_else(modules::defaultErrorHandler<XxxPlugin>);

// Whole-row read/write (for forms/editors)
auto row = t.getRow(uuid);            // ll::Expected<std::unordered_map<std::string, std::string>>
t.setRow(uuid, { {"name", "Steve"}, {"score", "100"} });

// Existence, delete, list
bool exists = t.has(uuid).value_or(false);
t.del(uuid);                          // soft delete
auto keys = t.list();                 // all rowKeys
```

> [!IMPORTANT]
> The default `def` of `get<T>` is also of type `T`. `get<long long>` and `get<int>` are **different** overloads—keep them consistent with the column's declared affinity, or you'll hit a decode error.

## 7. Conditional query (find)

`find` runs a SQL query on the side table; conditions are a `std::vector<std::pair<Key, std::string>>`—note that **values are always strings**, and the framework infers the parameter type from the column's affinity (numeric if parseable, otherwise text).

```cpp
// Find players where name == "Steve" AND score == "100" (And)
auto ids = t.find(FindMode::And, {
    { XxxCol::name, "Steve" },
    { XxxCol::score, "100" },
});

// Find name == "Steve" OR name == "Alex" (Or)
auto ids2 = t.find(FindMode::Or, {
    { XxxCol::name, "Steve" },
    { XxxCol::name, "Alex" },
});

// Take only the first
auto first = t.findFirst(FindMode::And, { { XxxCol::name, "Steve" } });

// Project: pull one column out of the matched rows
auto scores = t.findValues<long long>(XxxCol::score, FindMode::And, { { XxxCol::name, "Steve" } });
```

A queried column that is indexable (all columns are indexed by default) gets an index (`CREATE INDEX`) created and cached on demand; later queries on the same column use it.

> [!WARNING]
> The value argument of `find` is a string. Even for an `INTEGER` column, passing `"100"` compares correctly; but passing a malformed numeric string falls back to text comparison with unpredictable results—always pass well-formed values.

## 8. Batch transaction (tx())

A single `set` is already its own transaction, but frequent per-cell writes each open a transaction per cell—costly. **When atomically changing multiple rows, or multiple columns of one row, take a `Batch` via `tx()` and `commit()` once at the end.**

```cpp
auto batch = t.tx();
if (!batch) return ll::makeStringError(batch.error().message());
auto& b = batch.value();

b.set(uuid, XxxCol::name, "Steve");
b.set(uuid, XxxCol::score, 200LL);
b.set(other, XxxCol::name, "Alex");

// Reads inside the Batch see this transaction's uncommitted writes
auto justSet = b.get<std::string>(uuid, XxxCol::name, "");

auto ok = b.commit();                 // one atomic commit
if (!ok) {
    b.rollback();                     // rollback on failure
    return ll::makeStringError(ok.error().message());
}
```

`Batch` supports `set` / `get` / `has` / `del` / `commit` / `rollback`; `get` prefers the pending buffer, then falls back to persisted data.

## 9. Transactions, DDL, and concurrency (important)

### 9.1 Concurrency model

Databases open in `WAL` mode: `busy_timeout=5s` makes writes **wait** under lock contention instead of failing immediately; readers and writers don't block each other (reads are not blocked by writes). Normal read/write needs no locking—just call as usual.

On top of that, the connection pool adds a second layer of write serialization at the pool level (`tryAcquireWrite`, 5s timeout by default): **only one writer exists for the whole database at any moment**. When several threads call `WriteBatch::begin()` at once, only one takes the write lock and the others wait up to 5s, rather than each grabbing a connection and racing for the write lock until one of them drops with `SQLITE_BUSY` (`database is locked`). Read connections (`acquire()`) use a separate lock and stay concurrent. In short: `WAL + busy_timeout` copes with a single connection's writes being blocked by another connection, while write serialization prevents multiple write transactions from occupying separate connections and mutually excluding each other—together, concurrent writes no longer lose transactions.

### 9.2 DDL, autocommit, and which connection a transaction owns

SQLite's DDL is **transactional**: `CREATE TABLE` / `DROP TABLE` / `CREATE INDEX` inside a `BEGIN` do **not** implicitly commit—they commit or roll back atomically together with your DML. So "never run DDL in a transaction" is simply wrong. What you actually have to respect is:

1. **A few statements cannot run inside a transaction**—most notably `VACUUM` (SQLite reports `cannot VACUUM from within a transaction`), and `PRAGMA`s that switch `journal_mode`. Those must take the autocommit path: `repo->exec("VACUUM;")`.
2. **Don't call `repo->exec()` while a transaction is open**—`exec()` is the autocommit path and takes a **second** connection from the pool, while your transaction holds the write lock; that second connection is blocked and reports `SQLITE_BUSY` after at most `busy_timeout` (5s).

Reads inside a transaction (`load` / `records` / `children` / `withQuery` / `metaGet`) **reuse the transaction's own connection** and never request another one, so a transaction can never deadlock waiting for a second connection.

Right—DDL and DML together, all on the same transaction connection:

```cpp
auto b = WriteBatch::begin(repo->store());
(*b)->exec("DROP TABLE IF EXISTS t");   // on the transaction connection, atomic with the DML below
(*b)->exec("CREATE TABLE t(a INTEGER)");
(*b)->exec("INSERT INTO t VALUES(1)");
(*b)->commit();                         // one commit: all or nothing
```

Wrong—using the autocommit channel while a transaction is open:

```cpp
auto b = WriteBatch::begin(repo->store());
repo->exec("DROP TABLE IF EXISTS t");   // second connection → hits the write lock this transaction holds → SQLITE_BUSY
(*b)->commit();
```

> [!TIP]
> Within the project, `TypedTable::ensureSide`, `ensureColumnIndex`, and `BlockRepository`'s initialization all follow this rule: DDL and DML stay on their own path, each using only its own connection, never crossing. **Follow it in your own code.**
>
> `repo->exec("VACUUM;")` (reclaiming space in `unregistry`) must be called while no transaction is open on that repository.

### 9.3 Prefer tx() over per-cell writes

Each `set` carries its own transaction commit. When changing many columns/rows at once, merge them into one commit with `tx()`—it guarantees atomicity and cuts I/O significantly.

### 9.4 Nested transactions (SAVEPOINT)

When the **current thread already holds an active transaction**, opening another `WriteBatch` automatically **nests as a SAVEPOINT** instead of taking a second connection and racing for the write lock again:

```cpp
auto outer = WriteBatch::begin(repo->store());
// … open another batch inside the outer transaction …
auto inner = WriteBatch::begin(repo->store());  // thread already has an active txn → nested as SAVEPOINT
// inner's changes belong to outer; inner.commit() == RELEASE SAVEPOINT, does not end outer
// inner.rollback() == ROLLBACK TO SAVEPOINT, undoes only inner's changes
```

Key points:

- Nesting only happens when "the same thread already holds an active transaction" (the active transaction is `thread_local`, so it never nests across threads). The nested batch reuses the outer transaction's connection and write lock and **does not** request the write lock again, so it can't deadlock with the pool-level serialization in 9.1.
- The active transaction is tracked **per pool** (a stack per pool), not as a single global slot: one thread can hold open transactions on two different databases at once (e.g. `market.db` and `settings.db`) without interfering—opening a transaction on one pool never breaks another pool's connection affinity (I1) or cache guard (I3).
- A nested batch's `commit()` runs `RELEASE SAVEPOINT` and `rollback()` runs `ROLLBACK TO SAVEPOINT`; neither ends the outer transaction.
- If the outer transaction is ended prematurely (connection released) before the nested batch closes, that savepoint is gone too; the nested batch's later `commit`/`rollback` safely skips rather than operating on a connection it no longer owns.

> [!TIP]
> A read inside a transaction returns the transaction connection the current thread holds (`activeTransaction()`), so the nested and outer batches always share one connection—that is exactly what makes SAVEPOINT work. Don't open a second connection with `repo->exec()` inside a transaction (see 9.2).

### 9.5 In-process cache and uncommitted data

`BlockStore` keeps two process-wide caches, `mBlockCache` / `mNameCache`, to speed up reads across transactions. To rule out **phantom reads**, the cache only ever holds *committed-visible* state:

- `mayCache()` is `false` while the **current thread holds an active transaction**. So a read inside a transaction goes through the transaction's own connection (it sees the transaction's own uncommitted writes) but does **not** back-fill the process cache.
- After the transaction **commits**, the thread no longer holds a transaction, and subsequent reads populate the cache normally (the data is now a persisted snapshot).
- After the transaction **rolls back**, the uncommitted writes were **never in the cache** to begin with, so no ghost rows can linger.

> [!IMPORTANT]
> The cost: the thread holding a write transaction gets no cache acceleration for the duration of that transaction (every read goes straight to the database). This trades a little performance for the guarantee that the process cache never contains uncommitted data. If your code reads the same row repeatedly inside a transaction, move that read out of the transaction, or accept the miss.

> [!TIP]
> This also means: uncommitted data read inside a transaction is only valid on that transaction's connection and vanishes on rollback—it can never "poison" other threads, nor the same thread's reads taken outside the transaction.

## 10. Error handling

The data layer uses no exceptions (build option `set_exceptions("none")`); every step returns `ll::Expected<T>`:

| Scenario | Writing |
| --- | --- |
| Continue only if value present | `.and_then([](T v) -> ll::Expected<U> { ... })` (lambda for `Expected<void>` takes no argument) |
| Just transform the shape | `.transform([](T const& v) -> U { ... })` |
| Log and swallow | `.or_else(modules::defaultErrorHandler<XxxPlugin>)` |
| Handle yourself | `.or_else([this](ll::Error e) -> ll::Expected<void> { e.log(*logger); return {}; })` |

`defaultErrorHandler<Module>` calls `e.log(...)` for you when the module provides `getShared()->getLogger()`.

> [!IMPORTANT]
> `or_else`'s return type must match the chain's **current** value type. `Batch::commit()` returns `Expected<bool>`; at the chain tail, either let `or_else` return `Expected<bool>`, or first `.transform([](bool) {})` it down to `Expected<void>`—otherwise you'll get "no matching function for call to `or_else`".

## 11. Performance and best practices

- **Batch writes with `tx()`**: avoid one transaction commit per row/column.
- **Declare numeric fields as `long long` / `double`**: they become `INTEGER`/`REAL`, smaller, comparable, and indexable—unlike `TEXT`.
- **Keep queried columns indexable**: all columns are indexed by default; columns used only in `find` get indexed and cached automatically—no manual work.
- **Use a stable rowKey** (player uuid / xuid); don't use a mutable name as the primary key.
- **Don't `list()` + `get` in a hot path**: to filter by condition, use `find` / `findValues` and let SQL do the work on the side table.
- **Vacuum periodically**: under WAL, space isn't reclaimed automatically; `repo->exec("VACUUM;")` once in `unregistry` suffices (it cannot run inside a transaction, so no transaction may be open on that repository).

## 12. Common error codes

Errors come from the `BlockError` category and appear in logs as `BlockError: <message>`:

| Code | Enum | Meaning | Typical cause |
| --- | --- | --- | --- |
| 1 | `PoolTimeout` | Connection pool timeout | Too many concurrent writes, a transaction held too long, slow disk |
| 2 | `CreateFailed` | Block creation failed | Underlying SQLite write failure |
| 3 | `LoadFailed` | Block load failed | Corrupted record |
| 4 | `NotFound` | Block not found | Reading an unwritten rowKey (usually avoided with `def` / `has`) |
| 5 | `NoConnection` | No available connection | Pool exhausted |
| 6 | `CommitFailed` | Transaction commit failed | A statement that cannot run inside a transaction, e.g. `VACUUM` (see 9.2); or disk full |
| 7 | `RollbackFailed` | Rollback failed | Connection already dropped |
| 8 | `DuplicateName` | Duplicate name under parent | Re-upserting the same rowKey (rare with upsert) |
| 9 | `InvalidState` | Invalid block state | Illegal operation on a deleted block |
| 10 | `SchemaMismatch` | Table schema version/columns mismatch | Version or type name disagrees with stored `schema:<table>`; never hand-edit the schema |

## 13. Complete example (a module)

Stringing it together: a table + opening, reading/writing, querying, and batch-committing in a module's `registry`.

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
// modules/server/Plugins/XxxPlugin.cpp (excerpt)
ll::Expected<bool> XxxPlugin::registry() {
    if (!this->mImpl->options.ModuleEnabled)
        return false;

    // 1. Open the table
    return XxxTable::open(*this->mImpl->db, "Xxx")
        .and_then([this](XxxTable table) -> ll::Expected<void> {
            this->mImpl->xxx.emplace(std::move(table));
            return {};
        })
        .and_then([this]() -> ll::Expected<void> {
            auto& t = *this->mImpl->xxx;
            std::string uuid = "player-001";

            // 2. Single write
            t.set(uuid, XxxCol::name, "Steve");
            t.set(uuid, XxxCol::score, 100LL);

            // 3. Conditional query
            auto ids = t.find(FindMode::And, { { XxxCol::name, "Steve" } });
            if (!ids) return ll::makeStringError(ids.error().message());

            // 4. Batch transaction
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
> When a module is disabled, `registry` returns `false` immediately; the database is opened in `load` via `BlockRepository::open` (see the [Module Development Guide](./module.md)). This example focuses on the data-layer API—combine it with the module guide for the lifecycle details.

---

For the data layer's place in the overall architecture and the database file layout, see [Architecture Overview](./architecture.md); for module integration, see [Module Development Guide](./module.md).
