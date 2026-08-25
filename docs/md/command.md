# 命令列表

在 LOICollectionA 中，对于不同的 `内置插件` 均提供了不同的命令列表。  
而插件则提供以下命令以进行简单的交互：

> [!TIP]
> 在命令提示中的 `<>` 为必填参数，`[]` 为可选参数。

## Blacklist

```log
[Server] blacklist:
[Server] LOICollection -> 服务器黑名单
[Server] Usage:
[Server] - /blacklist add <Target: target> [Cause: string] [Time: int]
[Server] - /blacklist gui
[Server] - /blacklist info <Id: string>
[Server] - /blacklist list [Limit: int]
[Server] - /blacklist reload
[Server] - /blacklist remove <Id: string>
```

> [!TIP|style:callout]
> 其中 `blacklist` 为 Blacklist 的顶层命令（权限等级: GameDirectors）。

- `/blacklist add <Target: target> [Cause: string] [Time: int]`
  - 向黑名单中添加一个目标。
  - 其中 `<Target: target>` 为目标选择器。
  - 其中 `<Cause: string>` 为添加的原因。
  - 其中 `<Time: int>` 为添加的时间（单位：分钟）。

- `/blacklist gui`
  - 打开黑名单 GUI。

- `/blacklist info <Id: string>`
  - 查看黑名单中的一个目标的信息。
  - 其中 `<Id: string>` 为目标的ID。

- `/blacklist list [Limit: int]`
  - 列出黑名单中的所有目标。
  - 其中 `[Limit: int]` 为限制显示的数量。（默认为 100，当设置为负数时，显示所有）

- `/blacklist reload`
  - 重新加载（重新编译）黑名单 GUI。

- `/blacklist remove <Id: string>`
  - 从黑名单中移除一个目标。
  - 其中 `<Id: string>` 为目标的ID。

## Mute

```log
[Server] mute:
[Server] LOICollection -> 服务器禁言
[Server] 使用：
[Server] - /mute add <Target: target> [Cause: string] [Time: int]
[Server] - /mute gui
[Server] - /mute info <Id: string>
[Server] - /mute list [Limit: int]
[Server] - /mute reload
[Server] - /mute remove id <Id: string>
[Server] - /mute remove target <Target: target>
```

> [!TIP]
> 其中 `mute` 为 Mute 的顶层命令（权限等级: GameDirectors）。

- `/mute add <Target: target> [Cause: string] [Time: int]`
  - 向禁言列表中添加一个目标。
  - 其中 `<Target: target>` 为目标选择器。
  - 其中 `<Cause: string>` 为添加的原因。
  - 其中 `<Time: int>` 为添加的时间（单位：分钟）。

- `/mute gui`
  - 打开禁言 GUI。

- `/mute info <Id: string>`
  - 查看禁言列表中的一个目标的信息。
  - 其中 `<Id: string>` 为目标的ID。

- `/mute list [Limit: int]`
  - 列出禁言列表中的所有目标。
  - 其中 `[Limit: int]` 为限制显示的数量。（默认为 100，当设置为负数时，显示所有）

- `/mute reload`
  - 重新加载（重新编译）禁言 GUI。

- `/mute remove id <Id: string>`
  - 从禁言列表中移除一个目标。
  - 其中 `<Id: string>` 为目标的ID。

- `/mute remove target <Target: target>`
  - 从禁言列表中移除一个目标。
  - 其中 `<Target: target>` 为目标选择器。

## Cdk

```log
[Server] cdk:
[Server] LOICollection -> 总换码
[Server] Usage:
[Server] - /cdk convert <Id: string>
[Server] - /cdk edit
[Server] - /cdk gui
[Server] - /cdk reload
```

> [!TIP]
> 其中 `cdk` 为 Cdk 的顶层命令（权限等级: Any）。

- `/cdk convert <Id: string>`
  - 总换CDK。
  - 其中 `<Id: string>` 为要总换的CDK。

- `/cdk edit`
  - 打开总换CDK设置（权限等级: GameDirectors）。

