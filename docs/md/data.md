# 数据文件

## 配置文件

配置文件属于 `.json` 格式的文件，一般情况下文件名为 `config.json`  
配置文件通常是在安装完成后 `第一次启动服务器` 时自动生成，位于 `plugins/LOICollectionA/config/` 目录下  
对于 `config.json` 中的配置项，在 `启动服务器` 时只会进行一次读取，之后对配置文件的修改需要 `重启服务器` 才会生效  
当您升级插件版本时，新增的配置项会自动合并进现有配置文件，已有配置项及其取值会被保留，无需手动补写

> [!WARNING]
> 配置文件中的配置项，必须为 `英文` 或 `数字` 或 `下划线`，请不要使用中文作为配置项的名称，否则会导致配置文件无法正常读取。  
> 请勿使用记事本等不支持 `.json` 格式的文本编辑器进行编辑，以免导致配置文件结构损坏。

```json
{
    "version": 90052239, // 配置文件版本号，由插件版本号自动生成，用于配置同步，不建议修改
    "ConsoleLanguage": "system", // 控制台语言，其中 system 为跟随系统语言，zh_CN 为中文，en_US 为英文
    "ServerConfig": { // 服务端配置
        "Plugins": { // 内置插件配置
            "Blacklist": {
                "ModuleEnabled": false, // 是否启用黑名单
                "BroadcastMessage": true // 是否启用黑名单内容广播
            },
            "Mute": false,   // 是否启用禁言
            "Cdk": false, // 是否启用 CDK
            "Menu": { // 菜单配置
                "ModuleEnabled": false, // 是否启用菜单
                "MenuItemId": "minecraft:clock",  // 打开菜单物品 ID
                "EntranceKey": "main", // 菜单入口
                "GuiPath": "menu.lcui" // Gui 入口文件
            },
            "Tpa": { // TPA 配置
                "ModuleEnabled": false, // 是否启用 TPA
                "TargetScoreboard": "money", // TPA 请求目标使用 Score 对象
                "BlacklistUpload": 10, // 玩家黑名单目标最大上传数量
                "RequestRequired": 100, // TPA 请求所需 Score 数量
                "RequestTimeout": 60, // TPA 请求超时时间（单位为秒）
                "RequestUpload": 5 // 玩家传送请求最大上传数量
            },
            "Shop": {
                "ModuleEnabled": false, // 是否启用商店
                "GuiPath": "shop.lcui"
            },
            "Monitor": { // 消息强化配置
                "ModuleEnabled": false, // 是否启用消息强化
                "BelowName": {
                    "ModuleEnabled": true, // 是否启用玩家名称显示
                    "RefreshInterval": 20, // 刷新间隔，单位为 tick（20 tick = 1 秒）
                    "RefreshDisplayInterval": 100, // 刷新显示间隔，单位为 tick
                    "Pages": [
                        [
                            "{player}" // 每行显示内容
                        ]
                    ] // 玩家名称显示格式，支持 LOICollectionA API 变量
                },
                "ServerToast": {
                    "ModuleEnabled": true, // 是否启用服务器提示
                    "Messager": {
                        "join": true, // 是否启用玩家加入服务器提示
                        "leave": true // 是否启用玩家退出服务器提示
                    }
                },
                "ChangeScore": {
                    "ModuleEnabled": true, // 是否启用 Score 变化检测
                    "ScoreboardLists": [] // 检测 Score 变化的对象（为空时会检测所有 Score 的变更）
                },
                "DisableCommand": {
                    "ModuleEnabled": true, // 是否启用指令禁用检测
                    "CommandLists": [] // 被禁用的指令列表
                },
                "DynamicMotd": {
                    "ModuleEnabled": true, // 是否启用动态服务器 MOTD
                    "RefreshInterval": 200, // 刷新间隔，单位为 tick（20 tick = 1 秒）
                    "Pages": [
                        "'Players: ' + {server_player_online} + '/' + {server_player_max}" // 每行显示内容，支持 LOICollectionA API 变量
                    ]
                },
                "Sidebar": {
                    "ModuleEnabled": true, // 是否启用侧边栏
                    "RefreshInterval": 20, // 刷新间隔，单位为 tick（20 tick = 1 秒）
                    "Titles": [ // 侧边栏标题，每一行是对应一页的标题
                        "'Title'" // 标题，支持 LOICollectionA API 变量
                    ],
                    "Pages": [
                        [
                            "'Content'" // 每行显示内容，支持 LOICollectionA API 变量
                        ] // 每页内容
                    ]
                }
            },
            "Pvp": {
                "ModuleEnabled": false, // 是否启用 PVP
                "ExtraListener": {
                    "onActorHurt": true, // 是否启用玩家伤害侦听器
                    "onSplashPotion": true, // 是否启用药水效果侦听器
                    "onProjectileHit": true // 是否启用射击侦听器
                }
            },
            "Wallet": { // 钱包配置
                "ModuleEnabled": false, // 是否启用钱包
                "TargetScoreboard": "money", // 钱包指定使用 Score 对象
                "ExchangeRate": 0.1, // 钱包汇率
                "RedEnvelopeTimeout": 60, // 红包超时时间（单位为秒）
                "WalletHistoryEnabled": true, // 是否记录资金台账
                "WalletHistoryRetentionDays": 90, // 台账保留天数，超期自动清理
                "TransferMinAmount": 1, // 最低转账额，低于该值拒绝（0 = 不限）
                "TransferDailyLimit": 0, // 单日累计转账上限（含手续费），0 = 不限
                "TransferConfirmThreshold": 1000, // 大额转账确认阈值，超过需 GUI 二次确认（0 = 不需确认）
                "TransferCooldownSeconds": 0, // 两次转账最小间隔（秒），0 = 无冷却
                "WealthTopSize": 50, // 财富排行榜容量（单位为个）
                "WealthRefreshMinutes": 10, // 财富排行榜刷新间隔（单位为分钟）
                "WalletBankEnabled": true, // 是否启用银行储蓄
                "WalletBankMinDeposit": 10, // 最低存款额，低于该值拒绝
                "WalletBankDailyRate": 0.0005, // 银行日利率（单利，按天计息）
                "WalletInterestTaxRate": 0.1, // 利息税率（从利息中扣除进入钱池）
                "WalletInterestFromPool": true, // 利息优先从手续费钱池支付（钱池不足时按比例降发）
                "RedEnvelopeTargetedEnabled": true, // 是否启用定向红包
                "RedEnvelopeMaxCount": 100 // 单个红包最大份数
            },
            "Chat": { // 聊天强化配置
                "ModuleEnabled": false, // 是否启用聊天强化
                "FormatText": "<{player}> ${0}", // 聊天消息格式，其中 ${0} 为玩家聊天内容，支持 LOICollectionA API 变量
                "BlacklistUpload": 10 // 黑名单目标最大上传数量
            },
            "Notice": false, // 是否启用公告栏
            "Market": { // 玩家市场配置
                "ModuleEnabled": false, // 是否启用玩家市场
                "TargetScoreboard": "money", // 玩家市场指定使用 Score 对象
                "MaximumUpload": 20, // 玩家市场最大上传数量
                "BlacklistUpload": 10, // 玩家黑名单目标最大上传数量
                "TradeRequestTimeout": 60, // 玩家交易请求超时时间（单位为秒）
                "TradeTimeout": 90, // 玩家交易超时时间（单位为秒）
                "ProhibitedItems": [], // 玩家市场禁止上传的物品
                "StoreEnabled": true, // 是否启用玩家商店（店铺）功能
                "StoreReviewEnabled": true, // 是否启用商店评价功能
                "StoreMaximumItems": 20, // 单个商店最大上架物品数量
                "StoreCreationCost": 0, // 创建商店所需 Score 费用（从 TargetScoreboard 扣除，为 0 时免费）
                "StoreSalesWeight": 0.5, // 商店排名权重 - 近 30 天交易次数
                "StoreVolumeWeight": 0.3, // 商店排名权重 - 近 30 天成交数量
                "StoreRatingWeight": 0.35, // 商店排名权重 - 玩家评分
                "StoreBadReviewPenalty": 3.0, // 商店排名惩罚 - 近 30 天差评比例
                "StoreColdStartWeight": 0.5, // 商店排名加成 - 新店冷启动权重
                "StoreRatingSmoothing": 15.0, // 商店评分贝叶斯平滑参数
                "StoreColdStartDays": 7, // 新店冷启动加成天数（单位为天）
                "StoreTransactionWindowDays": 30, // 商店交易数据统计窗口（单位为天）
                "StoreRatingWindowDays": 180, // 商店评价数据统计窗口（单位为天）
                "StoreRiskWindowDays": 30, // 商店风险数据统计窗口（单位为天）
                "StoreRankRefreshMinutes": 60, // 商店排行榜刷新间隔（单位为分钟）
                "StoreQuoteEnabled": true, // 是否启用行情聚合（成交均价、成交量与成交额排行）
                "StoreQuoteRefreshMinutes": 30, // 行情刷新间隔（单位为分钟）
                "StoreTransactionTaxRate": 0.0, // 交易税税率（0.0 ~ 1.0，成交价 × 税率向下取整；可被 /market tax 运行时覆盖）
                "StorePriceCeilingRatio": 0.0, // 价格上限比例（基于近 30 天均价，超过均价 × 比例的定价将被拦截，为 0 时关闭）
                "StoreWantedEnabled": true, // 是否启用求购单功能
                "StoreWantedMaxPerPlayer": 5, // 单个玩家最大同时挂单数量
                "StoreWantedExpireDays": 7, // 求购单过期天数（过期后自动退还冻结预付款）
                "StorePartialBuyEnabled": true, // 是否启用求购单部分成交
                "StoreAuctionEnabled": true, // 是否启用拍卖功能
                "StoreAuctionMinDurationMinutes": 30, // 拍卖最短时长（单位为分钟）
                "StoreAuctionMaxDurationHours": 72, // 拍卖最长时长（单位为小时）
                "StoreAuctionMinBidIncrement": 1.05, // 最低加价比例（新出价须高于当前价 × 该比例）
                "StoreAuctionAntiSnipeSeconds": 0, // 防狙击时长（单位为秒，尾盘出价自动延长；为 0 时关闭）
                "StoreRepeatTradeLimit": 3, // 同一交易对重复成交计数上限（超过后不计入行情均价，防刷量）
                "StorePriceOutlierRatio": 0.0 // 离群价过滤比例（偏离近期均价超过该倍数的成交不计入均价，为 0 时关闭）
            },
            "BehaviorEvent": { // 行为事件配置
                "ModuleEnabled": false, // 是否启用行为事件
                "OrganizeDatabaseInterval": 144, // 行为事件数据库清理阈值（单位为小时）
                "CleanThresholdEvent": 10000, // 行为事件清理阈值
                "CleanDatabaseInterval": 1, // 数据库自动清理间隔（单位为小时）
                "RefreshIntervalInMinutes": 5, // 行为事件记录间隔（单位为分钟）
                "SingleBacktrackingQuantity": 2000, // 单次回溯事件数量（注：数量越大，单 tick 内处理的事件就越多，可能会导致服务器滞留直至完成处理）
                "Events": { // 行为事件配置
                    "onPlayerConnect": { // 玩家连接事件
                        "ModuleEnabled": true, // 是否启用该事件
                        "RecordDatabase": true, // 是否记录到数据库
                        "OutputConsole": true // 是否输出到控制台
                    },
                    "onPlayerDisconnect": { // 玩家断开连接事件
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    },
                    "onPlayerChat": { // 玩家聊天事件
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    },
                    "onPlayerAddExperience": { // 玩家获得经验事件
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    },
                    "onPlayerAttack": { // 玩家攻击事件
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    },
                    "onPlayerChangePerm": { // 玩家权限改变事件
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    },
                    "onPlayerDestroyBlock": { // 玩家破坏方块事件
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    },
                    "onPlayerPlaceBlock": { // 玩家放置方块事件
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    },
                    "onPlayerDie": { // 玩家死亡事件
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    },
                    "onPlayerPickUpItem": { // 玩家捡起物品事件
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    },
                    "onPlayerRespawn": { // 玩家重生事件
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    },
                    "onPlayerUseItem": { // 玩家使用物品事件
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    },
                    "onPlayerContainerInteract": { // 玩家容器交互事件
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    },
                    "onBlockExplode": { // 方块爆炸事件
                        "ModuleEnabled": true,
                        "RecordDatabase": true,
                        "OutputConsole": true
                    }
                }
            },
            "Statistics": {
                "ModuleEnabled": false, // 是否启用统计
                "RefreshIntervalInMinutes": 5, // 统计刷新间隔（单位为分钟）
                "RankingPlayerCount": 100, // 排行榜显示数量
                "DatabaseInfo": { // 统计数据库信息（每次退出后记录）
                    "OnlineTime": true, // 是否启用在线时间统计
                    "Kill": true, // 是否启用击杀生物统计
                    "Death": true, // 是否启用死亡统计
                    "Place": true, // 是否启用放置方块统计
                    "Destroy": true, // 是否启用破坏方块统计
                    "Respawn": true, // 是否启用重生统计
                    "Join": true // 是否启用加入服务器统计
                }
            }
        },
        "ProtableTool": { // 便携工具配置
            "BasicHook": { // 基础功能配置
                "ModuleEnabled": false, // 是否启用基础功能
                "FakeSeed": "$random" // 假种子配置
            },
            "RedStone": 0, // 红石高频检测，其中为每秒频率（为 0 时不启用）
            "OrderedUI": false // 是否启用有序 UI
        }
    }
}
```

