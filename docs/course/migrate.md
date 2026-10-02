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
2. 完成升级后，Menu 与 Shop 不再读取 `menu.json` / `shop.json`，请参考 [数据文件](../md/data.md) 中的示例，手动将旧数据改写为 `menu.lcui` 与 `shop.lcui`，并放置在 `plugins/LOICollectionA/config` 目录下。示例已按当前语法使用 `let` 声明变量；如果您在早期版本按旧示例写过脚本，请在升级到 1.17.0 之前补上 `let`，见 [下文](#对于-1160-版本升至-1170-版本)。
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

## 对于 1.16.0 版本升至 1.17.0 版本

> [!WARNING]
> 1.17.0 将数据层换成块存储（`BlockRepository` + `TypedTable`，详见 [架构概览](../dev/architecture.md#数据层)）。插件**不会**在启动时自动重放旧数据：旧版本写入的表不再被读取，需要手动运行迁移脚本或自行录入。

1. 升级前先备份整个 `plugins/LOICollectionA/data` 目录（以及 `config`、`gui` 目录）。
2. 请下载 `scripts/migrate/migrate_block.py` 文件并将其放入 `插件` 根目录下（即 `plugins/LOICollectionA/` 的上一级目录，脚本默认在该目录下查找数据库文件）
3. 建议先执行一次试运行，确认将要迁移的表与行数无误：

    ```bash
    python migrate_block.py --data-dir plugins/LOICollectionA/data --dry-run
    ```

4. 确认无误后去掉 `--dry-run` 正式执行迁移：

    ```bash
    python migrate_block.py --data-dir plugins/LOICollectionA/data
    ```

5. 该脚本会把旧版 `SQLiteStorage` 键值表原地改写为块模型的 `block` / `prop` / `link` / `meta` 结构，并按文件路由：`blacklist.db`、`mute.db`、`tpa.db`、`chat.db`、`market.db` 中的表在各自文件内迁移，而注册到共享 `SettingsDB` 服务的表（`Market`、`MarketTax`、`Language`、`Pvp`、`Chat`、`Tpa`、`Wallet` 等）统一在 `settings.db` 内迁移。所有业务列都会被保留，`col_<rootId>` 边表与 `schema:` / `sidecol:` 指纹由插件在首次打开数据库时重建。
6. 有两张旧表**不会**被迁移：`Notice`（当前版本用 JSON 而非 SQLite 存储公告）与 `statistics.db` 中的 `Language`（当前代码只读取 `settings.db` 中的 `Language`，该副本才是权威数据）。这两张表会原样保留在磁盘上。
7. 如果不想使用脚本，也可以手动迁移：升级后首次启动时，插件会在每个 `.db` 文件中创建块模型的系统表（`dict` / `block` / `prop` / `link` / `meta`），随后从空数据开始运行。
8. 不做迁移时旧数据也并未被删除，只是保留在各自数据库文件的旧表里（例如 `Wallet`、`Blacklist`、`Market` 这类以业务名命名的表）。需要保留这些内容时，请用 SQLite 工具从旧表导出，再通过游戏内界面或命令重新录入。
9. `notice.json`、`cdk.json`、`config.json` 仍是 JSON 文件，不受本次改动影响，无需迁移。
10. 自行编写的脚本（`config/menu.lcui`、`config/shop.lcui`，以及您在 `gui` 目录下改过的脚本）如果是从早期示例复制而来的，通常是无关键字的裸赋值写法。脚本语言要求所有变量绑定都用 `let` 显式声明，裸赋值不再隐式创建变量，否则加载脚本时会报 `Variable 'xxx' is not declared; write 'let xxx = ...' to introduce it`，该脚本不会被启用（启动日志里是 `ERROR [LOICollectionA]` 级别）。请给每个变量的**首次**赋值补上 `let`：

    ```lcui
    // 升级前
    button1 = new MenuItemData();
    form = new MenuForm("main", "Menu Example");

    // 升级后
    let button1 = new MenuItemData();
    let form = new MenuForm("main", "Menu Example");
    ```

    只有首次绑定需要 `let`：之后的重新赋值（`button1.permission = 0;`）、成员写入（`form.button(...)`）与数组下标写入都保持原样；带类型标注的声明写成 `let x: int = 0;`，且必须带初始值。修改脚本后无需手工清理 `.lcp` 缓存，插件会按源文件内容自动失效并重新编译。同类报错的排查步骤见 [错误处理](../md/errors.md) 第 59、60 条。

> [!TIP]
> 如果不需要历史数据，可以直接删除 `plugins/LOICollectionA/data` 下的 `.db` 文件让插件重建空库——但请先确认已备份。
