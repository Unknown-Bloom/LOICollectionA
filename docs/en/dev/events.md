# Custom Events

Besides LeviLamina and vanilla Minecraft events, LOICollectionA publishes 12 custom event headers (22 event types in total) under `src/LOICollectionA/include/server/Events/`. They are all published through `ll::event::EventBus`, so you listen to them in exactly the same way as any other event.

> [!NOTE]
> All of these headers are server-only. For the client target, `xmake.lua` removes them from the distribution manifest with `remove_headerfiles("src/LOICollectionA/include/(server/**.h)")`.

## Event reference

| Header | Event type | Exposed getters | Publisher | When it fires |
| --- | --- | --- | --- | --- |
| `modules/BlacklistEvent.h` | `BlacklistAddBeforeEvent` | `getCause`, `getTime` | Events adapters | Before `BlacklistPlugin::addBlacklist` runs; cancellable |
| `modules/BlacklistEvent.h` | `BlacklistAddAfterEvent` | `getCause`, `getTime` | Events adapters | After `BlacklistPlugin::addBlacklist` runs |
| `modules/BlacklistEvent.h` | `BlacklistRemoveEvent` | `getTarget` | Events adapters | Before `BlacklistPlugin::delBlacklist` runs; cancellable |
| `modules/MarketItemSoldEvent.h` | `MarketItemSoldEvent` | `getItemName`, `getPrice`, `getTax`, `getBuyerUuid`, `getSellerUuid`, `getTime` | MarketPlugin | When a trade completes (published by all three settlement paths: player stores, wanted orders and auctions) |
| `modules/MuteEvent.h` | `MuteAddBeforeEvent` | `getCause`, `getTime` | Events adapters | Before `MutePlugin::addMute` runs; cancellable |
| `modules/MuteEvent.h` | `MuteAddAfterEvent` | `getCause`, `getTime` | Events adapters | After `MutePlugin::addMute` runs; cancellable |
| `modules/MuteEvent.h` | `MuteRemoveEvent` | `getTarget` | Events adapters | Before `MutePlugin::delMute` runs; cancellable |
| `modules/NoticeEvent.h` | `NoticeCreateEvent` | `getTarget`, `getTitle`, `getPriority`, `isPoiontout` | Events adapters | Before `NoticePlugin::create` runs; cancellable |
| `modules/NoticeEvent.h` | `NoticeDeleteEvent` | `getTarget` | Events adapters | Before `NoticePlugin::remove` runs; cancellable |
| `modules/RedEnvelopeCompletedEvent.h` | `RedEnvelopeCompletedEvent` | `getEnvelopeId`, `getSenderUuid`, `getKingUuid`, `getKingName`, `getKingAmount`, `getTotal`, `getTime` | WalletPlugin | When a red envelope has been fully claimed |
| `modules/WalletTransferEvent.h` | `WalletTransferEvent` | `getFromUuid`, `getFromName`, `getToUuid`, `getToName`, `getAmount`, `getFee`, `getType`, `getTime` | WalletPlugin | When a transfer or fund movement is written to the ledger |
| `player/PlayerContainerEvent.h` | `PlayerOpenContainerEvent` | `getPosition`, `getDimensionId` | Events adapters | When a player opens a container; cancellable |
| `player/PlayerHurtEvent.h` | `PlayerHurtEvent` | `getSource`, `getDamage`, `isKnock`, `isIgnite`, `getReason` | Events adapters | When a player takes damage from another entity; cancellable |
| `player/PlayerScoreChangedEvent.h` | `PlayerScoreChangedEvent` | `getObjective`, `getScoreChangedType`, `getScore` | Events adapters | When a player's scoreboard score changes; cancellable |
| `server/NetworkPacketEvent.h` | `NetworkPacketBeforeEvent` | `getNetworkIdentifier`, `getPacket`, `getSubClientId`, `getType` | Events adapters | Around packet send / receive; cancellable |
| `server/NetworkPacketEvent.h` | `NetworkPacketAfterEvent` | `getNetworkIdentifier`, `getPacket`, `getSubClientId`, `getType` | Events adapters | After a packet is sent / received |
| `server/NetworkPacketEvent.h` | `NetworkBroadcastPacketEvent` | `getPacket` | Events adapters | When a packet is broadcast; cancellable |
| `world/BlockExplodedEvent.h` | `BlockExplodedEvent` | `getPosition`, `getBlock`, `getDimensionId`, `getSource` | Events adapters | When a block is destroyed by an explosion |
| `world/RedStoneEvent.h` | `RedStoneEvent` | `getPosition`, `getSource`, `getDimensionId`, `getStrength`, `isFirstTime` | Events adapters | When a redstone wire, redstone torch, comparator or observer receives a redstone update |