> [!NOTE]
> 以上内容取自 LOICollectionA 1.15.0 的配置文件结构，对于后续版本的配置文件结构可能会有所不同。

## 模块数据文件

数据文件是指存储在数据库中的数据文件。数据文件是数据库的核心，它存储了数据库中的所有数据。数据文件可以是文本文件、二进制文件或者其他类型的文件。数据文件的格式和内容取决于数据库的类型和应用场景。  
目前 `LOICollectionA` 支持 `Json` 和 `SQLite` 两种数据文件格式。其中只有 `Json` 格式的数据文件可以被直接修改。而对于 `SQLite` 格式的数据文件，我们是不建议您直接修改的。

> [!TIP]
> 通常情况下，您不需要手动修改数据文件，因为在使用 `LOICollectionA` 的过程中，大部分数据文件都存在内部编辑器。从 1.15.0 起，Menu 与 Shop 改为直接编辑 lcui 数据文件，不再提供游戏内编辑器。

### permission.json（gui 目录）

`permission.json` 是脚本能力授权文件，位于 `plugins/LOICollectionA/gui/` 目录。它控制每个脚本允许调用的敏感能力：命令执行（`mc::runCmd`）与业务读写（`GUIManager::value/request/callback`）。

> [!IMPORTANT]
> 授权采用默认拒绝（`defaultPolicy: "deny"`）策略。脚本与授权分离：修改脚本（尤其是 `menu.lcui` / `shop.lcui`）新增能力调用后，必须同步修改 `permission.json`，否则对应调用会被拒绝并记录错误日志。

