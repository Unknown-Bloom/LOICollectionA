# 插件 API 参考

LOICollectionA 的每个内置插件都会导出一组 C++ API，供其他插件或扩展直接调用。本文列出这些接口的**调用约定**，并以 Chat、Wallet、Market 与 BehaviorEvent 四个插件为例说明其用法。

> [!NOTE]
> 以下内容取自 LOICollectionA 1.17.1 的代码结构，对于后续版本可能会有所不同。

> [!TIP]
> 若您想扩展的是 LCUI 脚本可用的变量与函数，请阅读 [LOICollectionAPI 扩展指南](./api-extension.md)；本文介绍的是**模块之间的 C++ 接口**。

## 通用约定

### 通过 getShared() 获取单例

每个插件都是一个进程内单例，通过静态 `getShared()` 获取：

```cpp
#include "LOICollectionA/include/server/Plugins/wallet/WalletPlugin.h"

using namespace LOICollection::server::Plugins;

std::shared_ptr<WalletPlugin> wallet = WalletPlugin::getShared();
```

`getShared()` 永远不会返回 `nullptr`——它使用静态局部变量构造实例。因此**不要**用空指针判断插件是否可用，而要用 `isValid()`：

```cpp
if (!WalletPlugin::getShared()->isValid())
    return; // 插件未启用或尚未完成 registry()
```

> [!WARNING]
> `isValid()` 反映的是插件当前的注册状态。在服务器尚未完成启动、或模块被配置停用时，它会返回 `false`，此时调用其他 API 通常得到 `Invalid` 错误。请始终先判断 `isValid()`。

### ll::Expected 返回值

除少数简单 getter 外，所有可能失败的操作都返回 `ll::Expected<T>`（无返回值时为 `ll::Expected<void>`）。成功时用 `has_value()` 判断，取值用 `*` 或 `.value()`；失败时用 `.error()` 拿到 `ll::Error`：

```cpp
auto result = ChatPlugin::getShared()->getTitle(player);
if (!result)
    ChatPlugin::getShared()->getLogger()->error("failed: {}", result.error().message());
```

`ll::Expected` 支持链式调用，模块内部常用 `.and_then(...)` / `.or_else(...)`。在模块实现里，`modules::defaultErrorHandler<XxxPlugin>` 是一个现成的错误处理器，会把错误写入该插件自己的日志：

```cpp
this->addXxx(*pl, param.Time).or_else(modules::defaultErrorHandler<XxxPlugin>);
```

### 错误码与错误类别

每个插件都定义了自己的 `XxxPluginErrorCode` 枚举与 `XxxPluginErrorCategory`（`std::error_category` 子类），并把枚举转换为 `std::error_code` 的静态工厂也一并导出：

```cpp
std::error_code ec = WalletPlugin::makeErrorCode(WalletPluginErrorCode::BelowMinimum);
// ec.message() == "Transfer amount below minimum"
```

因此可以根据错误码精确分支，而不必解析错误文本：

```cpp
auto result = WalletPlugin::getShared()->forTransfer(player, target, name, score);
if (!result) {
    if (result.error().code() == WalletPlugin::makeErrorCode(WalletPluginErrorCode::ConfirmRequired)) {
        // 大额转账，需要 GUI 二次确认
    }
}
```

各插件的错误码数量差异较大：`ChatPluginErrorCode` 只有 3 个（`Invalid`、`TitleNotFound`、`BlacklistNotFound`），而 `MarketPluginErrorCode` 有 19 个，覆盖交易、商店、求购与拍卖的各种失败路径。

### 日志

需要记录日志时不要自行创建 logger，使用插件导出的 `getLogger()`，这样输出会带上插件自己的名字与级别：

```cpp
WalletPlugin::getShared()->getLogger()->info("balance: {}", amount);
```

### 数据库与 Executor

部分插件额外导出 `getDatabase()`，返回该插件使用的 `BlockRepository`，可用于只读查询或与 [SQLite 层](./sqlite.md) 配合。

需要把工作调度到服务器线程 / 特定执行器时，使用插件导出的 `setExecutor`：

```cpp
WalletPlugin::getShared()->setExecutor(ll::thread::ServerThreadExecutor::getDefault());
```

> [!NOTE]
> `setExecutor` 会替换插件内部用于异步任务的执行器。测试中常用 `MockExecutor` 替换它，从而确定性地推进时间，见 [测试指南](./testing.md)。

### 测试专用接口

个别插件导出以 `ForTest` 结尾的接口，它们**不属于稳定 API**，仅用于测试：

| 接口 | 所属 | 说明 |
| --- | --- | --- |
| `setOptionsForTest(const Config::C_Wallet&)` | `WalletPlugin` | 临时替换钱包配置，用于在同一进程内测试不同配置 |

