# Modules Overview

LOICollectionA uses a micro-kernel architecture: every feature is provided by a module, and each module can be enabled or disabled independently. This page lists every module with its purpose and where its configuration switch lives; for per-module commands see [Commands](./command.md), and for the configuration structure and data files see [Data Files](./data.md).

## Modules come in layers

Modules fall into three groups, and their configuration paths differ:

| Group | Configuration path | Build target |
| --- | --- | --- |
| Built-in plugins (Plugins) | `ServerConfig.Plugins.*` | Server only |
| Portable tools (ProtableTool) | `ServerConfig.ProtableTool.*` | Server only |
| Client modules | `ClientConfig.*` | Client only |

> [!NOTE]
> All server modules are registered in the same `ModulePriority` sequence; priority only affects load and registration order, not the group a module belongs to. See [Architecture Overview](../dev/architecture.md) for details.

## Server built-in plugins

| Module | Purpose | Config key | Default state |
| --- | --- | --- | --- |
| Blacklist | Block specified players from joining the server, with a reason and duration | `ServerConfig.Plugins.Blacklist.ModuleEnabled` | Disabled |
| Mute | Mute players, with a reason and duration | `ServerConfig.Plugins.Mute` | Disabled |
| Cdk | Create, manage, and redeem CDK codes | `ServerConfig.Plugins.Cdk` | Disabled |
| Menu | Open custom in-game menus and run script-defined actions | `ServerConfig.Plugins.Menu.ModuleEnabled` | Disabled |
| Tpa | Teleport request system with invites, blacklist, timeouts, and fees | `ServerConfig.Plugins.Tpa.ModuleEnabled` | Disabled |
| Shop | Server shops with buying, selling, and paginated browsing | `ServerConfig.Plugins.Shop.ModuleEnabled` | Disabled |
| Monitor | Message enhancement: player name display, join notices, score-change detection, disabled-command detection, dynamic MOTD, and sidebars | `ServerConfig.Plugins.Monitor.ModuleEnabled` | Disabled |
| Pvp | Per-player PvP toggle, optionally listening to damage, potions, and projectiles | `ServerConfig.Plugins.Pvp.ModuleEnabled` | Disabled |
| Wallet | Player economy: transfers, red envelopes, bank savings, wealth ranking, and a funds ledger | `ServerConfig.Plugins.Wallet.ModuleEnabled` | Disabled |
| Chat | Chat titles and per-player chat blacklist, with a customisable chat format | `ServerConfig.Plugins.Chat.ModuleEnabled` | Disabled |
| Language | Records each player's language preference, with a language selection GUI | None | Always enabled |
| Notice | Create announcements and notifications with priorities | `ServerConfig.Plugins.Notice` | Disabled |
| Market | A player-to-player marketplace: listings, player stores, reviews, market quotes, wanted orders, and auctions | `ServerConfig.Plugins.Market.ModuleEnabled` | Disabled |
| BehaviorEvent | Records player and world behaviour events to a database, with backtracking and automatic cleanup | `ServerConfig.Plugins.BehaviorEvent.ModuleEnabled` | Disabled |
| Statistics | Tracks online time, kills, deaths, blocks placed/destroyed, respawns, and joins | `ServerConfig.Plugins.Statistics.ModuleEnabled` | Disabled |

> [!WARNING]
> The `Mute`, `Cdk` and `Notice` entries are **booleans**, not objects, so you write `"Mute": false` / `"Cdk": false` / `"Notice": false` directly — there is no nested `ModuleEnabled` field.

> [!TIP]
> The `Language` module has no configuration switch and is always enabled. It backs API variables such as `{player_language}` / `{player_language_name}` and the built-in `gui/language.lcui` selection screen; disabling it would leave language-dependent variables without a value.

### Monitor sub-switches

Once `Monitor` itself is enabled, it has five sub-features that can be toggled independently, all enabled by default:

