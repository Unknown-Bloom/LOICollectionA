# Commands

In LOICollectionA, different command lists are provided for different `built-in plugins`.  
And the plugins provide the following commands for simple interaction:

> [!TIP]
> In the command prompt, `<>` denotes a required parameter and `[]` an optional parameter.

## Blacklist

```log
[Server] blacklist:
[Server] LOICollection -> Server blacklist
[Server] Usage:
[Server] - /blacklist add <Target: target> [Cause: string] [Time: int]
[Server] - /blacklist gui
[Server] - /blacklist info <Id: string>
[Server] - /blacklist list [Limit: int]
[Server] - /blacklist reload
[Server] - /blacklist remove <Id: string>
```

> [!TIP|style:callout]
> Here, `blacklist` is the top-level command of Blacklist (permission level: GameDirectors).

- `/blacklist add <Target: target> [Cause: string] [Time: int]`
  - Add a target to the blacklist.
  - Here, `<Target: target>` is the target selector.
  - Here, `<Cause: string>` is the reason for the addition.
  - Here, `<Time: int>` is the duration of the addition (in minutes).

- `/blacklist gui`
  - Open the blacklist GUI.

- `/blacklist info <Id: string>`
  - View the information of a target in the blacklist.
  - Here, `<Id: string>` is the target's ID.

- `/blacklist list [Limit: int]`
  - List all targets in the blacklist.
  - Here, `[Limit: int]` is the limit on the number of entries displayed (defaults to 100; when set to a negative number, all entries are displayed).

- `/blacklist reload`
  - Reload (recompile) the blacklist GUI.

- `/blacklist remove <Id: string>`
  - Remove a target from the blacklist.
  - Here, `<Id: string>` is the target's ID.

## Mute

```log
[Server] mute:
[Server] LOICollection -> Server mute
[Server] Usage:
[Server] - /mute add <Target: target> [Cause: string] [Time: int]
[Server] - /mute gui
[Server] - /mute info <Id: string>
[Server] - /mute list [Limit: int]
[Server] - /mute reload
[Server] - /mute remove id <Id: string>
[Server] - /mute remove target <Target: target>
```

> [!TIP]
> Here, `mute` is the top-level command of Mute (permission level: GameDirectors).

- `/mute add <Target: target> [Cause: string] [Time: int]`
  - Add a target to the mute list.
  - Here, `<Target: target>` is the target selector.
  - Here, `<Cause: string>` is the reason for the addition.
  - Here, `<Time: int>` is the duration of the addition (in minutes).

- `/mute gui`
  - Open the mute GUI.

- `/mute info <Id: string>`
  - View the information of a target in the mute list.
  - Here, `<Id: string>` is the target's ID.

- `/mute list [Limit: int]`
  - List all targets in the mute list.
  - Here, `[Limit: int]` is the limit on the number of entries displayed (defaults to 100; when set to a negative number, all entries are displayed).

- `/mute reload`
  - Reload (recompile) the mute GUI.

- `/mute remove id <Id: string>`
  - Remove a target from the mute list.
  - Here, `<Id: string>` is the target's ID.

- `/mute remove target <Target: target>`
  - Remove a target from the mute list.
  - Here, `<Target: target>` is the target selector.

## Cdk

```log
[Server] cdk:
[Server] LOICollection -> Redemption Code
[Server] Usage:
[Server] - /cdk convert <Id: string>
[Server] - /cdk edit
[Server] - /cdk gui
[Server] - /cdk reload
```

> [!TIP]
> Here, `cdk` is the top-level command of Cdk (permission level: Any).

- `/cdk convert <Id: string>`
  - Redeem the CDK.
  - Here, `<Id: string>` is the CDK to redeem.

- `/cdk edit`
  - Open the CDK redemption settings (permission level: GameDirectors).

- `/cdk gui`
  - Open the CDK redemption GUI.

- `/cdk reload`
  - Reload (recompile) the CDK redemption GUI (permission level: GameDirectors).

## Menu

```log
[Server] menu:
[Server] LOICollection -> Server menu
[Server] Usage:
[Server] - /menu clock
[Server] - /menu gui [Id: string]
[Server] - /menu reload
```

