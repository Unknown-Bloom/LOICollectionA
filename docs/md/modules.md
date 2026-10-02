# 模块总览

LOICollectionA 采用微内核架构：所有功能都由模块（Module）提供，每个模块可以独立启用或停用。本文列出全部模块、它们的用途与配置开关位置；单个模块的命令用法见 [命令列表](./command.md)，数据文件与配置结构见 [数据文件](./data.md)。

## 模块是分层的

模块按其归属分为三组，配置文件中的路径也不同：

| 分组 | 配置路径 | 编译目标 |
| --- | --- | --- |
| 内置插件（Plugins） | `ServerConfig.Plugins.*` | 仅服务端 |
| 便携工具（ProtableTool） | `ServerConfig.ProtableTool.*` | 仅服务端 |
| 客户端模块 | `ClientConfig.*` | 仅客户端 |

> [!NOTE]
> 所有服务端模块都注册在同一个 `ModulePriority` 序列里，优先级只影响加载与注册顺序，不影响功能归属。详见 [架构概览](../dev/architecture.md)。

## 服务端内置插件

| 模块 | 用途 | 配置键 | 默认状态 |
| --- | --- | --- | --- |
| Blacklist | 禁止指定玩家进入服务器，并支持理由与时长 | `ServerConfig.Plugins.Blacklist.ModuleEnabled` | 停用 |
| Mute | 对玩家禁言，支持理由与时长 | `ServerConfig.Plugins.Mute` | 停用 |
| Cdk | 创建、管理并兑换 CDK 兑换码 | `ServerConfig.Plugins.Cdk` | 停用 |
| Menu | 打开自定义游戏内菜单并执行脚本定义的操作 | `ServerConfig.Plugins.Menu.ModuleEnabled` | 停用 |
| Tpa | 传送请求系统，支持邀请、黑名单、超时与费用 | `ServerConfig.Plugins.Tpa.ModuleEnabled` | 停用 |
| Shop | 服务器商店，支持购买、出售与分页浏览 | `ServerConfig.Plugins.Shop.ModuleEnabled` | 停用 |
| Monitor | 消息强化：玩家名称显示、进服提示、Score 变化检测、指令禁用检测、动态 MOTD 与侧边栏 | `ServerConfig.Plugins.Monitor.ModuleEnabled` | 停用 |
| Pvp | 玩家自行开关 PVP，并可选监听伤害、药水与弹射物 | `ServerConfig.Plugins.Pvp.ModuleEnabled` | 停用 |
| Wallet | 玩家经济：转账、红包、银行储蓄、财富排行与资金台账 | `ServerConfig.Plugins.Wallet.ModuleEnabled` | 停用 |
| Chat | 聊天称号与玩家聊天黑名单，支持自定义聊天格式 | `ServerConfig.Plugins.Chat.ModuleEnabled` | 停用 |
| Language | 记录每位玩家的语言偏好，配套语言选择 GUI | 无 | 常驻启用 |
| Notice | 创建带优先级的公告与通知 | `ServerConfig.Plugins.Notice` | 停用 |
| Market | 玩家之间的交易市场：挂单、玩家商店、评价、行情、求购单与拍卖 | `ServerConfig.Plugins.Market.ModuleEnabled` | 停用 |
| BehaviorEvent | 将玩家与世界的行为事件记录到数据库，支持回溯与自动清理 | `ServerConfig.Plugins.BehaviorEvent.ModuleEnabled` | 停用 |
| Statistics | 统计在线时长、击杀、死亡、放置 / 破坏方块、重生与加入次数 | `ServerConfig.Plugins.Statistics.ModuleEnabled` | 停用 |

> [!WARNING]
> `Mute`、`Cdk` 与 `Notice` 的配置项是**布尔值**而非对象，因此直接写 `"Mute": false` / `"Cdk": false` / `"Notice": false`，没有嵌套的 `ModuleEnabled` 字段。

> [!TIP]
> `Language` 模块没有配置开关，始终启用。它负责 `{player_language}` / `{player_language_name}` 这类 API 变量以及内置的 `gui/language.lcui` 选择界面；关闭它会导致依赖语言信息的变量取不到值。

### Monitor 的子开关

`Monitor` 自身启用后，内部还有五个可独立开关的子功能，默认值均为启用：

