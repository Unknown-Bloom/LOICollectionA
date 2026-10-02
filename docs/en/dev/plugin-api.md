# Plugin API Reference

Every built-in plugin in LOICollectionA exports a set of C++ APIs that other plugins or extensions can call directly. This page documents the **calling conventions** of those interfaces and illustrates them with four plugins: Chat, Wallet, Market and BehaviorEvent.

> [!NOTE]
> The following content is taken from the code structure of LOICollectionA 1.17.1 and may differ in later versions.

> [!TIP]
> If what you want to extend is the set of variables and functions available to LCUI scripts, read the [LOICollectionAPI Extension Guide](./api-extension.md); this page covers the **C++ interfaces between modules**.

## General conventions

### Getting the singleton through getShared()

Each plugin is a process-wide singleton, obtained through the static `getShared()`:

```cpp
#include "LOICollectionA/include/server/Plugins/wallet/WalletPlugin.h"

using namespace LOICollection::server::Plugins;

std::shared_ptr<WalletPlugin> wallet = WalletPlugin::getShared();
```

`getShared()` never returns `nullptr` — it constructs the instance in a function-local static. So do **not** test availability with a null check; use `isValid()` instead:

```cpp
if (!WalletPlugin::getShared()->isValid())
    return; // the plugin is disabled or has not finished registry() yet
```

> [!WARNING]
> `isValid()` reflects the plugin's current registration state. It returns `false` while the server has not finished starting or when the module is disabled by configuration, and other API calls usually yield an `Invalid` error in that state. Always check `isValid()` first.

### ll::Expected return values

Apart from a few simple getters, every operation that can fail returns `ll::Expected<T>` (`ll::Expected<void>` when there is no value). Test success with `has_value()` and read the value with `*` or `.value()`; on failure, `.error()` gives you the `ll::Error`:

```cpp
auto result = ChatPlugin::getShared()->getTitle(player);
if (!result)
    ChatPlugin::getShared()->getLogger()->error("failed: {}", result.error().message());
```

`ll::Expected` supports chaining, and module internals commonly use `.and_then(...)` / `.or_else(...)`. Inside a module implementation, `modules::defaultErrorHandler<XxxPlugin>` is a ready-made error handler that writes the error to that plugin's own logger:

```cpp
this->addXxx(*pl, param.Time).or_else(modules::defaultErrorHandler<XxxPlugin>);
```

### Error codes and error categories

Every plugin defines its own `XxxPluginErrorCode` enum and `XxxPluginErrorCategory` (a `std::error_category` subclass), and also exports the static factory that turns the enum into a `std::error_code`:

```cpp
std::error_code ec = WalletPlugin::makeErrorCode(WalletPluginErrorCode::BelowMinimum);
// ec.message() == "Transfer amount below minimum"
```

That lets you branch on the exact error instead of parsing error text:

```cpp
auto result = WalletPlugin::getShared()->forTransfer(player, target, name, score);
if (!result) {
    if (result.error().code() == WalletPlugin::makeErrorCode(WalletPluginErrorCode::ConfirmRequired)) {
        // large transfer; the GUI must confirm it
    }
}
```

The number of error codes varies widely between plugins: `ChatPluginErrorCode` has only 3 (`Invalid`, `TitleNotFound`, `BlacklistNotFound`), while `MarketPluginErrorCode` has 19, covering the failure paths of trades, stores, wanted orders and auctions.

### Logging

Do not create your own logger when you need to log; use the plugin's exported `getLogger()` so the output carries that plugin's name and level:

```cpp
WalletPlugin::getShared()->getLogger()->info("balance: {}", amount);
```

### Database and executor

Some plugins additionally export `getDatabase()`, which returns the `BlockRepository` the plugin uses — useful for read-only queries and for working with the [SQLite layer](./sqlite.md).

When you need to schedule work onto the server thread or a specific executor, use the exported `setExecutor`:

```cpp
WalletPlugin::getShared()->setExecutor(ll::thread::ServerThreadExecutor::getDefault());
```

> [!NOTE]
> `setExecutor` replaces the executor the plugin uses for asynchronous work. Tests commonly swap in `MockExecutor` so time can be advanced deterministically; see the [Testing Guide](./testing.md).

### Test-only interfaces

A few plugins export interfaces ending in `ForTest`. They are **not** stable API and exist purely for tests:

| Interface | Owner | Description |
| --- | --- | --- |
| `setOptionsForTest(const Config::C_Wallet&)` | `WalletPlugin` | Temporarily replaces the wallet configuration so different configurations can be tested in one process |