| 字段 | 类型 | 说明 |
| ---- | ---- | ---- |
| `defaultPolicy` | string | 未在 `scripts` 中列出的脚本的默认策略，`"deny"`（拒绝）或 `"allow"`（放行），建议保持 `"deny"` |
| `scripts.<id>` | object | 脚本授权条目，`<id>` 为脚本短 id（如 `wallet`、`market.auction`、`menu`） |
| `scripts.<id>.enabled` | boolean | 是否允许打开该脚本 |
| `scripts.<id>.commands.allow` | boolean | 是否允许 `mc::runCmd` |
| `scripts.<id>.commands.templates` | string[] | 允许执行的命令白名单（原文精确匹配），为空时即使 `allow` 为 `true` 也拒绝 |
| `scripts.<id>.gui.values` | string[] | 允许的 `GUIManager::value` id 白名单 |
| `scripts.<id>.gui.requests` | string[] | 允许的 `GUIManager::request` id 白名单 |
| `scripts.<id>.gui.callbacks` | string[] | 允许的 `GUIManager::callback` id 白名单 |

```json
{
    "defaultPolicy": "deny",
    "scripts": {
        "menu": {
            "enabled": true,
            "commands": {
                "allow": true,
                "templates": ["say No permission", "say No score"]
            },
            "gui": {
                "values": [],
                "requests": [],
                "callbacks": []
            }
        },
        "wallet": {
            "enabled": true,
            "commands": { "allow": false, "templates": [] },
            "gui": {
                "values": ["wallet.players.online", "wallet.rank"],
                "requests": ["wallet.info", "wallet.transfer.submit"],
                "callbacks": ["wallet.wealth", "wallet.transfer.confirm"]
            }
        }
    }
}
```