## Chat：聊天称号与黑名单

头文件：`LOICollectionA/include/server/Plugins/ChatPlugin.h`

| 方法 | 说明 |
| --- | --- |
| `setTitle(Player&, const std::string& text)` | 设置玩家当前佩戴的称号 |
| `addTitle(Player&, const std::string& text, int time)` | 为玩家添加一个限时称号 |
| `delTitle(Player&, const std::string& text)` | 删除玩家的某个称号 |
| `getTitle(Player&)` | 获取玩家当前佩戴的称号 |
| `getTitleTime(Player&, const std::string& text)` | 获取指定称号的剩余时间 |
| `getTitles(Player&)` | 列出玩家的全部称号 |
| `hasTitle(Player&, const std::string& text)` | 判断玩家是否拥有某个称号 |
| `addBlacklist(Player&, Player& target)` | 把目标玩家加入自己的聊天黑名单 |
| `delBlacklist(Player&, const std::string& id)` | 按 id 移出聊天黑名单 |
| `getBlacklist(Player&, Player& target)` | 查询两个玩家之间的黑名单记录 |
| `getBlacklist(Player&)` | 列出该玩家黑名单中的所有 id |
| `getBlacklistData(const std::string& id)` | 取出一条黑名单记录的全部字段 |
| `hasBlacklist(Player&, const std::string& id)` | 判断某条黑名单记录是否存在 |
| `getBlacklistUpload()` | 该玩家的黑名单上传上限 |
| `isValid()` | 插件是否可用 |

```cpp
auto chat = ChatPlugin::getShared();
if (chat->isValid())
    chat->addTitle(player, "VIP", 86400).or_else(modules::defaultErrorHandler<ChatPlugin>);
```

## Wallet：玩家经济

头文件：`LOICollectionA/include/server/Plugins/wallet/WalletPlugin.h`

错误码 `WalletPluginErrorCode`：`Invalid`、`NotFound`、`RedEnvelopeCompleted`、`BelowMinimum`、`DailyLimitExceeded`、`CooldownActive`、`ConfirmRequired`、`BankEmpty`、`BelowMinDeposit`、`RedEnvelopeCountExceeded`、`NotInTargetList`。

| 分组 | 方法 | 说明 |
| --- | --- | --- |
| 玩家信息 | `getPlayerInfo(const std::string& uuid)` | 取单个玩家的钱包信息 |
| | `getPlayerInfo()` | 取全部玩家的钱包信息 |
| | `updateBalanceSnapshot(const std::string& uuid, long long balance)` | 刷新余额快照 |
| 转账 | `forTransfer(Player&, const std::string& target, const std::string& name, int score, bool confirmed = false)` | 发起转账；大额时返回 `ConfirmRequired` |
| | `transfer(const std::string& target, int score)` | 内部实际扣款与入账 |
| 排行 | `wealth(Player&)` | 向玩家展示财富排行 |
| | `getWealthRanking(int limit)` | 获取财富排行（uuid + 余额） |
| | `getWealthRank(const std::string& uuid)` | 获取单个玩家的名次与余额 |
| | `rebuildWealthRanking()` | 重建排行缓存 |
| 红包 | `redenvelope(Player&, const std::string& key, int score, int count, const std::vector<std::string>& targets = {})` | 发红包 |
| | `tryGrabRedEnvelope(Player&, const std::string& message)` | 尝试抢红包 |
| | `sweepExpiredEnvelopes()` | 清理过期红包 |
| | `getEnvelopeStats(const std::string& id)` | 单个红包的领取统计 |
| | `getRedEnvelopeDailyStats()` | 红包日报统计 |
| | `computeGiftAmount(int remainingCapacity, int remainingPeople)` | 静态方法，计算下一份红包金额 |
| 银行 | `bankDeposit(Player&, int amount)` | 存款 |
| | `bankWithdraw(Player&)` | 取款（本金 + 利息） |
| | `getBankPrincipal(const std::string& uuid)` | 查询本金 |
| | `getBankInterest(const std::string& uuid)` | 查询利息 |
| 台账 | `getFeePool()` | 当前手续费钱池余额 |
| | `getPlayerLedger(const std::string& uuid, int limit)` | 玩家资金流水 |
| | `sendHistory(Player& receiver, const std::string& uuid, const std::string& name, int limit)` | 把流水发送给玩家 |
| 配置 | `getOptions()` | 当前钱包配置（`Config::C_Wallet`） |
| | `getTargetScoreboard()` | 使用的 Score 对象名 |
| | `getExchangeRate()` | 汇率 |

