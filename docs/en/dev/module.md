# Module Development Guide

> [!NOTE]
> The following content uses `BlacklistPlugin` (the blacklist module) as an example, taken from the code structure of LOICollectionA 1.17.0; it may differ in later versions.

This article explains how to develop a C++ module for LOICollectionA. Before getting started, please read [Architecture Overview](./architecture.md) to learn about the module framework and the service container.

## Creating a Module

A module consists of a **public header file** (`include/server/Plugins/XxxPlugin.h`) and an **implementation file** (`modules/server/Plugins/XxxPlugin.cpp`).

### 1. Define the Public Class

```cpp
// include/server/Plugins/XxxPlugin.h
#pragma once

#include <memory>
#include <string>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"
#include "LOICollectionA/include/ModuleBase.h"
#include "LOICollectionA/include/ModManager.h"

namespace LOICollection::server::Plugins {
    class XxxPlugin : public std::enable_shared_from_this<XxxPlugin>,
                      public modules::ModuleBase,
                      public modules::AutoRegister<XxxPlugin> {
    public:
        LOICOLLECTION_A_NDAPI static std::shared_ptr<XxxPlugin> getShared(); // Singleton

    public:
        LOICOLLECTION_A_NDAPI std::string getName() override;                 // Module name
        LOICOLLECTION_A_NDAPI modules::ModulePriority getPriority() override; // Priority

        LOICOLLECTION_A_API   ll::Expected<bool> load() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unload() override;
        LOICOLLECTION_A_API   ll::Expected<bool> registry() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unregistry() override;

    private:
        XxxPlugin();

        struct Impl;                                    // Pimpl idiom
        std::unique_ptr<Impl> mImpl;
    };
}
```

> [!TIP]
> `LOICOLLECTION_A_API` / `LOICOLLECTION_A_NDAPI` are export macros (`__declspec(dllexport/dllimport)` on Windows). Public methods must be annotated with them; otherwise the symbols cannot be found during external linking.

### 2. Implement the Singleton and Lifecycle

```cpp
// modules/server/Plugins/XxxPlugin.cpp
namespace LOICollection::server::Plugins {
    struct XxxPlugin::Impl {
        std::shared_ptr<BlockRepository> db;
        std::optional<XxxTable> xxx;              // Typed table handle
        std::shared_ptr<ll::io::Logger> logger;
        ReadOnlyWrapper<Config::C_Xxx> options;   // Module configuration (read-only)
        std::filesystem::path dataPath;
        std::atomic<bool> mRegistered{ false };   // Registration status flag
    };

    std::shared_ptr<XxxPlugin> XxxPlugin::getShared() {
        static std::shared_ptr<XxxPlugin> instance(new XxxPlugin());
        return instance;
    }

    std::string XxxPlugin::getName() { return "Xxx"; }

    modules::ModulePriority XxxPlugin::getPriority() { return modules::ModulePriority::Normal; }

    ll::Expected<bool> XxxPlugin::load() {
        // Do not initialize when the module is disabled (load is still called, but only minimal resource preparation is done)
        auto& config = ServiceProvider::getInstance()
            .getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get();
        if (!config.ServerConfig.Plugins.Xxx.ModuleEnabled)
            return false;

        auto mDataPath = std::filesystem::path(ServiceProvider::getInstance()
            .getService<std::string>("DataPath")->data());
        this->mImpl->logger = ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA");
        this->mImpl->options = config.ServerConfig.Plugins.Xxx;  // Copy module configuration (read-only)

        // Initialize the module's own database (block storage)
        auto localDb = BlockRepository::open((mDataPath / "xxx.db").string(), 4);
        if (!localDb)
            return ll::makeStringError(localDb.error().message());
        this->mImpl->db = std::move(localDb.value());
        return true;
    }

    ll::Expected<bool> XxxPlugin::registry() {
        // Skip directly when the module is disabled (no command/event/UI registration)
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        // Open table -> register UI -> register commands and events
        return XxxTable::open(*this->mImpl->db, "Xxx")
            .and_then([this](XxxTable table) -> ll::Expected<void> {
                this->mImpl->xxx.emplace(std::move(table));
                return this->registeryUI();
            }).transform([this]() -> bool {
                this->registeryCommand();
                this->listenEvent();
                this->mImpl->mRegistered.store(true, std::memory_order_release);
                return true;
            });
    }

    ll::Expected<bool> XxxPlugin::unregistry() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        this->unlistenEvent();
        return this->mImpl->db->exec("VACUUM;")
            .transform([this]() -> bool {
                this->mImpl->mRegistered.store(false, std::memory_order_release);
                return true;
            });
    }

    ll::Expected<bool> XxxPlugin::unload() {
        this->mImpl->xxx.reset();
        this->mImpl->db.reset();
        this->mImpl->logger.reset();
        this->mImpl->options = {};
        if (this->mImpl->mRegistered.load(std::memory_order_acquire))
            this->unlistenEvent();
        return true;
    }
}
```

### 3. Responsibilities of Each Lifecycle Stage

