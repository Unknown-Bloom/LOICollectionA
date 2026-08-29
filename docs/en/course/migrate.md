# Data Migration

`Data migration` refers to the process of migrating data from one system or database to another system or database. `Data migration` is an important part of data management and can be used in scenarios such as data backup, data recovery, data integration, and data migration.  
Here, `data migration` refers to migrating the specified lower-version data in `LOICollectionA` to the corresponding higher version of `LOICollectionA`.

> [!WARNING]
> Before data migration, please make sure `Python 3.10.0` or above is properly installed and the server is configured

## For upgrading from version 1.4.6 to version 1.4.7

1. Download the `migrate147.py` file and place it in the `plugin` root directory
2. After that, run `python migrate147.py` in the command line to complete the migration
3. After the migration is complete, the new `settings.db` file will contain all the migrated data

## For upgrading from version 1.6.1 to version 1.6.2

1. Download the `migrate162.py` file and place it in the `plugin` root directory
2. After that, run `python migrate162.py` in the command line to complete the migration
3. After the migration is complete, the new `settings.db` file will contain all the migrated data

## For upgrading from version 1.6.5 to version 1.7.0

1. Download the `migrate170.py` file and place it in the `plugin` root directory
2. After that, run `python migrate170.py` in the command line to complete the migration
3. After the migration is complete, all module data will be migrated

## For upgrading from version 1.9.2 to version 1.10.0

1. Download the `migrate1100.py` file and place it in the `plugin` root directory
2. After that, run `python migrate1100.py` in the command line to complete the migration
3. After the migration is complete, all module data will be migrated

## For upgrading from version 1.14.0 to version 1.15.0

> [!WARNING]
> 1.15.0 no longer provides automatic migration scripts. The interface data of Menu and Shop needs to be migrated manually.

1. Before upgrading, please back up `config.json`, `menu.json`, and `shop.json` in the `plugins/LOICollectionA/config` directory.
2. After the upgrade, Menu and Shop no longer read `menu.json` / `shop.json`. Please refer to the examples in [Data Files](../md/data.md) to manually rewrite the old data as `menu.lcui` and `shop.lcui`, and place them in the `plugins/LOICollectionA/config` directory.
3. If you have customized `GuiPath`, please confirm that `ServerConfig.Plugins.Menu.GuiPath` and `ServerConfig.Plugins.Shop.GuiPath` in `config.json` point to the newly created lcui files (defaulting to `menu.lcui` and `shop.lcui` respectively).
4. The data files of other modules (such as `notice.json` and `cdk.json`) and database files are not affected; the interfaces of other modules are now loaded from the plugin's built-in `gui` directory and do not need migration.

## For upgrading from version 1.15.1 to version 1.16.0

> [!WARNING]
> 1.16.0 introduces a script sandbox and authorization mechanism. Earlier versions had no `permission.json`, and scripts could freely run `mc::runCmd`, read and write GUI data, and navigate to other scripts. After upgrading these calls are denied by default, so scripts you wrote (such as `menu.lcui` / `shop.lcui`) must be granted authorization to keep working.

1. `permission.json` is a new file in 1.16.0, located in `plugins/LOICollectionA/gui/`, with a deny-by-default policy (`defaultPolicy: "deny"`). It governs `mc::runCmd`, `GUIManager::value/request/callback`, and cross-script navigation via `GUIManager::open`. Authorization for built-in scripts (`blacklist`, `wallet`, etc.) ships with the plugin and needs no action.
2. If your `menu.lcui` / `shop.lcui` invokes any of the above, fill in the allowlists under `scripts.menu` / `scripts.shop` based on what the script actually calls: commands go in `commands.templates` (with `commands.allow` set to `true`), data ids go in `gui.values` / `gui.requests` / `gui.callbacks`.
3. If the script contains cross-script navigation such as `GUIManager::open("<other script id>", ...)`, also declare the target in `gui.navigations`, for example:

    ```json
    "menu": {
        "enabled": true,
        "commands": { "allow": true, "templates": [] },
        "gui": {
            "values": [],
            "requests": [],
            "callbacks": [],
            "navigations": ["wallet", "market"]
        }
    }
    ```

4. Opening a script's own form (`GUIManager::open("wallet", ...)` inside `wallet`) is unaffected and needs no grant.
5. 1.16.0 also introduces a bytecode cache: compiled scripts are stored next to the source as `.lcp` and reused on later starts to skip compilation. The plugin generates and invalidates it automatically; deleting one only causes a recompile on the next start and does not affect correctness.
6. If the log shows `is not allowed for script` after upgrading, a capability is missing its grant — add the script id from the message to the matching allowlist.