> [!TIP]
> 内置脚本（`blacklist`、`wallet` 等）的授权随插件更新自动维护；`menu` / `shop` 是您自行编写的脚本，其授权需要您根据脚本内实际调用的命令与 GUI id 手动同步。

### menu.lcui（config 目录）

> 从 LOICollectionA 1.15.0 起，Menu 不再读取 `menu.json`。请在 `plugins/LOICollectionA/config` 目录下自行创建 `menu.lcui`，在文件内直接定义 `MenuData` 并使用 `MenuForm` 打开。表单 ID 对应 `/menu gui <Id>` 中传入的 Id。

```lcui
button1 = new MenuItemData();
button1.type = "button";
button1.title = "Button 1";
button1.id = "Button1";
button1.run = [ "say Button1" ];
button1.permission = 0;

form = new MenuForm("main", "Menu Example");
form.label("This is a menu example", new TextOptions());
form.button("Button 1", button1, func () -> void {
}, new ButtonOptions());
form.closeButton();
form.show(func (result) -> void {
    if (result.closeReason == 2) [
        mc::runCmd("say No permission");
    :
        if (result.closeReason == 3) [
            mc::runCmd("say No score");
        ]
    ]
});

confirmAction = new MenuItemData();
confirmAction.type = "button";
confirmAction.title = "Confirm";
confirmAction.run = [ "say Confirm" ];
confirmAction.permission = 0;

cancelAction = new MenuItemData();
cancelAction.type = "button";
cancelAction.title = "Cancel";
cancelAction.run = [ "say Cancel" ];
cancelAction.permission = 0;

box = new MenuMessageBox("Menu1", "Menu 1");
box.body("This is a menu 1");
box.button1("Confirm", confirmAction);
box.button2("Cancel", cancelAction);
box.show(func (result) -> void {
});

```

