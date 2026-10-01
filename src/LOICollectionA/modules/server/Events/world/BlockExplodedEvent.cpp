#include <memory>

#include <ll/api/memory/Hook.h>
#include <ll/api/service/Bedrock.h>
#include <ll/api/event/Emitter.h>
#include <ll/api/event/EmitterBase.h>
#include <ll/api/event/EventBus.h>

#include <mc/world/actor/Actor.h>

#include <mc/world/level/BlockPos.h>
#include <mc/world/level/block/Block.h>
#include <mc/world/level/dimension/Dimension.h>

#include <mc/scripting/modules/minecraft/events/ScriptBlockBreakingEventListener.h>
#include <mc/scripting/modules/minecraft/events/ScriptBlockGlobalEventListener.h>

#include "LOICollectionA/include/server/Events/world/BlockExplodedEvent.h"


namespace LOICollection::server::Events {
    const BlockPos& BlockExplodedEvent::getPosition() const {
        return mPosition;
    }

    const Block& BlockExplodedEvent::getBlock() const {
        return mBlock;
    }

    int BlockExplodedEvent::getDimensionId() const {
        return mDimensionId;
    }

    Actor* BlockExplodedEvent::getSource() const {
        return mSource;
    }

    LL_TYPE_INSTANCE_HOOK(
        BlockExplodedEventHook,
        HookPriority::Normal,
        ScriptModuleMinecraft::ScriptBlockGlobalEventListener,
        &ScriptBlockGlobalEventListener::$onBlockExploded,
        EventResult,
        Dimension& dimension,
        BlockPos const& blockPos,
        Block const& destroyedBlock,
        Actor* source
    ) {
        if (destroyedBlock.isAir())
            return origin(dimension, blockPos, destroyedBlock, source);

        BlockExplodedEvent event(blockPos, destroyedBlock, dimension.getDimensionId(), source);
        ll::event::EventBus::getInstance().publish(event);

        return origin(dimension, blockPos, destroyedBlock, source);
    }

    static std::unique_ptr<ll::event::EmitterBase> BlockExplodedEmitterFactory();
    class BlockExplodedEventEmitter : public ll::event::Emitter<BlockExplodedEmitterFactory, BlockExplodedEvent> {
        ll::memory::HookRegistrar<BlockExplodedEventHook> hook;
    };

    static std::unique_ptr<ll::event::EmitterBase> BlockExplodedEmitterFactory() {
        return std::make_unique<BlockExplodedEventEmitter>();
    }
}