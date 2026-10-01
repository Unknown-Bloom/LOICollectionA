#include <memory>

#include <ll/api/memory/Hook.h>
#include <ll/api/service/Bedrock.h>
#include <ll/api/event/Emitter.h>
#include <ll/api/event/EmitterBase.h>
#include <ll/api/event/EventBus.h>

#include <mc/world/level/Level.h>

#include <mc/world/actor/Mob.h>
#include <mc/world/actor/Actor.h>
#include <mc/world/actor/HurtParameters.h>
#include <mc/world/actor/ActorHurtResult.h>
#include <mc/world/actor/ActorDamageSource.h>
#include <mc/world/actor/player/Player.h>

#include <mc/legacy/ActorUniqueID.h>

#include "LOICollectionA/include/server/Events/player/PlayerHurtEvent.h"

namespace LOICollection::server::Events {
    Actor& PlayerHurtEvent::getSource() const {
        return this->mSource;
    }

    int PlayerHurtEvent::getDamage() const {
        return this->mDamage;
    }

    bool PlayerHurtEvent::isKnock() const {
        return this->mKnock;
    }

    bool PlayerHurtEvent::isIgnite() const {
        return this->mIgnite;
    }

    PlayerHurtReason PlayerHurtEvent::getReason() const {
        return this->mReason;
    }

    LL_TYPE_INSTANCE_HOOK(
        PlayerHurtEventHook,
        HookPriority::Normal,
        Mob,
        &Mob::$_hurt,
        ActorHurtResult,
        ActorDamageSource const& source,
        float damage,
        HurtParameters const& hurtParameters
    ) {
        if (!this->isPlayer() || !source.isEntitySource() || this->getOrCreateUniqueID().rawID == source.getEntityUniqueID().rawID)
            return origin(source, damage, hurtParameters);

        Actor* mSource = ll::service::getLevel()->fetchEntity(
            source.isChildEntitySource() ? source.getEntityUniqueID() : source.getDamagingEntityUniqueID(), false
        );
        if (!mSource)
            return origin(source, damage, hurtParameters);

        PlayerHurtEvent event(
            *reinterpret_cast<Player*>(this),
            *mSource,
            static_cast<int>(damage),
            hurtParameters.mKnockback,
            hurtParameters.mIgnition,
            source.mCause == SharedTypes::Legacy::ActorDamageCause::Projectile ? PlayerHurtReason::Projectile :
            source.mCause == SharedTypes::Legacy::ActorDamageCause::Piston ? PlayerHurtReason::Effect : PlayerHurtReason::Hurt
        );
        ll::event::EventBus::getInstance().publish(event);
        if (event.isCancelled())
            return { false, false };

        return origin(source, damage, hurtParameters);
    };

    static std::unique_ptr<ll::event::EmitterBase> PlayerHurtEmitterFactory();
    class PlayerHurtEventEmitter : public ll::event::Emitter<PlayerHurtEmitterFactory, PlayerHurtEvent> {
        ll::memory::HookRegistrar<PlayerHurtEventHook> hook;
    };

    static std::unique_ptr<ll::event::EmitterBase> PlayerHurtEmitterFactory() {
        return std::make_unique<PlayerHurtEventEmitter>();
    }
}