> [!TIP]
> Here, `menu` is the top-level command of Menu (permission level: Any).

- `/menu clock`
  - Obtain the portable item for opening the menu.

- `/menu gui [Id: string]`
  - Open the menu GUI.
  - Here, `<Id: string>` is the menu ID.

- `/menu reload`
  - Reload (recompile) the menu GUI (permission level: GameDirectors).

## Tpa

```log
[Server] tpa:
[Server] LOICollection -> Player teleport
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
> Here, `tpa` is the top-level command of Tpa (permission level: Any).

- `/tpa accept <Id: string>`
  - Accept a player's teleport request.
  - Here, `<Id: string>` is the teleport request ID.

- `/tpa cancel <Id: string>`
  - Cancel your own teleport request.
  - Here, `<Id: string>` is the teleport request ID.

- `/tpa gui`
  - Open the Tpa GUI.

- `tpa invite <tpa|tphere> <Target: target>`
  - Send a teleport request to the target.
  - Here, `<tpa|tphere>` is the teleport type.
  - Here, `<Target: target>` is the target selector.

- `/tpa reject <Id: string>`
  - Reject a player's teleport request.
  - Here, `<Id: string>` is the teleport request ID.

- `/tpa reload`
  - Reload (recompile) the Tpa GUI (permission level: GameDirectors).

- `/tpa setting`
  - Open Tpa personal settings.

## Shop

```log
[Server] shop:
[Server] LOICollection -> Server shop
[Server] Usage:
[Server] - /shop gui <Id: string>
[Server] - /shop reload
```

> [!TIP]
> Here, `shop` is the top-level command of Shop (permission level: Any).

- `/shop gui <Id: string>`
  - Open the shop GUI.
  - Here, `<Id: string>` is the shop ID.

- `/shop reload`
  - Reload (recompile) the shop GUI (permission level: GameDirectors).

## Pvp

```log
[Server] pvp:
[Server] LOICollection -> Server PvP
[Server] Usage:
[Server] - /pvp gui
[Server] - /pvp off
[Server] - /pvp on
[Server] - /pvp reload
```

> [!TIP]
> Here, `pvp` is the top-level command of Pvp (permission level: Any).

- `/pvp gui`
  - Open the Pvp GUI.

- `/pvp off`
  - Turn off PvP.

- `/pvp on`
  - Turn on PvP.

- `/pvp reload`
  - Reload (recompile) the Pvp GUI (permission level: GameDirectors).

## Wallet

```log
? wallet
[Server] wallet:
[Server] LOICollection -> Personal wallet
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
> Here, `wallet` is the top-level command of Wallet (permission level: Any).

- `/wallet bank`
  - Open the bank savings GUI (deposit, withdrawal, interest query).

- `/wallet gui`
  - Open the personal wallet GUI.

- `/wallet history`
  - View your latest 20 fund records (transfers, red envelopes, bank).

- `/wallet query <PlayerName: string>`
  - Query the latest 50 fund records of a specified player (permission level: GameDirectors).
  - Here, `<PlayerName: string>` is the target player name.

- `/wallet rank`
  - Open the wealth ranking GUI.

- `/wallet rinfo <EnvelopeId: string>`
  - Query the grab details and lucky king of a specified red envelope.
  - Here, `<EnvelopeId: string>` is the red envelope ID.

- `/wallet reload`
  - Reload (recompile) the personal wallet GUI (permission level: GameDirectors).

- `/wallet rstat`
  - View today's global red envelope statistics (count, total amount, top sender, lucky kings).

- `/wallet transfer <Target: target> <Score: int>`
  - Transfer to the target.
  - Here, `<Target: target>` is the target selector.
  - Here, `<Score: int>` is the transfer amount.

- `/wallet wealth`
  - View the personal wallet balance.

## Chat

