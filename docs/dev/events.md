# 自定义事件

除 LeviLamina 与 Minecraft 原生事件外，LOICollectionA 还对外发布了 12 个自定义事件头文件（共 22 个事件类型），位于 `src/LOICollectionA/include/server/Events/`。它们都使用 `ll::event::EventBus` 发布，因此可以用与其他事件完全相同的方式监听。

> [!NOTE]
> 所有这些头文件都是服务端专用的。`xmake.lua` 在 client 目标下会通过 `remove_headerfiles("src/LOICollectionA/include/(server/**.h)")` 把它们从分发清单中移除。

## 事件总表

| 头文件 | 事件类型 | 暴露的 Getter | 发布者 | 触发时机 |
| --- | --- | --- | --- | --- |
| `modules/BlacklistEvent.h` | `BlacklistAddBeforeEvent` | `getCause`、`getTime` | Events 适配器 | `BlacklistPlugin::addBlacklist` 执行前，可取消 |
| `modules/BlacklistEvent.h` | `BlacklistAddAfterEvent` | `getCause`、`getTime` | Events 适配器 | `BlacklistPlugin::addBlacklist` 执行后 |
| `modules/BlacklistEvent.h` | `BlacklistRemoveEvent` | `getTarget` | Events 适配器 | `BlacklistPlugin::delBlacklist` 执行前，可取消 |
| `modules/MarketItemSoldEvent.h` | `MarketItemSoldEvent` | `getItemName`、`getPrice`、`getTax`、`getBuyerUuid`、`getSellerUuid`、`getTime` | MarketPlugin | 一笔交易成交时（玩家商店、求购单与拍卖三个成交路径都会发布） |
| `modules/MuteEvent.h` | `MuteAddBeforeEvent` | `getCause`、`getTime` | Events 适配器 | `MutePlugin::addMute` 执行前，可取消 |
| `modules/MuteEvent.h` | `MuteAddAfterEvent` | `getCause`、`getTime` | Events 适配器 | `MutePlugin::addMute` 执行后，可取消 |
| `modules/MuteEvent.h` | `MuteRemoveEvent` | `getTarget` | Events 适配器 | `MutePlugin::delMute` 执行前，可取消 |
| `modules/NoticeEvent.h` | `NoticeCreateEvent` | `getTarget`、`getTitle`、`getPriority`、`isPoiontout` | Events 适配器 | `NoticePlugin::create` 执行前，可取消 |
| `modules/NoticeEvent.h` | `NoticeDeleteEvent` | `getTarget` | Events 适配器 | `NoticePlugin::remove` 执行前，可取消 |
| `modules/RedEnvelopeCompletedEvent.h` | `RedEnvelopeCompletedEvent` | `getEnvelopeId`、`getSenderUuid`、`getKingUuid`、`getKingName`、`getKingAmount`、`getTotal`、`getTime` | WalletPlugin | 一个红包被领完时 |
| `modules/WalletTransferEvent.h` | `WalletTransferEvent` | `getFromUuid`、`getFromName`、`getToUuid`、`getToName`、`getAmount`、`getFee`、`getType`、`getTime` | WalletPlugin | 一笔转账或资金变动被记入台账时 |
| `player/PlayerContainerEvent.h` | `PlayerOpenContainerEvent` | `getPosition`、`getDimensionId` | Events 适配器 | 玩家打开容器时，可取消 |
| `player/PlayerHurtEvent.h` | `PlayerHurtEvent` | `getSource`、`getDamage`、`isKnock`、`isIgnite`、`getReason` | Events 适配器 | 玩家受到其他实体伤害时，可取消 |
| `player/PlayerScoreChangedEvent.h` | `PlayerScoreChangedEvent` | `getObjective`、`getScoreChangedType`、`getScore` | Events 适配器 | 玩家计分板分数变化时，可取消 |
| `server/NetworkPacketEvent.h` | `NetworkPacketBeforeEvent` | `getNetworkIdentifier`、`getPacket`、`getSubClientId`、`getType` | Events 适配器 | 数据包发送 / 接收前后，可取消 |
| `server/NetworkPacketEvent.h` | `NetworkPacketAfterEvent` | `getNetworkIdentifier`、`getPacket`、`getSubClientId`、`getType` | Events 适配器 | 数据包发送 / 接收之后 |
| `server/NetworkPacketEvent.h` | `NetworkBroadcastPacketEvent` | `getPacket` | Events 适配器 | 广播数据包时，可取消 |
| `world/BlockExplodedEvent.h` | `BlockExplodedEvent` | `getPosition`、`getBlock`、`getDimensionId`、`getSource` | Events 适配器 | 方块被爆炸破坏时 |
| `world/RedStoneEvent.h` | `RedStoneEvent` | `getPosition`、`getSource`、`getDimensionId`、`getStrength`、`isFirstTime` | Events 适配器 | 红石线、红石火把、比较器或观察者发生红石更新时 |

