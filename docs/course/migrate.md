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

## 对于 1.15.1 版本升至 1.16.0 版本

> [!WARNING]
> 1.16.0 引入了脚本沙箱与授权机制。此前版本没有 `permission.json`，脚本可自由执行 `mc::runCmd`、读写 GUI 数据、跳转到其他脚本；升级后这些调用默认被拒绝，自行编写的脚本（如 `menu.lcui` / `shop.lcui`）必须补配授权才能继续工作。

1. `permission.json` 是 1.16.0 新增的授权文件，位于 `plugins/LOICollectionA/gui/`，默认策略为拒绝（`defaultPolicy: "deny"`）。它管控 `mc::runCmd`、`GUIManager::value/request/callback` 以及 `GUIManager::open` 的跨脚本跳转。内置脚本（`blacklist`、`wallet` 等）的授权已随插件提供，无需干预。
2. 若您的 `menu.lcui` / `shop.lcui` 中调用了上述能力，请按脚本内实际调用的内容，在 `scripts.menu` / `scripts.shop` 下填写白名单：命令填入 `commands.templates`（并将 `commands.allow` 置为 `true`），数据 id 填入 `gui.values` / `gui.requests` / `gui.callbacks`。
3. 若脚本中存在 `GUIManager::open("<其他脚本 id>", ...)` 这类跨脚本跳转，还需在 `gui.navigations` 中声明目标，例如：

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
5. 1.16.0 同时引入了字节码缓存：脚本编译产物以 `.lcp` 形式存放在源文件旁，后续启动直接复用以跳过编译。它由插件自动生成与失效，删除后只会让下次启动重新编译，不影响正确性。
6. 若升级后日志出现 `is not allowed for script` 提示，说明某项能力缺少授权，按提示中的脚本 id 与能力类型补进对应的白名单即可。