| 子功能 | 配置键 | 说明 |
| --- | --- | --- |
| 玩家名称显示 | `Monitor.BelowName` | 在玩家头顶显示带 API 变量的内容 |
| 进服 / 退服提示 | `Monitor.ServerToast` | `Messager.join` / `Messager.leave` 分别控制 |
| Score 变化检测 | `Monitor.ChangeScore` | `ScoreboardLists` 为空时检测全部 Score |
| 指令禁用检测 | `Monitor.DisableCommand` | 命中 `CommandLists` 的指令会被拦截 |
| 动态 MOTD | `Monitor.DynamicMotd` | 支持 API 变量与多页轮播 |
| 侧边栏 | `Monitor.Sidebar` | 支持标题与多页内容 |

### Market 的子开关

`Market` 是配置面最大的模块，主要子功能同样可以单独关闭：

| 子功能 | 配置键 | 默认 |
| --- | --- | --- |
| 玩家商店（店铺） | `Market.StoreEnabled` | 启用 |
| 商店评价 | `Market.StoreReviewEnabled` | 启用 |
| 行情聚合 | `Market.StoreQuoteEnabled` | 启用 |
| 求购单 | `Market.StoreWantedEnabled` | 启用 |
| 求购单部分成交 | `Market.StorePartialBuyEnabled` | 启用 |
| 拍卖 | `Market.StoreAuctionEnabled` | 启用 |

完整的字段与含义请参见 [数据文件](./data.md#配置文件)。

### BehaviorEvent 的事件开关

`BehaviorEvent` 在 `ServerConfig.Plugins.BehaviorEvent.Events` 下为每个事件提供三个开关：`ModuleEnabled`（是否启用该事件）、`RecordDatabase`（是否写入数据库）与 `OutputConsole`（是否输出到控制台）。可记录的事件包括玩家连接、断开、聊天、获得经验、攻击、权限变更、破坏方块、放置方块、死亡、捡起物品、重生、使用物品、容器交互，以及方块爆炸，共 14 项，默认全部启用。

## 服务端便携工具

便携工具是更底层的 Hook 与行为改造，配置在 `ServerConfig.ProtableTool` 下：

| 模块 | 用途 | 配置键 | 默认状态 |
| --- | --- | --- | --- |
| BasicHook | 底层事件 Hook。目前提供 FakeSeed：向客户端伪造 `StartGame` 包中的世界种子 | `ProtableTool.BasicHook.ModuleEnabled` | 停用 |
| RedStone | 红石高频检测：同一方块在一秒内被红石更新超过阈值的次数达标后，破坏该方块并记录日志 | `ProtableTool.RedStone` | 停用（`0`） |
| OrderedUI | 让多个 UI 表单按顺序弹出，避免界面互相覆盖 | `ProtableTool.OrderedUI` | 停用 |

> [!TIP]
> `BasicHook.FakeSeed` 默认为 `$random`。取值可以是一个十进制整数（会被解析为种子），也可以是无法解析为整数的字符串——后者会被视为随机，即每次开服都向客户端报告一个随机种子。

> [!NOTE]
> `RedStone` 的取值是**整数阈值**而非布尔值：`0` 表示不启用，写入 `2` 表示同一方块在一秒内累计 2 次及以上红石更新就会被破坏。请谨慎设置较小的数值。

## 客户端模块

客户端目标（`--target_type=client`）下构建的模块只有音乐播放通知：

| 模块 | 用途 | 配置键 | 默认状态 |
| --- | --- | --- | --- |
| Music | 检测系统媒体会话（默认匹配网易云音乐），在游戏内以卡片形式显示「正在播放」信息，并可选显示音频可视化 | `ClientConfig.Music.ModuleEnabled` | 启用 |

> [!WARNING]
> Music 是**纯客户端**模块，源码位于 `src/LOICollectionA/modules/client/Plugins/music/`，在服务端目标下完全不参与编译（`xmake.lua` 的 `is_server` 分支会排除 `modules/client/**.cpp`）。它依赖 `imgui` 与 Windows 的媒体会话 API，无法在服务端启用。其外观参数（卡片尺寸、字号、动画时长等）见 [数据文件](./data.md#配置文件)。

## 相关文档

- [命令列表](./command.md) —— 各模块的完整命令与参数
- [数据文件](./data.md) —— `config.json` 全量结构与各模块的数据文件
- [LOICollectionAPI](./api.md) —— 可在配置与脚本中使用的变量
- [模块开发指南](../dev/module.md) —— 如何编写一个新的模块
- [自定义事件](../dev/events.md) —— 模块对外发布的 12 个事件头文件