### shop.lcui（config 目录）

> 从 LOICollectionA 1.15.0 起，Shop 不再读取 `shop.json`。请在 `plugins/LOICollectionA/config` 目录下自行创建 `shop.lcui`，在文件内直接定义 `ShopData` 并使用 `ShopForm` 打开。表单 ID 对应 `/shop gui <Id>` 中传入的 Id。

```lcui
mainBuy = new ShopData();
mainBuy.id = "MainBuy";
mainBuy.type = "buy";
mainBuy.title = "Buy Shop Example";
mainBuy.content = "This is a shop example";
mainBuy.exitCommand = "say Exit Shop";
mainBuy.scoreCommand = "say No score";

apple = new ShopItemData();
apple.type = "commodity";
apple.title = "Apple";
apple.introduce = "A red apple";
apple.number = "Buy number";
apple.id = "minecraft:apple";
appleScore = new ScoreRequirement();
appleScore.objective = "money";
appleScore.value = 100;
apple.scores = [ appleScore ];

mainBuy.items = [ apple ];

form = new ShopForm("MainBuy", mainBuy);
form.show(func (result) -> void {
    if (result.closeReason == 1) [
        if (result.resultCode == 1) [
            mc::runCmd(result.shop.scoreCommand);
        ]
    ]
});

```