- `/cdk gui`
  - 打开总换CDK GUI。

- `/cdk reload`
  - 重新加载（重新编译）总换CDK GUI（权限等级: GameDirectors）。

## Menu

```log
[Server] menu:
[Server] LOICollection -> 服务器菜单
[Server] Usage:
[Server] - /menu clock
[Server] - /menu gui [Id: string]
[Server] - /menu reload
```

> [!TIP]
> 其中 `menu` 为 Menu 的顶层命令（权限等级: Any）。

- `/menu clock`
  - 获取便携打开菜单物品。

- `/menu gui [Id: string]`
  - 打开菜单 GUI。
  - 其中 `<Id: string>` 为菜单ID。

- `/menu reload`
  - 重新加载（重新编译）菜单 GUI（权限等级: GameDirectors）。

## Tpa

```log
[Server] tpa:
[Server] LOICollection -> 玩家互传
[Server] Usage:
[Server] - /tpa accept <Id: string>
[Server] - /tpa cancel <Id: string>
[Server] - /tpa gui
[Server] - /tpa invite <tpa|tphere> <Target: target>
[Server] - /tpa reject <Id: string>
[Server] - /tpa reload
[Server] - /tpa setting
```

> [!TIP]
> 其中 `tpa` 为 Tpa 的顶层命令（权限等级: Any）。

- `/tpa accept <Id: string>`
  - 接受玩家的传送请求。
  - 其中 `<Id: string>` 为传送请求ID。

- `/tpa cancel <Id: string>`
  - 取消自己的传送请求。
  - 其中 `<Id: string>` 为传送请求ID。

- `/tpa gui`
  - 打开 Tpa GUI。

- `tpa invite <tpa|tphere> <Target: target>`
  - 向目标发送传送请求。
  - 其中 `<tpa|tphere>` 为传送类型。
  - 其中 `<Target: target>` 为目标选择器。

- `/tpa reject <Id: string>`
  - 拒绝玩家的传送请求。
  - 其中 `<Id: string>` 为传送请求ID。

- `/tpa reload`
  - 重新加载（重新编译）Tpa GUI（权限等级: GameDirectors）。

- `/tpa setting`
  - 打开 Tpa 个人设置。

## Shop

```log
[Server] shop:
[Server] LOICollection -> 服务器商店
[Server] Usage:
[Server] - /shop gui <Id: string>
[Server] - /shop reload
```

> [!TIP]
> 其中 `shop` 为 Shop 的顶层命令（权限等级: Any）。

- `/shop gui <Id: string>`
  - 打开商店 GUI。
  - 其中 `<Id: string>` 为商店ID。

- `/shop reload`
  - 重新加载（重新编译）商店 GUI（权限等级: GameDirectors）。

## Pvp

```log
[Server] pvp:
[Server] LOICollection -> 服务器PVP
[Server] Usage:
[Server] - /pvp gui
[Server] - /pvp off
[Server] - /pvp on
[Server] - /pvp reload
```

> [!TIP]
> 其中 `pvp` 为 Pvp 的顶层命令（权限等级: Any）。

- `/pvp gui`
  - 打开 Pvp GUI。

- `/pvp off`
  - 关闭 Pvp。

- `/pvp on`
  - 开启 Pvp。

- `/pvp reload`
  - 重新加载（重新编译）Pvp GUI（权限等级: GameDirectors）。

## Wallet

```log
? wallet
[Server] wallet:
[Server] LOICollection -> 个人钱包
[Server] Usage:
[Server] - /wallet bank
[Server] - /wallet gui
[Server] - /wallet history
[Server] - /wallet query <PlayerName: string>
[Server] - /wallet rank
[Server] - /wallet rinfo <EnvelopeId: string>
[Server] - /wallet reload
[Server] - /wallet rstat
[Server] - /wallet transfer <Target: target> <Score: int>
[Server] - /wallet wealth
```

> [!TIP]
> 其中 `wallet` 为 Wallet 的顶层命令（权限等级: Any）。

