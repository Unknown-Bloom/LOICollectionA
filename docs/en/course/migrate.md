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

## For upgrading from version 1.15.0 to version 1.16.0

> [!WARNING]
> 1.16.0 tightens authorization for cross-script navigation. If a script you wrote calls `GUIManager::open` to navigate to another script, you must add the grant after upgrading, otherwise the navigation is denied.

1. The bytecode cache format changes from `.lcc` to `.lcp` (a self-contained bytecode package). Old caches are invalidated and recompiled on first start, so no manual action is required; to clean up, delete the `.lcc` and `.lcc.dbg` files under `plugins/LOICollectionA/gui/`.
2. `gui/permission.json` gains a `scripts.<id>.gui.navigations` field declaring which scripts or forms a script may navigate to. This file is overwritten by the copy shipped with the plugin on upgrade, so **authorization for scripts you wrote must be re-entered**.
3. If your `menu.lcui` / `shop.lcui` contains cross-script navigation such as `GUIManager::open("<other script id>", ...)`, add the target under the matching entry in `permission.json`, for example:

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
5. Authorization for built-in scripts is maintained by the plugin and needs no manual work. If the log shows `is not allowed for script` after upgrading, a cross-script navigation is missing its grant — add the script id from the message to `navigations`.