## Chat: chat titles and blacklist

Header: `LOICollectionA/include/server/Plugins/ChatPlugin.h`

| Method | Description |
| --- | --- |
| `setTitle(Player&, const std::string& text)` | Sets the title a player currently wears |
| `addTitle(Player&, const std::string& text, int time)` | Grants a player a time-limited title |
| `delTitle(Player&, const std::string& text)` | Removes one of a player's titles |
| `getTitle(Player&)` | Gets the title a player currently wears |
| `getTitleTime(Player&, const std::string& text)` | Gets the remaining time of a given title |
| `getTitles(Player&)` | Lists all of a player's titles |
| `hasTitle(Player&, const std::string& text)` | Whether a player owns a given title |
| `addBlacklist(Player&, Player& target)` | Adds a target player to your chat blacklist |
| `delBlacklist(Player&, const std::string& id)` | Removes an entry from the chat blacklist by id |
| `getBlacklist(Player&, Player& target)` | Queries the blacklist record between two players |
| `getBlacklist(Player&)` | Lists every id in that player's blacklist |
| `getBlacklistData(const std::string& id)` | Reads all fields of one blacklist record |
| `hasBlacklist(Player&, const std::string& id)` | Whether a given blacklist record exists |
| `getBlacklistUpload()` | That player's blacklist upload limit |
| `isValid()` | Whether the plugin is usable |

```cpp
auto chat = ChatPlugin::getShared();
if (chat->isValid())
    chat->addTitle(player, "VIP", 86400).or_else(modules::defaultErrorHandler<ChatPlugin>);
```

## Wallet: player economy

Header: `LOICollectionA/include/server/Plugins/wallet/WalletPlugin.h`

Error codes `WalletPluginErrorCode`: `Invalid`, `NotFound`, `RedEnvelopeCompleted`, `BelowMinimum`, `DailyLimitExceeded`, `CooldownActive`, `ConfirmRequired`, `BankEmpty`, `BelowMinDeposit`, `RedEnvelopeCountExceeded`, `NotInTargetList`.

| Group | Method | Description |
| --- | --- | --- |
| Player info | `getPlayerInfo(const std::string& uuid)` | Reads one player's wallet info |
| | `getPlayerInfo()` | Reads every player's wallet info |
| | `updateBalanceSnapshot(const std::string& uuid, long long balance)` | Refreshes the balance snapshot |
| Transfers | `forTransfer(Player&, const std::string& target, const std::string& name, int score, bool confirmed = false)` | Starts a transfer; returns `ConfirmRequired` for large amounts |
| | `transfer(const std::string& target, int score)` | The actual debit and credit internally |
| Ranking | `wealth(Player&)` | Shows the wealth ranking to a player |
| | `getWealthRanking(int limit)` | Gets the wealth ranking (uuid + balance) |
| | `getWealthRank(const std::string& uuid)` | Gets one player's rank and balance |
| | `rebuildWealthRanking()` | Rebuilds the ranking cache |
| Red envelopes | `redenvelope(Player&, const std::string& key, int score, int count, const std::vector<std::string>& targets = {})` | Sends a red envelope |
| | `tryGrabRedEnvelope(Player&, const std::string& message)` | Tries to claim a red envelope |
| | `sweepExpiredEnvelopes()` | Cleans up expired red envelopes |
| | `getEnvelopeStats(const std::string& id)` | Claim statistics for one red envelope |
| | `getRedEnvelopeDailyStats()` | Daily red-envelope statistics |
| | `computeGiftAmount(int remainingCapacity, int remainingPeople)` | Static; computes the next share of a red envelope |
| Bank | `bankDeposit(Player&, int amount)` | Deposits |
| | `bankWithdraw(Player&)` | Withdraws (principal plus interest) |
| | `getBankPrincipal(const std::string& uuid)` | Reads the principal |
| | `getBankInterest(const std::string& uuid)` | Reads the interest |
| Ledger | `getFeePool()` | Current fee-pool balance |
| | `getPlayerLedger(const std::string& uuid, int limit)` | A player's fund movements |
| | `sendHistory(Player& receiver, const std::string& uuid, const std::string& name, int limit)` | Sends the ledger to a player |
| Configuration | `getOptions()` | Current wallet configuration (`Config::C_Wallet`) |
| | `getTargetScoreboard()` | The scoreboard objective in use |
| | `getExchangeRate()` | The exchange rate |