- `/wallet bank`
  - 打开银行储蓄 GUI（存款、取款、利息查询）。

- `/wallet gui`
  - 打开个人钱包 GUI。

- `/wallet history`
  - 查看自己最近 20 条资金流水（转账、红包、银行）。

- `/wallet query <PlayerName: string>`
  - 查询指定玩家最近 50 条资金流水（权限等级: GameDirectors）。
  - 其中 `<PlayerName: string>` 为目标玩家名称。

- `/wallet rank`
  - 打开财富排行榜 GUI。

- `/wallet rinfo <EnvelopeId: string>`
  - 查询指定红包的领取明细与手气王。
  - 其中 `<EnvelopeId: string>` 为红包 ID。

- `/wallet reload`
  - 重新加载（重新编译）个人钱包 GUI（权限等级: GameDirectors）。

- `/wallet rstat`
  - 查看今日全局红包统计（红包数、总金额、最豪发送者、手气王）。

- `/wallet transfer <Target: target> <Score: int>`
  - 向目标转账。
  - 其中 `<Target: target>` 为目标选择器。
  - 其中 `<Score: int>` 为转账金额。

- `/wallet wealth`
  - 查看个人钱包余额。

## Chat

```log
[Server] chat:
[Server] LOICollection -> 个人称号
[Server] Usage:
[Server] - /chat add <Target: target> <Title: string> [Time: int]
[Server] - /chat gui
[Server] - /chat list <Target: target>
[Server] - /chat reload
[Server] - /chat remove <Target: target> <Title: string>
[Server] - /chat set <Target: target> <Title: string>
[Server] - /chat setting
```

> [!TIP]
> 其中 `chat` 为 Chat 的顶层命令（权限等级: Any）。

- `/chat add <Target: target> <Title: string> [Time: int]`
  - 为目标添加指定称号（权限等级: GameDirectors）。
  - 其中 `<Target: target>` 为目标选择器。
  - 其中 `<Title: string>` 为称号名称。
  - 其中 `[Time: int]` 为称号时间（单位：分钟）。

- `/chat remove <Target: target> <Title: string>`
  - 为目标移除指定称号（权限等级: GameDirectors）。
  - 其中 `<Target: target>` 为目标选择器。
  - 其中 `<Title: string>` 为称号名称。

- `/chat set <Target: target> <Title: string>`
  - 为目标设置指定称号（权限等级: GameDirectors）。
  - 其中 `<Target: target>` 为目标选择器。
  - 其中 `<Title: string>` 为称号名称。
  - 注: 无法设置未拥有的称号。

- `/chat list <Target: target>`
  - 查看目标的所有称号（权限等级: GameDirectors）。
  - 其中 `<Target: target>` 为目标选择器。

- `/chat reload`
  - 重新加载（重新编译）管理称号 GUI（权限等级: GameDirectors）。

- `/chat gui`
  - 打开管理称号 GUI（权限等级: GameDirectors）。

- `/chat setting`
  - 打开个人称号设置。

## Language

```log
[Server] language:
[Server] LOICollection -> 语言设置
[Server] Usage:
[Server] - /language reload
[Server] - /language setting
```

> [!TIP]
> 其中 `language` 为 Language 的顶层命令（权限等级: Any）。

- `/language reload`
  - 重新加载（重新编译）语言设置 GUI（权限等级: GameDirectors）。

- `/language setting`
  - 打开个人语言设置界面。

## Notice

```log
[Server] notice:
[Server] LOICollection -> 公告系统
[Server] Usage:
[Server] - /notice edit
[Server] - /notice gui [Id: string]
[Server] - /notice reload
[Server] - /notice setting
```

> [!TIP]
> 其中 `notice` 为 notice 的顶层命令（权限等级: Any）。

- `/notice edit`
  - 打开公告编辑界面（权限等级: GameDirectors）。

- `/notice gui [Id: string]`
  - 打开公告 GUI。
  - 其中 `[Id: string]` 为公告ID。

