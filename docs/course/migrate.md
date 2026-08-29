# 数据迁移

`数据迁移`是指将数据从一个系统或数据库迁移到另一个系统或数据库的过程。`数据迁移`是数据管理的重要组成部分，可以用于数据备份、数据恢复、数据整合、数据迁移等场景。  
而在此的`数据迁移`则是指将`LOICollectionA`中的指定低版本数据迁移到对应的`LOICollectionA`高版本中。

> [!WARNING]
> 在数据迁移之前请确保已正常安装 `Python 3.10.0` 及以上并配置好服务端

## 对于 1.4.6 版本升至 1.4.7 版本

1. 请下载 `migrate147.py` 文件并将其放入 `插件` 根目录下
2. 完成后在命令行中执行 `python migrate147.py` 即可完成迁移
3. 迁移完成后，新的 `settings.db` 文件将包含所有迁移的数据

## 对于 1.6.1 版本升至 1.6.2 版本

1. 请下载 `migrate162.py` 文件并将其放入 `插件` 根目录下
2. 完成后在命令行中执行 `python migrate162.py` 即可完成迁移
3. 迁移完成后，新的 `settings.db` 文件将包含所有迁移的数据

## 对于 1.6.5 版本升至 1.7.0 版本

1. 请下载 `migrate170.py` 文件并将其放入 `插件` 根目录下
2. 完成后在命令行中执行 `python migrate170.py` 即可完成迁移
3. 迁移完成后，所有模块数据都将进行迁移

## 对于 1.9.2 版本升至 1.10.0 版本

1. 请下载 `migrate1100.py` 文件并将其放入 `插件` 根目录下
2. 完成后在命令行中执行 `python migrate1100.py` 即可完成迁移
3. 迁移完成后，所有模块数据都将进行迁移

## 对于 1.14.0 版本升至 1.15.0 版本

> [!WARNING]
> 1.15.0 不再提供自动迁移脚本。Menu 与 Shop 的界面数据需要手动迁移。

1. 升级前请先备份 `plugins/LOICollectionA/config` 目录下的 `config.json`、`menu.json` 与 `shop.json`。
2. 完成升级后，Menu 与 Shop 不再读取 `menu.json` / `shop.json`，请参考 [数据文件](../md/data.md) 中的示例，手动将旧数据改写为 `menu.lcui` 与 `shop.lcui`，并放置在 `plugins/LOICollectionA/config` 目录下。
3. 如自定义过 `GuiPath`，请确认 `config.json` 中的 `ServerConfig.Plugins.Menu.GuiPath` 与 `ServerConfig.Plugins.Shop.GuiPath` 指向新创建的 lcui 文件（默认分别为 `menu.lcui` 与 `shop.lcui`）。
4. 其余模块的数据文件（如 `notice.json`、`cdk.json`）与数据库文件不受影响；其余模块的界面已改由插件内置的 `gui` 目录加载，无需迁移。

## 对于 1.15.0 版本升至 1.16.0 版本

> [!WARNING]
> 1.16.0 收紧了跨脚本跳转的授权。自行编写的脚本若调用了 `GUIManager::open` 跳转到其他脚本，升级后必须补充授权，否则跳转会被拒绝。

1. 字节码缓存格式由 `.lcc` 变更为 `.lcp`（自包含字节码包）。旧缓存会在首次启动时自动失效并重新编译，无需手动处理；如希望清理，可删除 `plugins/LOICollectionA/gui/` 下的 `.lcc` 与 `.lcc.dbg` 文件。
2. `gui/permission.json` 新增 `scripts.<id>.gui.navigations` 字段，用于声明该脚本允许跳转到哪些脚本或表单。升级时该文件会被插件附带的版本覆盖，因此**自写脚本的授权配置需要重新填写**。
3. 若您的 `menu.lcui` / `shop.lcui` 中存在 `GUIManager::open("<其他脚本 id>", ...)` 这类跨脚本跳转，请在 `permission.json` 的对应条目下补上目标，例如：

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

4. 打开自身表单（`wallet` 内调用 `GUIManager::open("wallet", ...)`）不受此限制，无需授权。
5. 内置脚本的授权已由插件自动维护，无需手动干预。若升级后日志出现 `is not allowed for script` 提示，说明有跨脚本跳转缺少授权，按提示中的脚本 id 补进 `navigations` 即可。