```log
[Server] chat:
[Server] LOICollection -> Personal title
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
> Here, `chat` is the top-level command of Chat (permission level: Any).

- `/chat add <Target: target> <Title: string> [Time: int]`
  - Add the specified title to the target (permission level: GameDirectors).
  - Here, `<Target: target>` is the target selector.
  - Here, `<Title: string>` is the title name.
  - Here, `[Time: int]` is the title duration (in minutes).

- `/chat remove <Target: target> <Title: string>`
  - Remove the specified title from the target (permission level: GameDirectors).
  - Here, `<Target: target>` is the target selector.
  - Here, `<Title: string>` is the title name.

- `/chat set <Target: target> <Title: string>`
  - Set the specified title for the target (permission level: GameDirectors).
  - Here, `<Target: target>` is the target selector.
  - Here, `<Title: string>` is the title name.
  - Note: A title that the target does not own cannot be set.

- `/chat list <Target: target>`
  - View all titles of the target (permission level: GameDirectors).
  - Here, `<Target: target>` is the target selector.

- `/chat reload`
  - Reload (recompile) the title management GUI (permission level: GameDirectors).

- `/chat gui`
  - Open the title management GUI (permission level: GameDirectors).

- `/chat setting`
  - Open personal title settings.

## Language

```log
[Server] language:
[Server] LOICollection -> Language settings
[Server] Usage:
[Server] - /language reload
[Server] - /language setting
```

> [!TIP]
> Here, `language` is the top-level command of Language (permission level: Any).

- `/language reload`
  - Reload (recompile) the language settings GUI (permission level: GameDirectors).

- `/language setting`
  - Open the personal language settings screen.

## Notice

```log
[Server] notice:
[Server] LOICollection -> Notice system
[Server] Usage:
[Server] - /notice edit
[Server] - /notice gui [Id: string]
[Server] - /notice reload
[Server] - /notice setting
```

> [!TIP]
> Here, `notice` is the top-level command of Notice (permission level: Any).

- `/notice edit`
  - Open the notice editor (permission level: GameDirectors).

- `/notice gui [Id: string]`
  - Open the notice GUI.
  - Here, `[Id: string]` is the notice ID.

- `/notice reload`
  - Reload (recompile) the notice GUI (permission level: GameDirectors).

- `/notice setting`
  - Open notice personal settings.

## Market

```log
[Server] market:
[Server] LOICollection -> Player market
[Server] Usage:
[Server] - /market gui
[Server] - /market reload
[Server] - /market report [Days: int]
[Server] - /market tax
[Server] - /market tax <Rate: string>
```

> [!TIP]
> Here, `market` is the top-level command of Market (permission level: Any).

- `/market gui`
  - Open the player market GUI.

- `/market reload`
  - Reload (recompile) the player market GUI (permission level: GameDirectors).

- `/market report [Days]`
  - View the market report (trade count, turnover, tax, active sellers), default statistics window is 7 days (permission level: GameDirectors).
  - Here, `[Days: int]` is the statistics window in days.

- `/market tax`
  - Show the current market transaction tax rate (permission level: GameDirectors).

- `/market tax <Rate>`
  - Set the market transaction tax rate at runtime, effective immediately and persisted (permission level: GameDirectors).
  - Here, `<Rate: string>` is the tax rate as a decimal (range 0.0 ~ 1.0).

> [!TIP]
> Can you see me? ( •̀ ω •́ )✧

## BehaviourEvent

```log
[Server] behaviorevent:
[Server] LOICollection -> Behaviour event
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
> Here, `behaviorevent` is the top-level command of BehaviourEvent (permission level: GameDirectors).

- `/behaviorevent back position <PositionOrigin: x y z> <PositionTarget: x y z> <Time: int>`
  - Perform a behaviour event rollback within the specified area.
  - Here, `<PositionOrigin: x y z>` is the starting position of the behaviour event rollback.
  - Here, `<PositionTarget: x y z>` is the target position of the behaviour event rollback.
  - Here, `<Time: int>` is the rollback time (in hours).

- `/behaviorevent back range <PositionOrigin: x y z> <Radius: int> <Time: int>`
  - Perform a behaviour event rollback within the specified range.
  - Here, `<PositionOrigin: x y z>` is the starting position of the behaviour event rollback.
  - Here, `<Radius: int>` is the radius of the behaviour event rollback.
  - Here, `<Time: int>` is the rollback time (in hours).

- `/behaviorevent clean`
  - Clear all behaviour event records that meet the conditions.