- `/notice reload`
  - 重新加载（重新编译）公告 GUI（权限等级: GameDirectors）。

- `/notice setting`
  - 打开公告个人设置。

## Market

```log
[Server] market:
[Server] LOICollection -> 玩家市场
[Server] Usage:
[Server] - /market gui
[Server] - /market reload
[Server] - /market report [Days: int]
[Server] - /market tax
[Server] - /market tax <Rate: string>
```

> [!TIP]
> 其中 `market` 为 Market 的顶层命令（权限等级: Any）。

- `/market gui`
  - 打开玩家市场 GUI。

- `/market reload`
  - 重新加载（重新编译）玩家市场 GUI（权限等级: GameDirectors）。

- `/market report [Days]`
  - 查看市场经营报表（成交笔数、成交总额、税收总额、活跃卖家数），统计窗口默认 7 天（权限等级: GameDirectors）。
  - 其中 `[Days: int]` 为统计窗口天数。

- `/market tax`
  - 查询当前市场交易税率（权限等级: GameDirectors）。

- `/market tax <Rate>`
  - 运行时设置市场交易税率，立即生效并持久化（权限等级: GameDirectors）。
  - 其中 `<Rate: string>` 为税率小数（范围 0.0 ~ 1.0）。

> [!TIP]
> 你看到我了吗？( •̀ ω •́ )✧

## BehaviourEvent

```log
[Server] behaviorevent:
[Server] LOICollection -> 行为事件
[Server] Usage:
[Server] - /behaviorevent back position <PositionOrigin: x y z> <PositionTarget: x y z> <Time: int>
[Server] - /behaviorevent back range <PositionOrigin: x y z> <Radius: int> <Time: int>
[Server] - /behaviorevent clean
[Server] - /behaviorevent query action position <PositionOrigin: x y z> <PositionTarget: x y z> [Limit: int]
[Server] - /behaviorevent query action range <PositionOrigin: x y z> <Radius: int> [Limit: int]
[Server] - /behaviorevent query event custom <Target: string> <Value: string> [Limit: int]
[Server] - /behaviorevent query event dimension <Dimension: Dimension> [Limit: int]
[Server] - /behaviorevent query event foundation <EventName: string> <Time: int> [Limit: int]
[Server] - /behaviorevent query event info <EventId: string>
[Server] - /behaviorevent query event name <EventName: string> [Limit: int]
[Server] - /behaviorevent query event position <PositionOrigin: x y z> [Limit: int]
[Server] - /behaviorevent query event site <PositionOrigin: x y z> <Dimension: Dimension> [Limit: int]
[Server] - /behaviorevent query event time <Time: int> [Limit: int]
```

> [!TIP]
> 其中 `behaviorevent` 为 BehaviourEvent 的顶层命令（权限等级: GameDirectors）。

- `/behaviorevent back position <PositionOrigin: x y z> <PositionTarget: x y z> <Time: int>`
  - 向指定区域内执行行为事件回溯。
  - 其中 `<PositionOrigin: x y z>` 为行为事件回溯的起始位置。
  - 其中 `<PositionTarget: x y z>` 为行为事件回溯的目标位置。
  - 其中 `<Time: int>` 为回溯时间（单位：小时）。

- `/behaviorevent back range <PositionOrigin: x y z> <Radius: int> <Time: int>`
  - 向指定区间内执行行为事件回溯。
  - 其中 `<PositionOrigin: x y z>` 为行为事件回溯的起始位置。
  - 其中 `<Radius: int>` 为行为事件回溯的半径。
  - 其中 `<Time: int>` 为回溯时间（单位：小时）。

- `/behaviorevent clean`
  - 清除所有满足条件的行为事件记录。

- `/behaviorevent query action position <PositionOrigin: x y z> <PositionTarget: x y z> [Limit: int]`
  - 查询指定区域内的行为事件。
  - 其中 `<PositionOrigin: x y z>` 为行为事件查询的起始位置。
  - 其中 `<PositionTarget: x y z>` 为行为事件查询的目标位置。
  - 其中 `[Limit: int]` 为限制显示的数量。（默认为 100，当设置为负数时，显示所有）