| Sub-feature | Config key | Description |
| --- | --- | --- |
| Player name display | `Monitor.BelowName` | Shows API-variable content above players' heads |
| Join / leave notices | `Monitor.ServerToast` | Controlled separately by `Messager.join` / `Messager.leave` |
| Score change detection | `Monitor.ChangeScore` | An empty `ScoreboardLists` means all scores are watched |
| Disabled command detection | `Monitor.DisableCommand` | Commands matching `CommandLists` are blocked |
| Dynamic MOTD | `Monitor.DynamicMotd` | Supports API variables and multi-page rotation |
| Sidebar | `Monitor.Sidebar` | Supports titles and multi-page content |

### Market sub-switches

`Market` has the largest configuration surface, and its main sub-features can likewise be turned off individually:

| Sub-feature | Config key | Default |
| --- | --- | --- |
| Player stores | `Market.StoreEnabled` | Enabled |
| Store reviews | `Market.StoreReviewEnabled` | Enabled |
| Market quotes | `Market.StoreQuoteEnabled` | Enabled |
| Wanted orders | `Market.StoreWantedEnabled` | Enabled |
| Partial fills for wanted orders | `Market.StorePartialBuyEnabled` | Enabled |
| Auctions | `Market.StoreAuctionEnabled` | Enabled |

For every field and its meaning see [Data Files](./data.md#configuration-file).

### BehaviorEvent event switches

Under `ServerConfig.Plugins.BehaviorEvent.Events`, `BehaviorEvent` gives every event three switches: `ModuleEnabled` (whether the event is recorded at all), `RecordDatabase` (whether it is written to the database), and `OutputConsole` (whether it is printed to the console). The recordable events are player connect, disconnect, chat, experience gain, attack, permission change, block destroy, block place, death, item pickup, respawn, item use, container interaction, and block explode — 14 in total, all enabled by default.

## Server portable tools

Portable tools are lower-level hooks and behaviour changes, configured under `ServerConfig.ProtableTool`:

| Module | Purpose | Config key | Default state |
| --- | --- | --- | --- |
| BasicHook | Low-level event hooks. Currently provides FakeSeed: spoofs the world seed in the `StartGame` packet sent to clients | `ProtableTool.BasicHook.ModuleEnabled` | Disabled |
| RedStone | Redstone high-frequency detection: once a single block exceeds the redstone-update threshold within one second, the block is destroyed and the event is logged | `ProtableTool.RedStone` | Disabled (`0`) |
| OrderedUI | Queues multiple UI forms so they open one by one instead of overwriting each other | `ProtableTool.OrderedUI` | Disabled |

> [!TIP]
> `BasicHook.FakeSeed` defaults to `$random`. The value may be a decimal integer (parsed as the seed) or a string that cannot be parsed as an integer — the latter is treated as random, so every server start reports a fresh random seed to clients.

> [!NOTE]
> `RedStone` takes an **integer threshold**, not a boolean: `0` disables it, while `2` means a block is destroyed once it accumulates two or more redstone updates within one second. Be careful with small values.

## Client modules

Only one module is built for the client target (`--target_type=client`): the now-playing notification.

| Module | Purpose | Config key | Default state |
| --- | --- | --- | --- |
| Music | Detects the system media session (matching NetEase Cloud Music by default) and shows a "now playing" card in game, optionally with an audio visualiser | `ClientConfig.Music.ModuleEnabled` | Enabled |

> [!WARNING]
> Music is a **client-only** module. Its source lives in `src/LOICollectionA/modules/client/Plugins/music/` and it is not compiled at all for the server target (the `is_server` branch in `xmake.lua` excludes `modules/client/**.cpp`). It depends on `imgui` and the Windows media session APIs, so it cannot be enabled on a server. Its appearance parameters (card size, font sizes, animation durations, and so on) are documented in [Data Files](./data.md#configuration-file).

## Related documentation

- [Commands](./command.md) — full commands and parameters for every module
- [Data Files](./data.md) — the complete `config.json` structure and per-module data files
- [LOICollectionAPI](./api.md) — variables usable in configuration and scripts
- [Module Development Guide](../dev/module.md) — how to write a new module
- [Custom Events](../dev/events.md) — the 12 event headers modules publish to the outside