```cpp
auto wallet = WalletPlugin::getShared();
if (!wallet->isValid())
    return;

auto result = wallet->redenvelope(player, key, /*score=*/1000, /*count=*/5);
if (!result && result.error().code() == WalletPlugin::makeErrorCode(WalletPluginErrorCode::RedEnvelopeCountExceeded))
    player.sendMessage("份数超出上限");
```

## Market：交易市场

头文件：`LOICollectionA/include/server/Plugins/market/MarketPlugin.h`

`MarketPlugin` 是接口最多的插件，`MarketPluginErrorCode` 共 19 个错误码，覆盖交易、商店、求购与拍卖四组失败路径（如 `TradeNotFound`、`StoreAlreadyExists`、`WantedExpired`、`AuctionBidTooLow`、`CompensationRequired`、`TaxRateInvalid`）。

| 分组 | 方法 | 说明 |
| --- | --- | --- |
| 挂单 | `addItem(Player&, ItemStack&, name, icon, intr, score)` | 把手中物品上架 |
| | `delItem(const std::string& id)` | 下架并删除 |
| | `offshelfItem(Player&, const std::string& id, bool returnItem = false)` | 下架（可选择是否退回物品） |
| | `buyItem(Player&, const std::string& id)` | 购买挂单 |
| | `getItems()` / `getItems(Player&)` | 列出挂单 id |
| | `getItemData(const std::string& id)` | 单条挂单的字段 |
| | `getItemsData(const std::vector<std::string>& ids)` | 批量取字段 |
| | `hasItem(const std::string& id)` | 挂单是否存在 |
| 玩家交易 | `sendRequest(Player&, Player&, MarketTradeType)` | 发起交易请求 |
| | `acceptRequest(Player&)` / `rejectRequest(Player&)` / `cancelRequest(Player&)` | 处理交易请求 |
| | `sendTrade(Player&, Player&, MarketTradeType)` | 进入交易界面 |
| | `acceptTrade(Player&, int slot, int score)` / `cancelTrade(Player&)` | 完成或取消交易 |
| | `hasTrade(Player&)` | 玩家是否正在交易 |
| 黑名单 | `addBlacklist` / `delBlacklist` / `getBlacklist` / `hasBlacklist` / `getBlacklistData` | 与 Chat 同形，另提供按目标名查询的重载 |
| 商店 | `createStore(Player&, name, icon, introduce)` / `dissolveStore(Player&)` | 创建 / 解散商店 |
| | `uploadStoreItem` / `offshelfStoreItem` / `buyStoreItem(Player&, id, count = 1)` | 商店商品的上下架与购买 |
| | `getStore(id)` / `getStore(Player&)` / `getStoreItems(storeId)` / `getStoreItemData(id)` | 商店查询 |
| | `getStoreRanking()` / `hasPurchasedInStore(Player&, storeId)` | 排行与购买记录 |
| 评价 | `addReview(Player&, storeId, rating, content)` / `auditReview(Player&, id, approve)` | 发表 / 审核评价 |
| | `getReviews(storeId, MarketStoreReviewStatus)` / `getReviewData(id)` | 查询评价 |
| 行情 | `getQuote(itemName)` / `getTopVolume(limit, days = 30)` / `getTopTurnover(limit, days = 30)` / `getReport(days)` | 成交均价与排行 |
| 求购 | `createWanted(Player&, slot, name, unitPrice, amount)` / `cancelWanted` / `fillWanted(Player&, id, amount)` | 求购单的创建、取消与成交 |
| | `getWantedList()` / `getWantedItems(Player&)` / `getWantedData(id)` | 求购查询 |
| 拍卖 | `createAuction(Player&, slot, name, startPrice, durationSeconds)` / `bidAuction(Player&, id, price)` | 创建拍卖与出价 |
| | `getAuctionList()` / `getAuctionItems(Player&)` / `getAuctionData(id)` | 拍卖查询 |
| 静态工具 | `computeStoreScore(StoreScoreInput const&, Config::C_Market const&)` | 商店综合评分 |
| | `computeTax(int price, double rate)` | 税费计算 |
| | `isPriceAboveCeiling(price, referencePrice, ratio)` | 价格上限判断 |
| 税率 | `getTaxRate()` / `setTaxRate(double rate)` / `guardPriceCeiling(Player&, itemName, price)` | 运行时税率与价格守卫 |
| 表访问 | `stores()` / `items()` / `sales()` / `reviews()` | 直接获取 `TypedTable`，用于自定义查询 |
| 缓存 | `clearStoreRankCache()` / `startStoreRankRefresh()` | 商店排行缓存控制 |