- `/behaviorevent query action range <PositionOrigin: x y z> <Radius: int> [Limit: int]`
  - 查询指定区间内的行为事件。
  - 其中 `<PositionOrigin: x y z>` 为行为事件查询的起始位置。
  - 其中 `<Radius: int>` 为行为事件查询的半径。
  - 其中 `[Limit: int]` 为限制显示的数量。（默认为 100，当设置为负数时，显示所有）

- `/behaviorevent query event custom <Target: string> <Value: string> [Limit: int]`
  - 查询指定自定义事件。
  - 其中 `<Target: string>` 为事件目标。
  - 其中 `<Value: string>` 为事件值。
  - 其中 `[Limit: int]` 为限制显示的数量。（默认为 100，当设置为负数时，显示所有）

- `/behaviorevent query event dimension <Dimension: Dimension> [Limit: int]`
  - 查询指定维度的行为事件。
  - 其中 `<Dimension: Dimension>` 为事件维度。
  - 其中 `[Limit: int]` 为限制显示的数量。（默认为 100，当设置为负数时，显示所有）

- `/behaviorevent query event foundation <EventName: string> <Time: int> [Limit: int]`
  - 查询指定基础信息的行为事件。
  - 其中 `<EventName: string>` 为事件名称。
  - 其中 `<Time: int>` 为事件发生时间。
  - 其中 `[Limit: int]` 为限制显示的数量。（默认为 100，当设置为负数时，显示所有）

- `/behaviorevent query event info <EventId: string>`
  - 查询指定事件的详细信息。
  - 其中 `<EventId: string>` 为事件ID。

- `/behaviorevent query event name <EventName: string> [Limit: int]`
  - 查询指定名称的行为事件。
  - 其中 `<EventName: string>` 为事件名称。
  - 其中 `[Limit: int]` 为限制显示的数量。（默认为 100，当设置为负数时，显示所有）

- `/behaviorevent query event position <PositionOrigin: x y z> [Limit: int]`
  - 查询指定位置的行为事件。
  - 其中 `<PositionOrigin: x y z>` 为事件发生位置。
  - 其中 `[Limit: int]` 为限制显示的数量。（默认为 100，当设置为负数时，显示所有）

- `/behaviorevent query event site <PositionOrigin: x y z> <Dimension: Dimension> [Limit: int]`
  - 查询指定区域的行为事件。
  - 其中 `<PositionOrigin: x y z>` 为事件发生位置。
  - 其中 `<Dimension: Dimension>` 为事件发生维度。
  - 其中 `[Limit: int]` 为限制显示的数量。（默认为 100，当设置为负数时，显示所有）

- `/behaviorevent query event time <Time: int> [Limit: int]`
  - 查询指定时间范围内的行为事件。
  - 其中 `<Time: int>` 为事件发生时间。
  - 其中 `[Limit: int]` 为限制显示的数量。（默认为 100，当设置为负数时，显示所有）

## Statistics

```log
[Server] statistics:
[Server] LOICollection -> 玩家数据统计
[Server] Usage：
[Server] - /statistics gui
[Server] - /statistics gui <Type: LOICollection::Plugins::StatisticType>
[Server] - /statistics reload
```

> [!TIP]
> 其中 `statistics` 为 Statistics 的顶层命令（权限等级: Any）。

- `/statistics gui`
  - 打开玩家数据统计 GUI。

- `/statistics gui <Type: LOICollection::Plugins::StatisticType>`
  - 打开指定类型的数据统计 GUI。
  - 其中 `<Type: LOICollection::Plugins::StatisticType>` 为数据统计类型。

- `/statistics reload`
  - 重新加载（重新编译）玩家数据统计 GUI（权限等级: GameDirectors）。

> [!NOTE]
> 以上内容均属于 LOICollectionA 1.15.0 版本的命令列表，对于后续版本的命令列表可能会有所不同。