> [!NOTE]
> "Events adapters" above refers to the bridge implementations under `src/LOICollectionA/modules/server/Events/**`. They register themselves through `LL_TYPE_INSTANCE_HOOK` + `ll::event::Emitter`, so they are **independent of module switches**: as soon as the plugin is loaded, these hooks are already active.

## Event layers

### Cancellable events

Most events inherit `ll::event::Cancellable`, so a listener can call `event.cancel()` to suppress the default behaviour. Whether that actually takes effect depends on the publisher:

- `BlacklistAddBeforeEvent`, `BlacklistRemoveEvent`, `MuteAddBeforeEvent`, `MuteRemoveEvent`, `NoticeCreateEvent`, `NoticeDeleteEvent`: cancelling makes the corresponding plugin method return immediately and **nothing is written to the database**
- `PlayerOpenContainerEvent`, `PlayerHurtEvent`, `PlayerScoreChangedEvent`: cancelling makes the hook return `EventResult::StopProcessing` or return early, so **the vanilla behaviour is blocked**
- `NetworkPacketBeforeEvent`, `NetworkBroadcastPacketEvent`: cancelling stops the packet from being sent

> [!WARNING]
> `BlacklistAddAfterEvent` and `MuteAddAfterEvent` are published **after** the database write has already completed, so cancelling them does not roll back the stored data. Their base classes are not the same either: `BlacklistAddAfterEvent` inherits from plain `PlayerEvent`, while `MuteAddAfterEvent` inherits `Cancellable<PlayerEvent>` — so it can be cancelled, but that still has no rollback effect.

### Non-cancellable events

The following events are notifications only and cannot be intercepted: `MarketItemSoldEvent`, `RedEnvelopeCompletedEvent`, `WalletTransferEvent`, `BlockExplodedEvent`, `RedStoneEvent`, `NetworkPacketAfterEvent`.

### Enum parameters

Three events carry an enum you can branch on:

| Enum | Values | Event |
| --- | --- | --- |
| `PlayerHurtReason` | `Hurt`, `Effect`, `Projectile` | `PlayerHurtEvent` |
| `ScoreChangedType` | `add`, `reduce` | `PlayerScoreChangedEvent` |
| `NetworkPacketType` | `send`, `receive` | `NetworkPacketBeforeEvent` / `NetworkPacketAfterEvent` |

## Listening to events

```cpp
void XxxPlugin::listenEvent() {
    ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();

    this->mImpl->myListener = eventBus.emplaceListener<LOICollection::server::Events::PlayerScoreChangedEvent>(
        [this](LOICollection::server::Events::PlayerScoreChangedEvent& event) -> void {
            if (event.getScoreChangedType() == LOICollection::server::Events::ScoreChangedType::reduce)
                return;

            // event.cancel(); // only needed for cancellable events
        });
}

void XxxPlugin::unlistenEvent() {
    ll::event::EventBus::getInstance().removeListener(this->mImpl->myListener);
}
```

> [!TIP]
> Every event type lives in the `LOICollection::server::Events` namespace and its header path starts with `LOICollectionA/include/server/Events/`, for example `#include "LOICollectionA/include/server/Events/player/PlayerScoreChangedEvent.h"`.

> [!WARNING]
> Always call `unlistenEvent()` from the module's `unregistry()`, otherwise a listener outlives the module and keeps a dangling `this` pointer.

## Related documentation

- [Module Development Guide](./module.md) — module lifecycle and event listening
- [Modules Overview](../md/modules.md) — purpose and switches of every module
- [Architecture Overview](./architecture.md) — the module system and service container