> [!NOTE]
> 上表中的「Events 适配器」指 `src/LOICollectionA/modules/server/Events/**` 下的桥接实现。它们通过 `LL_TYPE_INSTANCE_HOOK` + `ll::event::Emitter` 自行注册，因此**与模块开关无关**：只要插件被加载，这些 hook 就已经生效。

## 事件的层次

### 可取消事件

多数事件继承 `ll::event::Cancellable`，监听器可以调用 `event.cancel()` 来阻止默认行为。是否真正生效取决于发布者的处理：

- `BlacklistAddBeforeEvent`、`BlacklistRemoveEvent`、`MuteAddBeforeEvent`、`MuteRemoveEvent`、`NoticeCreateEvent`、`NoticeDeleteEvent`：取消后对应插件方法直接返回，**不写入数据库**
- `PlayerOpenContainerEvent`、`PlayerHurtEvent`、`PlayerScoreChangedEvent`：取消后 hook 返回 `EventResult::StopProcessing` 或提前返回，**原版行为被拦截**
- `NetworkPacketBeforeEvent`、`NetworkBroadcastPacketEvent`：取消后数据包不再发送

> [!WARNING]
> `BlacklistAddAfterEvent` 与 `MuteAddAfterEvent` 在数据库写入**已经完成之后**发布，此时取消事件不会回滚已落库的数据。两者的基类并不一致：`BlacklistAddAfterEvent` 继承普通 `PlayerEvent`，而 `MuteAddAfterEvent` 继承 `Cancellable<PlayerEvent>`，因此它虽然可以调用 `cancel()`，但同样没有回滚效果。

### 不可取消事件

以下事件只用于通知，无法拦截：`MarketItemSoldEvent`、`RedEnvelopeCompletedEvent`、`WalletTransferEvent`、`BlockExplodedEvent`、`RedStoneEvent`、`NetworkPacketAfterEvent`。

### 枚举参数

三个事件带有可用来分支的枚举：

| 枚举 | 取值 | 所属事件 |
| --- | --- | --- |
| `PlayerHurtReason` | `Hurt`、`Effect`、`Projectile` | `PlayerHurtEvent` |
| `ScoreChangedType` | `add`、`reduce` | `PlayerScoreChangedEvent` |
| `NetworkPacketType` | `send`、`receive` | `NetworkPacketBeforeEvent` / `NetworkPacketAfterEvent` |

## 监听事件

```cpp
void XxxPlugin::listenEvent() {
    ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();

    this->mImpl->myListener = eventBus.emplaceListener<LOICollection::server::Events::PlayerScoreChangedEvent>(
        [this](LOICollection::server::Events::PlayerScoreChangedEvent& event) -> void {
            if (event.getScoreChangedType() == LOICollection::server::Events::ScoreChangedType::reduce)
                return;

            // event.cancel(); // 可取消事件才需要
        });
}

void XxxPlugin::unlistenEvent() {
    ll::event::EventBus::getInstance().removeListener(this->mImpl->myListener);
}
```

> [!TIP]
> 事件类型都位于 `LOICollection::server::Events` 命名空间下，头文件路径以 `LOICollectionA/include/server/Events/` 开头，例如 `#include "LOICollectionA/include/server/Events/player/PlayerScoreChangedEvent.h"`。

> [!WARNING]
> 请务必在模块的 `unregistry()` 中调用 `unlistenEvent()`，否则模块被卸载后监听器仍会持有悬空的 `this` 指针。

## 相关文档

- [模块开发指南](./module.md) —— 模块生命周期与事件监听
- [模块总览](../md/modules.md) —— 各模块的用途与开关
- [架构概览](./architecture.md) —— 模块系统与服务容器