> [!WARNING]
> `stores()` / `items()` / `sales()` / `reviews()` 返回的是底层表对象，直接写入会绕过插件的业务校验（税费、价格上限、状态机）。除非您明确知道后果，否则请使用上表中的高层方法。

## BehaviorEvent：行为事件日志

头文件：`LOICollectionA/include/server/Plugins/behaviorevent/BehaviorEventPlugin.h` 与 `.../BehaviorEventLog.h`

`BehaviorEventPlugin` 负责采集与查询，`BehaviorEventLog` 是与块存储直接打交道的**数据访问层**。

### BehaviorEventPlugin

| 方法 | 说明 |
| --- | --- |
| `getBehaviorEventLog()` | 取得底层日志对象（`observer<BehaviorEventLog>`） |
| `getBasicEvent(name, type, position, dimension)` | 构造一个基础事件（类型为嵌套的 `BehaviorEventPlugin::Event`） |
| `getEvents(int limit = -1)` | 取最近的事件 id |
| `getEventsWithin(int hours, int limit = -1)` | 取指定小时数内的事件 |
| `getEvents(conditions, filter = {}, limit = -1)` | 按条件与自定义过滤器查询 |
| `getEventsByPosition(dimension, filter(x,y,z), limit = -1)` | 按坐标范围查询 |
| `filter(const std::vector<std::string>& ids)` | 过滤出仍存在的事件 id |
| `write(const BehaviorEventPlugin::Event&)` | 写入一个事件，返回其 id |
| `back(const std::vector<std::string>& ids)` | 回溯（读取）指定事件 |
| `clean(int hours)` | 清理早于指定小时数的事件 |
| `setExecutor(const ll::coro::Executor&)` | 设置异步执行器 |

### BehaviorEventLog 数据访问 API

`BehaviorEventLog::create(BlockStore& store, std::string_view rootName)` 是唯一的构造入口，返回 `ll::Expected<std::unique_ptr<BehaviorEventLog>>`（构造函数私有）。

数据结构：

```cpp
using Row  = std::unordered_map<std::string, std::string>;
using Rows = std::unordered_map<std::string, Row>;

struct PreparedEvent {
    std::string name;      // 事件名，如 "onPlayerChat"
    std::string type;      // 事件类型
    std::int64_t timestamp = 0;
    std::int64_t actor = 0;
    std::int64_t posX = 0;
    std::int64_t posY = 0;
    std::int64_t posZ = 0;
    std::int64_t dimension = 0;
    std::vector<std::pair<std::string, std::string>> fields; // 自定义字段
};
```

| 方法 | 说明 |
| --- | --- |
| `append(const PreparedEvent&)` | 写入单个事件，返回 `BlockId` |
| `appendMany(std::span<const PreparedEvent>)` | 批量写入，返回 `BlockId` 列表 |
| `read(BlockId)` / `read(std::span<const BlockId>)` | 读取单个 / 批量事件为 `Row` / `Rows` |
| `all(size_t limit = 0)` | 全部事件 id（`0` 表示不限） |
| `count()` | 事件总数 |
| `byTimeRange(from, to, limit = 0)` | 按时间区间查询 |
| `byName(name, limit = 0)` | 按事件名查询 |
| `byType(type, limit = 0)` | 按类型查询 |
| `byActor(actor, limit = 0)` | 按 actor 查询 |
| `byDimension(dimension, limit = 0)` | 按维度查询 |
| `byPosition(x, y, z, limit = 0)` | 按坐标查询 |
| `erase(std::span<const BlockId>)` | 删除事件 |
| `archiveBefore(std::int64_t timestamp)` | 归档早于时间戳的事件，返回处理数量 |
| `root()` | 日志的根块 `BlockId` |

配套的自由函数 `readBehaviorEventLogFieldOf(Row const&, std::string_view key)` 可在字段缺失时安全地返回空字符串：

```cpp
auto rows = log->read(id);
if (rows) {
    std::string name = readBehaviorEventLogFieldOf((*rows)[std::to_string(id)], "name");
}
```

> [!NOTE]
> `BehaviorEventLog` 内部维护了「名称 → 整数 id」的字典缓存（`intern` / `unintern`，带 `mDictMutex` 保护），因此按名称查询时不必担心字符串比较开销；但这也意味着**不要**绕过该类直接改写底层的 `dict` 表。

## 相关文档

- [模块开发指南](./module.md) —— 如何编写一个新模块并导出自己的 API
- [LOICollectionAPI 扩展指南](./api-extension.md) —— 扩展脚本可用的变量与函数
- [SQLite 层使用教程](./sqlite.md) —— `BlockRepository` / `TypedTable` 的用法
- [自定义事件](./events.md) —— 插件对外发布的事件
- [架构概览](./architecture.md) —— 服务容器与模块优先级