```cpp
auto wallet = WalletPlugin::getShared();
if (!wallet->isValid())
    return;

auto result = wallet->redenvelope(player, key, /*score=*/1000, /*count=*/5);
if (!result && result.error().code() == WalletPlugin::makeErrorCode(WalletPluginErrorCode::RedEnvelopeCountExceeded))
    player.sendMessage("share count exceeds the limit");
```

## Market: the trading marketplace

Header: `LOICollectionA/include/server/Plugins/market/MarketPlugin.h`

`MarketPlugin` has the largest interface of any plugin. `MarketPluginErrorCode` holds 19 codes spanning the failure paths of trades, stores, wanted orders and auctions (such as `TradeNotFound`, `StoreAlreadyExists`, `WantedExpired`, `AuctionBidTooLow`, `CompensationRequired`, `TaxRateInvalid`).

| Group | Method | Description |
| --- | --- | --- |
| Listings | `addItem(Player&, ItemStack&, name, icon, intr, score)` | Lists the held item |
| | `delItem(const std::string& id)` | Delists and deletes |
| | `offshelfItem(Player&, const std::string& id, bool returnItem = false)` | Delists, optionally returning the item |
| | `buyItem(Player&, const std::string& id)` | Buys a listing |
| | `getItems()` / `getItems(Player&)` | Lists listing ids |
| | `getItemData(const std::string& id)` | The fields of one listing |
| | `getItemsData(const std::vector<std::string>& ids)` | Fetches fields in bulk |
| | `hasItem(const std::string& id)` | Whether a listing exists |
| Player trades | `sendRequest(Player&, Player&, MarketTradeType)` | Sends a trade request |
| | `acceptRequest(Player&)` / `rejectRequest(Player&)` / `cancelRequest(Player&)` | Handles trade requests |
| | `sendTrade(Player&, Player&, MarketTradeType)` | Opens the trade UI |
| | `acceptTrade(Player&, int slot, int score)` / `cancelTrade(Player&)` | Completes or cancels a trade |
| | `hasTrade(Player&)` | Whether a player is currently trading |
| Blacklist | `addBlacklist` / `delBlacklist` / `getBlacklist` / `hasBlacklist` / `getBlacklistData` | Same shape as Chat, plus an overload that queries by target name |
| Stores | `createStore(Player&, name, icon, introduce)` / `dissolveStore(Player&)` | Creates / dissolves a store |
| | `uploadStoreItem` / `offshelfStoreItem` / `buyStoreItem(Player&, id, count = 1)` | Listing, delisting and buying store items |
| | `getStore(id)` / `getStore(Player&)` / `getStoreItems(storeId)` / `getStoreItemData(id)` | Store queries |
| | `getStoreRanking()` / `hasPurchasedInStore(Player&, storeId)` | Ranking and purchase history |
| Reviews | `addReview(Player&, storeId, rating, content)` / `auditReview(Player&, id, approve)` | Posts / moderates a review |
| | `getReviews(storeId, MarketStoreReviewStatus)` / `getReviewData(id)` | Queries reviews |
| Quotes | `getQuote(itemName)` / `getTopVolume(limit, days = 30)` / `getTopTurnover(limit, days = 30)` / `getReport(days)` | Average prices and rankings |
| Wanted orders | `createWanted(Player&, slot, name, unitPrice, amount)` / `cancelWanted` / `fillWanted(Player&, id, amount)` | Creating, cancelling and filling wanted orders |
| | `getWantedList()` / `getWantedItems(Player&)` / `getWantedData(id)` | Wanted-order queries |
| Auctions | `createAuction(Player&, slot, name, startPrice, durationSeconds)` / `bidAuction(Player&, id, price)` | Creating an auction and bidding |
| | `getAuctionList()` / `getAuctionItems(Player&)` / `getAuctionData(id)` | Auction queries |
| Static helpers | `computeStoreScore(StoreScoreInput const&, Config::C_Market const&)` | Composite store score |
| | `computeTax(int price, double rate)` | Tax computation |
| | `isPriceAboveCeiling(price, referencePrice, ratio)` | Price-ceiling check |
| Tax | `getTaxRate()` / `setTaxRate(double rate)` / `guardPriceCeiling(Player&, itemName, price)` | Runtime tax rate and price guard |
| Table access | `stores()` / `items()` / `sales()` / `reviews()` | Direct `TypedTable` access for custom queries |
| Caches | `clearStoreRankCache()` / `startStoreRankRefresh()` | Store-ranking cache control |