| Stage | Responsibility | Notes |
| --- | --- | --- |
| `load` | Obtain services, initialize the database and logger, cache paths | Only prepare resources; do not register any runtime behavior |
| `registry` | Check `ModuleEnabled` → open table → register UI/commands/events | Return `false` to skip when the plugin is not enabled |
| `unregistry` | Unregister events, clean up the database (e.g. `VACUUM`) | Strictly corresponds to registry |
| `unload` | Release all resources | Unregister first if still in the registered state |

> [!WARNING]
> `load` runs every time the plugin is loaded, while `registry` only executes when the plugin is `enable`d. If the module is not enabled (`ModuleEnabled: false`), `registry` returns `false`, but `load` still runs — do not register commands or events in `load`.

## Reading Configuration

The configuration is obtained through the `ReadOnlyWrapper<Config::C_Config>` service and can only be read:

```cpp
auto& config = ServiceProvider::getInstance()
    .getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get();
bool enabled = config.ServerConfig.Plugins.Mute;  // Read the Mute module switch

// Usually copy the module's own configuration substructure in load
this->mImpl->options = config.ServerConfig.Plugins.Blacklist;
```

### Adding Configuration Options

Just add members to the corresponding struct in `src/LOICollectionA/ConfigPlugin.h`; **default values are required**:

```cpp
struct C_Xxx {
    bool ModuleEnabled = false;       // Module switch (conventionally the first field)
    std::string TargetScoreboard = "money";
    int Limit = 10;
};
```

On the next plugin startup, `MergePatch` automatically merges the new fields into the player's `config.json`, while existing fields are preserved.

> [!WARNING]
> Configuration key names may only use `English`, `numbers` and `underscores`; do not use Chinese.

## Data Layer

A module persists through `BlockRepository` (the block-storage facade, see [Architecture](./architecture.md)), but business code should not talk to it directly — columns are **compile-time** information, expressed as `TypedTable`.

### Declaring a table

Every table's columns are declared centrally in `src/LOICollectionA/include/server/Plugins/TableSchema.h`; columns can only be added or removed there, never misspelled at a call site:

```cpp
namespace LOICollection::server::Plugins {
    enum class XxxCol { id, name, time };
    using XxxTable = TypedTable<XxxCol, 1>;   // the second parameter is the schema version
}
```

`TypedTable` uses the enumerator order as the physical column id, therefore:

- **Append new columns at the end of the enum.** Inserting one in the middle shifts every stored column.
- Bump the version when the layout changes. `open` records a `schema:<table>` fingerprint; a mismatch in version, type name or column count returns `BlockError::SchemaMismatch`.

### BlockRepository

```cpp
#include "LOICollectionA/data/sqlite/block/BlockRepository.h"

auto db = BlockRepository::open((dataPath / "xxx.db").string(), 4);
if (!db)
    return ll::makeStringError(db.error().message());
this->mImpl->db = std::move(db.value());
```

| Method | Description |
| --- | --- |
| `open(path, connections)` | Open (or create) a database, returning `shared_ptr<BlockRepository>` |
| `exec(sql)` | Execute raw SQL (e.g. `"VACUUM;"`) |
| `store()` | Return the underlying `BlockStore`, which `TypedTable` and `WriteBatch` build on |
| `metaGet/metaSet/metaDel` | Read/write database-level metadata (`TypedTable` stores its schema fingerprint here) |

### TypedTable (Recommended)

`get` / `set` are templates; a cell holds a `string`, an integral, a floating point value or a `bool`. The column argument accepts either an enumerator or a string literal — literals are resolved to enumerators at **compile time**, so a typo fails the build:

| Method | Description |
| --- | --- |
| `open(repo, name)` | Open (or create) the table and return a handle; it only holds a reference to `repo` and is freely movable |
| `get<T>(row, col, def)` | Read a single column, returning `def` when the row is absent |
| `set<T>(row, col, value)` | Write a single column, creating the row when needed |
| `getRow(row)` / `setRow(row, map)` | Whole-row read/write (`column name -> value` mapping) |
| `has(row)` / `del(row)` / `list()` | Test existence, delete, list every key |
| `find(FindMode, conds)` | Query matching keys (`FindMode::And` / `FindMode::Or`) |
| `findFirst(FindMode, conds)` | Same, returning only the first key |
| `findValues<T>(col, FindMode, conds)` | Project one column into a list of values |
| `tx()` | Start a batched write transaction, see below |

```cpp
// Write
this->mImpl->xxx->set(uuid, XxxCol::name, player.getRealName());
this->mImpl->xxx->set(uuid, "time", time);

// Read + chained error handling
this->mImpl->xxx->get<std::string>(uuid, XxxCol::name, "")
    .and_then([this, uuid](const std::string& name) -> ll::Expected<void> {
        // ...
        return {};
    })
    .or_else(modules::defaultErrorHandler<XxxPlugin>);

// Query (And / Or conditions, columns given as enumerators)
auto ids = this->mImpl->xxx->find(FindMode::Or, {
    { XxxCol::name, name },
    { XxxCol::id, uuid }
});
```

