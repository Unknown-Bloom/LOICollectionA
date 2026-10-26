# Architecture Overview

> [!NOTE]
> The following content is taken from the code structure of LOICollectionA 1.17.3 and may differ in later versions.

LOICollectionA is a C++ plugin (NativeMod) based on [LeviLamina](https://github.com/LiteLDev/LeviLamina), with an overall **microkernel architecture**: the core only handles configuration loading, service registration, and module scheduling, while all features are implemented independently as **modules**, which can be toggled on demand in the configuration file.

## Plugin Entry and Lifecycle

The plugin entry is located at `src/LOICollectionA/LOICollectionA.cpp`, registered via `LL_REGISTER_MOD`:

```cpp
LL_REGISTER_MOD(LOICollection::A, LOICollection::A::getInstance());
```

`A` inherits `ll::mod::NativeMod` from LeviLamina, and the four stages drive all modules in sequence:

| Stage | Core action | Calls to modules |
| --- | --- | --- |
| `load` | Compute the config version → generate/merge/load `config.json` → register configuration and path services → open `settings.db` → load `gui/permission.json` and register the sandbox permission and script storage services → initialize the language directory | Call module `load()` one by one |
| `enable` | Set the default language → compile LOICollectionAPI scripts → start `ProcSchedulerDriver` → start the LSP according to `LanguageServer.enabled` | Call module `registry()` one by one |
| `disable` | Stop the scheduler and LSP → release all UI | Call module `unregistry()` one by one |
| `unload` | — | Call module `unload()` one by one |

> [!TIP]
> The four stages of a module are not an "on/off" relationship: `load`/`unload` handle the **resource lifecycle** (database, logs, paths), while `registry`/`unregistry` handle **runtime registration** (commands, events, UI). See [Module Development Guide](./module.md) for details.

## Module System

The module framework is located at `src/LOICollectionA/include/` and contains three core classes:

### ModuleBase (Module Base Class)

All modules inherit from `LOICollection::modules::ModuleBase` and need to implement six pure virtual methods:

```cpp
class ModuleBase {
public:
    virtual std::string getName() = 0;                 // module name (registration name)
    virtual ModulePriority getPriority() = 0;          // module priority
    virtual ll::Expected<bool> load() = 0;             // resource loading
    virtual ll::Expected<bool> unload() = 0;           // resource release
    virtual ll::Expected<bool> registry() = 0;         // runtime registration
    virtual ll::Expected<bool> unregistry() = 0;       // runtime unregistration
};
```

### AutoRegister (Auto Registration)

A module only needs to inherit from `modules::AutoRegister<Derived>`; its static initialization automatically calls `ModManager::registry(getShared(), name, priority)` at program startup, with no manual registration needed:

```cpp
class BlacklistPlugin : public std::enable_shared_from_this<BlacklistPlugin>,
                        public modules::ModuleBase,
                        public modules::AutoRegister<BlacklistPlugin> {
    // ...
};
```

### ModManager (Module Manager)

| Method | Description |
| --- | --- |
| `registry(shared_ptr, name, priority)` | Register a module; `priority` defaults to `Normal` |
| `unregistry(name)` | Unregister a module |
| `getModule(name)` | Get a module instance by name |
| `mods()` | Get the list of all module names (sorted by priority) |

The `ModulePriority` enum: `Highest` (0), `High` (1), `Normal` (2), `Low` (3), `Lowest` (4). Priority affects the module load/registration order.

## Service Container and Dependency Injection

Modules do not depend on each other directly; instead, they register and retrieve services by **type + name** through `ServiceProvider` / `ServiceContainer`:

```cpp
// Register
ServiceProvider::getInstance().registerInstance<TService>(instance, name);

// Retrieve
auto svc = ServiceProvider::getInstance().getService<TService>(name);
```

Global services registered at plugin startup:

| Type | Name | Description |
| --- | --- | --- |
| `ReadOnlyWrapper<Config::C_Config>` | `"Config"` | Read-only configuration (see below) |
| `std::string` | `"DataPath"` | Plugin data directory (`plugins/LOICollectionA/data`) |
| `std::string` | `"GuiPath"` | GUI directory (`plugins/LOICollectionA/gui`) |
| `std::string` | `"ConfigPath"` | Configuration directory (`plugins/LOICollectionA/config`) |
| `BlockRepository` | `"SettingsDB"` | Global settings database (`data/settings.db`, block storage) |
| `std::shared_ptr<frontend::sandbox::ScriptPermissionService>` | none | LCUI sandbox permissions: reads `gui/permission.json` and supplies trust levels, capabilities and budget policies |
| `std::shared_ptr<frontend::storage::ScriptStorage>` | none | LCUI script persistent storage (root `data/script`) backing the `storage` / `playerdata` namespaces |

> [!NOTE]
> The configuration is registered as `ReadOnlyWrapper<Config::C_Config>`, so modules can only **read** the configuration and cannot modify it. This is an intentional design: the configuration is only read once at startup, and modifying it at runtime requires restarting the server.

## Configuration System

- The configuration structure is defined in `src/LOICollectionA/ConfigPlugin.h` (nested structure `C_Config` → `C_ServerConfig` → `C_ServerPlugins` / `C_ServerProtableTool`, plus `C_ClientConfig`, `C_ScriptPermission` and `C_LanguageServer`)
- The configuration version is generated by hashing the plugin version number (`SynchronousPluginConfigVersion`)
- When upgrading the plugin, the default configuration in code is **recursively merged** into the existing `config.json` via `MergePatch`: new keys are filled in automatically, while existing keys keep the user's values
- See [Data Files](../md/data.md) for detailed configuration item descriptions

## Directory Structure

```txt
src/LOICollectionA/
├─ LOICollectionA.cpp / .h   # plugin entry (A class)
├─ ConfigPlugin.h / .cpp     # configuration structure definition and loading
├─ base/                     # infrastructure: ServiceContainer, ServiceProvider,
│                            #   ReadOnlyWrapper, LRUKCache, Throttle, ScopeGuard
├─ data/                     # data layer: sqlite/ block storage (BlockStore + WriteBatch +
│                            #   BlockRepository), JsonStorage
├─ frontend/                 # LCUI script engine: Lexer, Parser, SemanticAnalyzer,
│   │                        #   AST, Callback (native binding registry), ir/ (compiler, VM, optimizer)
│   ├─ sandbox/              # script sandbox: ScriptPermission (permission and execution
│   │                        #   budget configuration), SandboxBudget (fuel metering),
│   │                        #   SandboxReport; permissions use a trust-level + capability model
│   ├─ storage/              # script persistence: storage (per script) and playerdata
│   │                        #   (per player + script): paths, capability checks, whole-file cache
│   ├─ stdlib/               # native values and functions: Math/Format, String/Array/Map,
│   │                        #   Observable and UIRawMessage, etc.
│   ├─ lsp/                  # LCUI language service core: Protocol (JSON-RPC framing),
│   │                        #   Analysis (diagnostics and symbols), LanguageServer (dispatch),
│   │                        #   Server (optional TCP transport)
│   └─ builtin/              # built-in script namespaces: mc/server commands, storage,
│                            #   ui/ (form and GUIManager native classes)
├─ include/                  # public header files (distributed with the plugin after installation)
│   ├─ ModuleBase.h / ModManager.h / ModulePriority.h
│   ├─ CallbackUtils.h       # LOICollectionAPI variable registration entry
│   ├─ form/GUIManager.h     # GUI manager
│   ├─ server/Events/        # custom event types (see [Custom Events](./events.md))
│   └─ server/Plugins/       # public interfaces of each module (BlacklistPlugin.h, etc.)
├─ modules/                  # module implementations (client/ and server/ isolated by platform)
├─ utils/                    # utilities: I18nUtils, MathUtils, mc-server toolkit
tests/                       # gtest tests (common/ cross-platform, server/ server-only, client/ reserved and empty)
```

## Dual-Platform Support

The plugin supports both `server` and `client` targets at the same time, distinguished via the `target_type` option in xmake:

- Compile macros: `LL_PLAT_S` (server) / `LL_PLAT_C` (client)
- Source isolation: `modules/server/*` and `modules/client/*` are compiled mutually exclusively by target
- Header isolation: `include/server/*` and `include/client/*` are removed from the distribution manifest by target
- See [Build and Test](./build.md) for build methods

## Data Layer

The data layer is organized into four levels bottom-up; business code only ever touches the topmost one:

```txt
TypedTable<E, Version>        # compile-time columns + versioned schema (the only business entry)
        |  reads / writes / transactions
BlockRepository               # facade: open / fromStore / store() / meta* / exec
        |
BlockStore                    # block-model persistence (dict / block / prop / link / meta)
WriteBatch                    # atomic write transaction
        |
ConnectionPool               # SQLite connection pool (WAL mode)
```

| Level | Location | Responsibility |
| --- | --- | --- |
| `ConnectionPool` | `data/sqlite/connection/` | SQLite connection management: `WAL` + `synchronous=NORMAL` + `busy_timeout=5s` |
| `BlockStore` | `data/sqlite/block/` | Splits data into addressable blocks with incremental key-based access over five system tables: `dict` / `block` / `prop` / `link` / `meta` |
| `WriteBatch` | same | Flushes a batch of changes atomically in one `commit()`, `rollback()` on failure |
| `BlockRepository` | same | Public surface is only: `open` / `fromStore` / `store()` / `metaGet\|Set\|Del` / `exec` |
| `TypedTable` | same | Lifts columns to compile time (declared as an `enum class`); `open` writes `version + type name + column count` to `schema:<table>` and returns `SchemaMismatch` on disagreement |

Every method returns `ll::Expected<T>` so errors compose via `and_then` / `transform` / `or_else`; no exceptions are used. See the "Data Layer" section of the [Module Development Guide](./module.md) for usage, and the complete [SQLite Layer Tutorial](./sqlite.md).

> [!NOTE]
> The full data-layer API, schema definition, conditional queries, transactions, and error-code reference are in the [SQLite Layer Tutorial](./sqlite.md).

> [!WARNING]
> Data-layer constraint: **a few statements such as `VACUUM` cannot run inside a transaction** and must go through `BlockRepository::exec()` (autocommit), while `CREATE` / `DROP TABLE` / `CREATE INDEX` are transactional in SQLite and are safe to place in the same `WriteBatch` as your DML. **Never call `repo->exec()` while a transaction is open**—it takes a second connection and collides with the write lock your transaction holds. Reads inside a transaction reuse the transaction's own connection and never request another one. See section 9 of the tutorial.

### Database Files

| File | Owner | Contents |
| --- | --- | --- |
| `data/settings.db` | global | registered as the `SettingsDB` service; shared tables of Wallet, Language, PvP, Notice, Chat, Tpa, Market and Statistics live here |
| `data/blacklist.db` | Blacklist | banned words and ban list |
| `data/mute.db` | Mute | mute list |
| `data/chat.db` | Chat | chat logs and interception rules |
| `data/market.db` | Market | market / store / wanted / auction / quotes |
| `data/statistics.db` | Statistics | player statistics |
| `data/tpa.db` | Tpa | teleport requests |
| `data/behaviorevent.db` | BehaviorEvent | behavior event logs |

`Config`, `Notice` (`notice.json`) and `Cdk` (`cdk.json`) do not use block storage; they still use `JsonStorage` and can be edited directly.

> [!WARNING]
> `BlockRepository::open` performs **no migration**: it only creates the five block-model system tables inside the target SQLite file. Tables written by older versions are neither read nor converted. See [Data Migration](../course/migrate.md) when upgrading across versions.

## Scripting VM

LCUI scripts are compiled in `frontend/ir/` into three-address MIR and executed directly by `VM`. Object access and field caching are designed around two ideas:

### Object Model and Field Layout

Each `Object` holds an **immutable** `FieldLayout` (an ordered map from field names to slot indices). Class fields known at compile/construction time occupy contiguous slots, so `slotOf(name)` is O(1). Dynamically written fields fall back to a `spill` hash map, keeping undeclared fields accessible.

### Field Inline Cache

Field names are constant per instruction. `VM` keeps a fixed-size table indexed directly by instruction address, whose entries record `(instruction, layout, slot)`: the same instruction on the same shape hits immediately, and a shape change or slot rearrangement invalidates and recomputes the entry. The cache is private to each VM, so it no longer goes through a shared `ChunkCache` hash lookup and needs no cross-thread synchronization.

Name-based dispatch (`CALL_METHOD_BY_NAME`) caches the resolved ordinal keyed by `(instruction, classIndex)`, and host function calls cache the split of `ns::func` at the instruction, avoiding repeated linear lookup and slice allocation.

### Hidden Class / Shape Transitions

`extendShape(base, name)` memoizes an immutable extended `FieldLayout` keyed by `(base, name)`, so objects of the same kind share one shape. `Object::addField()` migrates dynamically added fields into real slots (instead of per-object `spill`), letting objects with the same dynamic shape converge to one `layout` and thus stabilizing the PIC across objects. Existing fields are written in place, preserving semantics.

> [!NOTE]
> The shape-transition table is protected by a mutex: the scheduler may pump tasks on several threads, and the same shape can be registered concurrently. Field and dispatch caches are VM-private state, read and written only by the thread that owns the VM.

### Cycle Collection (`CycleCollector`)

Script objects, arrays, and closures can reference each other through `shared_ptr`, forming cycles whose destructors never run. Each VM owns a collector that registers only containers that have **received a reference write**: it finds externally reachable roots using "reference count minus internal in-degree", detaches the outgoing edges of the rest, and lets destruction release their budget slots. Node indices are embedded in the objects, so a scan allocates no hash table.

`NativeHandle::trace(ValueVisitor&)` lets native handles expose the script values they hold (such as a form's `show` callback or the wrapped value of `CtxValue`) to the same scan; the default implementation is empty, so handles that hold no script values need no override. Script values captured by `std::function` remain invisible, and cycles through them still leak — this is a deliberately preserved boundary.

Heaps are shared per script id through `ScriptHeapRegistry`: every VM of the same script (repeated open by one player, instances of different players, arrays held by the host) lands on the same graph, so cross-VM cycles are visible. Heaps created by the registry are shared heaps, and registration, barriers, and advancement all take the same mutex; a VM's own heap is not shared and takes no lock. Advancement (`step`/`collect`) additionally requires that there is exactly one executor (`exclusive`): when the same script runs concurrently, collection is skipped rather than scanning fields another thread is rewriting, and missed cycles are collected in a later exclusive window.

Collection advances in stages (`Build -> Index -> Mark -> Sweep`); `step(budget)` processes a fixed number of nodes, driven by the interpreter every 1024 instructions, so a single tick never stalls on a full-heap scan. If a reference is written during execution, the VM marks both the overwritten old value and the new value as roots (write barrier), ensuring live objects are not collected during incrementality; the barrier only retains extra objects and never kills live ones.

### Optimizer Pipeline

The optimizer repeatedly runs `ConstantPropagation -> ConstantFold -> DeadStore -> DeadCode -> CSE -> DeadValue` per basic block until a round makes no changes; it then runs LICM and branch fusion followed by another convergence pass. `ConstantPropagation` performs forward dataflow analysis over the CFG and intersects register, slot, and variable-name states at joins; self-loop blocks are iterated separately until stable so loop headers are not mistaken for constants. Besides full constant folding, `ConstantFold` reduces numeric identities (`x + 0`, `x * 1`, `x / 1`, `x ^ 1`) by replacing the instruction with `MOVE` only when the result type is known to be `int` / `float`, matching runtime promotion.

### Bytecode Package (`.lcp`)

A package written by `MirSerializer` consists of `abiFingerprint` (the native export surface), source/import hashes, `layoutFingerprint` (the class table of this package), and the MIR body. `layoutFingerprint` is computed from the in-memory chunk when writing and recomputed from the decoded result when reading; a mismatch rejects the package — it catches self-consistent but semantically wrong packages caused by writer/reader drift (e.g. a class-table field not being written). On a version mismatch or checksum failure, the loader falls back to recompiling the source and overwrites the package.