> [!IMPORTANT]
> `menu.lcui` 与 `shop.lcui` 中直接调用的 `mc::runCmd`、`GUIManager::value/request/callback` 等能力受 `plugins/LOICollectionA/gui/permission.json` 管控。修改脚本并新增这类调用后，请同步在 `permission.json` 的 `scripts.menu` / `scripts.shop` 下补充对应的命令模板（`commands.templates`）或 GUI id（`gui.values/requests/callbacks`），否则相关调用会被拒绝并记录错误日志。

> [!TIP]
> 除 Menu 与 Shop 外，其余模块（Blacklist、Mute、Cdk、Chat、Market、Notice、Pvp、Statistics、Tpa、Wallet、Language 等）的界面文件随插件内置在 `plugins/LOICollectionA/gui` 目录下（如 `blacklist.lcui`、`mute.lcui`）。它们随插件更新，不属于可编辑的配置文件，请勿直接修改。（如果需要修改，请深入学习 lcui 具体语法）

### notice.json

> [!NOTE]
> 以下内容取自 LOICollectionA 1.10.0 的 `notice.json` 结构，对于后续版本的 `notice.json` 结构可能会有所不同。  
> 您可以在 `plugins/LOICollectionA/config` 目录下找到 `notice.json` 文件。  

对于内部编辑器，您可以通过以下 [命令](./command.md#notice) 进行编辑

```json
{
    "main": { // 公告ID（不可重复）
        "title": "'Test Notice 123'", // 公告标题
        "content": [ // 公告内容（支持多行）
            "'This is a test text 1'", // 第 1 行内容
            "'This is a test text 2'", // 第 2 行内容
            "'This is a test text 3'" // 第 3 行内容
        ],
        "priority": 0, // 公告优先级
        "poiontout": true // 公告是否在玩家上线时弹出显示
    }
}
```

### cdk.json

> [!NOTE]
> 以下内容取自 LOICollectionA 1.7.0 的 `cdk.json` 结构，对于后续版本的 `cdk.json` 结构可能会有所不同。  
> 您可以在 `plugins/LOICollectionA/config` 目录下找到 `cdk.json` 文件。  

对于内部编辑器，您可以通过以下 [命令](./command.md#cdk) 进行编辑

```json
{
    "cdk": { // CDK ID（不可重复）
        "personal": false, // 是否只能被总换一次
        "player": [], // 总换 CDK 的玩家列表
        "scores": { // CDK 给予的Score（可选）
            "money": 100 // CDK 给予的 Score 分数
        },
        "item": [ // CDK 给予的物品（可选）
            { 
                "id": "minecraft:apple", // 物品 ID
                "name": "apple", // 物品名称
                "quantity": 1, // 物品数量
                "specialvalue": 0, // 物品特殊值
                "type": "universal" // 物品解析类型
            },
            {
                "id": "{Count:2b,Damage:0s,Name:'minecraft:apple',WasPickedUp:0b}",
                "type": "nbt"
            }
        ],
        "title": { // CDK 给予的称号（可选）
            "None": 0 // 称号 ID（值为 0 表示永久称号）
        },
        "time": "0" // CDK 删除时间（具体格式为 %Y%m%d%H%M%S，为 0 时永久存在）
    }
}
```

> [!DANGER]
> 在服务器启动时，请不要直接修改数据文件，否则极有可能丢失数据内容。

---

> [!TIP]
> 有时候，朋友真的很好ヾ(•ω•`)o
