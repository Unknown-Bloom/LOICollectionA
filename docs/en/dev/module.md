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
        std::shared_ptr<SQLiteStorage> db;
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

        // Initialize the module's own database
        this->mImpl->db = std::make_shared<SQLiteStorage>((mDataPath / "xxx.db").string());
        return true;
    }

    ll::Expected<bool> XxxPlugin::registry() {
        // Skip directly when the module is disabled (no command/event/UI registration)
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        // Create table -> register UI -> register commands and events
        return this->mImpl->db->create("Xxx", [](SQLiteStorage::ColumnCallback ctor) -> void {
            ctor("id");
            ctor("name");
        }).and_then([this]() -> ll::Expected<void> {
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
| `registry` | Check `ModuleEnabled` → create table → register UI/commands/events | Return `false` to skip when the plugin is not enabled |
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

### SQLiteStorage (Recommended)

A connection-pool wrapper based on SQLite; all methods return `ll::Expected<T>`:

| Method | Description |
| --- | --- |
| `create(table, callback)` | Create a table; call `ctor("column name")` for each column in the callback |
| `set(table, key, values)` | Write one row (key is the primary key, values is a column-name-to-value mapping) |
| `get(table, key)` | Read one row, returning a `column name -> value` mapping |
| `get(table, key, column, default)` | Read a single column |
| `find(table, conditions, match)` | Query by conditions, returning the list of matching keys (`FindCondition::AND/OR`) |
| `has(table, key)` | Check whether the key exists |
| `del(table, key)` | Delete one row |
| `list(table)` | List all keys |
| `exec(sql)` | Execute raw SQL (e.g. `"VACUUM;"`) |

```cpp
// Write
this->db->set("Xxx", uuid, {
    { "name", player.getRealName() },
    { "time", std::to_string(time) }
});

// Query (OR condition)
auto id = this->db->find("Xxx", {
    { "data_uuid", uuid },
    { "data_ip", ip }
}, "", SQLiteStorage::FindCondition::OR);

// Chained error handling
this->db->get("Xxx", id)
    .and_then([this](std::unordered_map<std::string, std::string> data) -> ll::Expected<void> {
        // ...
        return {};
    })
    .or_else(modules::defaultErrorHandler<XxxPlugin>);
```

> [!TIP]
> When a transaction is needed, use `SQLiteStorageTransaction::create(storage)` and control it with `commit()` / `rollback()`.

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