> [!WARNING]
> `stores()` / `items()` / `sales()` / `reviews()` hand back the underlying table objects, so writing through them bypasses the plugin's business validation (tax, price ceiling, state machine). Unless you know exactly what you are doing, use the higher-level methods in the table above.

## BehaviorEvent: the behaviour event log

Headers: `LOICollectionA/include/server/Plugins/behaviorevent/BehaviorEventPlugin.h` and `.../BehaviorEventLog.h`

`BehaviorEventPlugin` handles collection and querying, while `BehaviorEventLog` is the **data-access layer** that talks to block storage directly.

### BehaviorEventPlugin

| Method | Description |
| --- | --- |
| `getBehaviorEventLog()` | Obtains the underlying log object (`observer<BehaviorEventLog>`) |
| `getBasicEvent(name, type, position, dimension)` | Builds a basic event (of the nested type `BehaviorEventPlugin::Event`) |
| `getEvents(int limit = -1)` | Gets the most recent event ids |
| `getEventsWithin(int hours, int limit = -1)` | Gets events within the given number of hours |
| `getEvents(conditions, filter = {}, limit = -1)` | Queries by conditions and a custom filter |
| `getEventsByPosition(dimension, filter(x,y,z), limit = -1)` | Queries by coordinate range |
| `filter(const std::vector<std::string>& ids)` | Narrows to event ids that still exist |
| `write(const BehaviorEventPlugin::Event&)` | Writes an event and returns its id |
| `back(const std::vector<std::string>& ids)` | Backtracks (reads) the given events |
| `clean(int hours)` | Cleans up events older than the given number of hours |
| `setExecutor(const ll::coro::Executor&)` | Sets the asynchronous executor |

### BehaviorEventLog data-access API

`BehaviorEventLog::create(BlockStore& store, std::string_view rootName)` is the only way to construct one, returning `ll::Expected<std::unique_ptr<BehaviorEventLog>>` (the constructor is private).

Data structures:

```cpp
using Row  = std::unordered_map<std::string, std::string>;
using Rows = std::unordered_map<std::string, Row>;

struct PreparedEvent {
    std::string name;      // event name, e.g. "onPlayerChat"
    std::string type;      // event type
    std::int64_t timestamp = 0;
    std::int64_t actor = 0;
    std::int64_t posX = 0;
    std::int64_t posY = 0;
    std::int64_t posZ = 0;
    std::int64_t dimension = 0;
    std::vector<std::pair<std::string, std::string>> fields; // custom fields
};
```

| Method | Description |
| --- | --- |
| `append(const PreparedEvent&)` | Writes one event, returning a `BlockId` |
| `appendMany(std::span<const PreparedEvent>)` | Writes in bulk, returning a list of `BlockId` |
| `read(BlockId)` / `read(std::span<const BlockId>)` | Reads one / many events as `Row` / `Rows` |
| `all(size_t limit = 0)` | Every event id (`0` means unlimited) |
| `count()` | Total number of events |
| `byTimeRange(from, to, limit = 0)` | Queries by time range |
| `byName(name, limit = 0)` | Queries by event name |
| `byType(type, limit = 0)` | Queries by type |
| `byActor(actor, limit = 0)` | Queries by actor |
| `byDimension(dimension, limit = 0)` | Queries by dimension |
| `byPosition(x, y, z, limit = 0)` | Queries by coordinates |
| `erase(std::span<const BlockId>)` | Deletes events |
| `archiveBefore(std::int64_t timestamp)` | Archives events older than the timestamp, returning how many were processed |
| `root()` | The log's root `BlockId` |

The companion free function `readBehaviorEventLogFieldOf(Row const&, std::string_view key)` safely returns an empty string when a field is missing:

```cpp
auto rows = log->read(id);
if (rows) {
    std::string name = readBehaviorEventLogFieldOf((*rows)[std::to_string(id)], "name");
}
```

> [!NOTE]
> `BehaviorEventLog` keeps a name-to-integer-id dictionary cache internally (`intern` / `unintern`, guarded by `mDictMutex`), so querying by name avoids string comparison overhead. That also means you should **not** bypass the class and rewrite the underlying `dict` table directly.

## Related documentation

- [Module Development Guide](./module.md) — how to write a new module and export your own API
- [LOICollectionAPI Extension Guide](./api-extension.md) — extending the variables and functions available to scripts
- [SQLite Layer Tutorial](./sqlite.md) — using `BlockRepository` / `TypedTable`
- [Custom Events](./events.md) — events plugins publish to the outside
- [Architecture Overview](./architecture.md) — the service container and module priorities