> [!TIP]
> To mutate several rows atomically, take a `Batch` from `tx()`: `auto batch = table.tx()`, then `batch->set/get/has/del`, and finally `commit()` or `rollback()`.

> [!WARNING]
> String columns only work as **literals** (`"time"`). For a `std::string` obtained at runtime, use `parseColumn()`.

### Error Handling Conventions

The data layer never throws (the build sets `set_exceptions("none")`); every step returns `ll::Expected<T>`:

| Case | Form |
| --- | --- |
| Continue only with a value | `.and_then([](XxxTable::Row row) -> ll::Expected<U> { ... })` (the lambda takes no parameter for `Expected<void>`) |
| Just reshape the value | `.transform([](const std::string& s) -> U { ... })` |
| Log and swallow the error | `.or_else(modules::defaultErrorHandler<XxxPlugin>)` |
| Handle it yourself | `.or_else([this](ll::Error e) -> ll::Expected<void> { e.log(*this->mImpl->logger); return {}; })` |

`defaultErrorHandler<Module>` calls `e.log(...)` for you when the module exposes `getShared()->getLogger()`.

> [!IMPORTANT]
> The `or_else` result type must match the **current** value type of the chain. `Batch::commit()` yields `Expected<bool>`, so either let `or_else` return `Expected<bool>` or collapse it first with `.transform([](bool) {})` — otherwise the compiler reports "no matching function for call to `or_else`".

### JsonStorage (Simple JSON Files)

Suitable for small-scale configuration data:

```cpp
JsonStorage storage("path/to/file.json");
storage.load().or_else(...);          // Load file
storage.set("key", value);            // Write to memory
storage.save();                       // Save to disk
auto v = storage.get<std::string>("key"); // ll::Expected<T>
```

It also supports `get_ptr` / `set_ptr` (JSON Pointer paths) as well as `remove` / `has` / `keys`.

## Registering Commands

Use LeviLamina's `ll::command` system:

```cpp
void XxxPlugin::registeryCommand() {
    ll::command::CommandHandle& command = ll::command::CommandRegistrar::getInstance(false)
        .getOrCreateCommand("xxx", "LOICollection -> xxx", CommandPermissionLevel::Any, CommandFlagValue::NotCheat | CommandFlagValue::Async);

    command.overload<operation>().text("add").required("Target").optional("Time").execute(
        [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
            CommandSelectorResults<Player> results = param.Target.results(origin);
            if (results.empty())
                return output.error("target not found");

            for (Player*& pl : results) {
                this->addXxx(*pl, param.Time).or_else(modules::defaultErrorHandler<XxxPlugin>);
                output.success("done");
            }
        });
}
```

Here `operation` is the command parameter struct defined in the module (refer to BlacklistPlugin's `struct operation`).

## Listening for Events

Use LeviLamina's `ll::event::EventBus` to listen for vanilla events and module-defined events (`include/server/Events/*`):

```cpp
void XxxPlugin::listenEvent() {
    ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
    this->mImpl->myListener = eventBus.emplaceListener<LOICollection::server::Events::PlayerScoreChangedEvent>(
        [this](LOICollection::server::Events::PlayerScoreChangedEvent& event) -> void {
            // ...
        });
}

void XxxPlugin::unlistenEvent() {
    ll::event::EventBus::getInstance().removeListener(this->mImpl->myListener);
}
```

> [!WARNING]
> The listener handle must be stored in `Impl`; be sure to call `removeListener` in `unregistry` / `unload`, otherwise events may still be triggered on already-freed objects after the module is unloaded.

## Error Handling Conventions

Public methods of a module uniformly return `ll::Expected<T>`, together with **module error codes**:

```cpp
enum class XxxPluginErrorCode : int {
    Invalid = 1,
    NotFound = 2,
    PermissionDenied = 3
};

struct XxxPluginErrorCategory : std::error_category {
    [[nodiscard]] const char* name() const noexcept override { return "XxxPluginError"; }
    [[nodiscard]] std::string message(int ev) const override {
        switch (static_cast<XxxPluginErrorCode>(ev)) {
            case XxxPluginErrorCode::Invalid: return "Plugin is invalid";
            case XxxPluginErrorCode::NotFound: return "Data not found";
            default: return "Unknown";
        }
    }
};

// Usage
return ll::makeErrorCodeError(XxxPlugin::makeErrorCode(XxxPluginErrorCode::NotFound));
```

When a chained call inside the module fails, use the template function `modules::defaultErrorHandler<XxxPlugin>` to write the error to the module log in a unified way.

## Integrating the GUI

A module can load and execute `.lcui` scripts through `GUIManager` (see [Native UI (Native UI)](../md/native-ui.md) and [LCUI Script Syntax](../md/lcui.md)):

```cpp
auto& gui = LOICollection::form::GUIManager::getInstance();
gui.load("xxx", (guiPath / "xxx.lcui").string());   // Load and compile the script
gui.execute("xxx");                                  // Execute the script to register the form
gui.open("xxx", "formId", GUIManagerType::CustomForm, player);
```

GUIManager also provides `registerValue` / `registerRequest` / `registerCallback` for bidirectional communication between the C++ side and script forms.