- `/behaviorevent query action position <PositionOrigin: x y z> <PositionTarget: x y z> [Limit: int]`
  - Query behaviour events in the specified area.
  - Here, `<PositionOrigin: x y z>` is the starting position of the behaviour event query.
  - Here, `<PositionTarget: x y z>` is the target position of the behaviour event query.
  - Here, `[Limit: int]` is the limit on the number of entries displayed (defaults to 100; when set to a negative number, all entries are displayed).

- `/behaviorevent query action range <PositionOrigin: x y z> <Radius: int> [Limit: int]`
  - Query behaviour events within the specified range.
  - Here, `<PositionOrigin: x y z>` is the starting position of the behaviour event query.
  - Here, `<Radius: int>` is the radius of the behaviour event query.
  - Here, `[Limit: int]` is the limit on the number of entries displayed (defaults to 100; when set to a negative number, all entries are displayed).

- `/behaviorevent query event custom <Target: string> <Value: string> [Limit: int]`
  - Query the specified custom event.
  - Here, `<Target: string>` is the event target.
  - Here, `<Value: string>` is the event value.
  - Here, `[Limit: int]` is the limit on the number of entries displayed (defaults to 100; when set to a negative number, all entries are displayed).

- `/behaviorevent query event dimension <Dimension: Dimension> [Limit: int]`
  - Query behaviour events in the specified dimension.
  - Here, `<Dimension: Dimension>` is the event dimension.
  - Here, `[Limit: int]` is the limit on the number of entries displayed (defaults to 100; when set to a negative number, all entries are displayed).

- `/behaviorevent query event foundation <EventName: string> <Time: int> [Limit: int]`
  - Query behaviour events with the specified basic information.
  - Here, `<EventName: string>` is the event name.
  - Here, `<Time: int>` is the event occurrence time.
  - Here, `[Limit: int]` is the limit on the number of entries displayed (defaults to 100; when set to a negative number, all entries are displayed).

- `/behaviorevent query event info <EventId: string>`
  - Query the detailed information of the specified event.
  - Here, `<EventId: string>` is the event ID.

- `/behaviorevent query event name <EventName: string> [Limit: int]`
  - Query behaviour events with the specified name.
  - Here, `<EventName: string>` is the event name.
  - Here, `[Limit: int]` is the limit on the number of entries displayed (defaults to 100; when set to a negative number, all entries are displayed).

- `/behaviorevent query event position <PositionOrigin: x y z> [Limit: int]`
  - Query behaviour events at the specified position.
  - Here, `<PositionOrigin: x y z>` is the event occurrence position.
  - Here, `[Limit: int]` is the limit on the number of entries displayed (defaults to 100; when set to a negative number, all entries are displayed).

- `/behaviorevent query event site <PositionOrigin: x y z> <Dimension: Dimension> [Limit: int]`
  - Query behaviour events in the specified area.
  - Here, `<PositionOrigin: x y z>` is the event occurrence position.
  - Here, `<Dimension: Dimension>` is the event occurrence dimension.
  - Here, `[Limit: int]` is the limit on the number of entries displayed (defaults to 100; when set to a negative number, all entries are displayed).

- `/behaviorevent query event time <Time: int> [Limit: int]`
  - Query behaviour events within the specified time range.
  - Here, `<Time: int>` is the event occurrence time.
  - Here, `[Limit: int]` is the limit on the number of entries displayed (defaults to 100; when set to a negative number, all entries are displayed).

## Statistics

```log
[Server] statistics:
[Server] LOICollection -> Player data statistics
[Server] Usage:
[Server] - /statistics gui
[Server] - /statistics gui <Type: LOICollection::Plugins::StatisticType>
[Server] - /statistics reload
```

> [!TIP]
> Here, `statistics` is the top-level command of Statistics (permission level: Any).

- `/statistics gui`
  - Open the player data statistics GUI.

- `/statistics gui <Type: LOICollection::Plugins::StatisticType>`
  - Open the data statistics GUI of the specified type.
  - Here, `<Type: LOICollection::Plugins::StatisticType>` is the data statistics type.

- `/statistics reload`
  - Reload (recompile) the player data statistics GUI (permission level: GameDirectors).

> [!NOTE]
> All of the above is the command list for LOICollectionA version 1.15.0; the command list in later versions may differ.
