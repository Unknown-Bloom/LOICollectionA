#include <gtest/gtest.h>

#include <string>

#include <ll/api/service/Bedrock.h>
#include <ll/api/event/EventBus.h>

#include <mc/world/level/Level.h>
#include <mc/world/level/BlockPos.h>
#include <mc/world/actor/Actor.h>
#include <mc/world/actor/player/Player.h>

#include "LOICollectionA/include/server/Events/player/PlayerHurtEvent.h"
#include "LOICollectionA/include/server/Events/player/PlayerContainerEvent.h"
#include "LOICollectionA/include/server/Events/modules/MuteEvent.h"
#include "LOICollectionA/include/server/Events/modules/BlacklistEvent.h"

using namespace LOICollection::server::Events;

template <class TEvent>
constexpr bool PlayerEventBusTestHasCancel() {
    return requires(TEvent& event) { event.cancel(); };
}

TEST(PlayerEventBusTest, BlacklistAddBeforeEventRoundTripAndCancel) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    ASSERT_TRUE(sp);

    static_assert(PlayerEventBusTestHasCancel<BlacklistAddBeforeEvent>());

    std::string cause = "test_event_cause";

    BlacklistAddBeforeEvent event(*sp, cause, 120);

    bool called = false;
    auto listener = ll::event::EventBus::getInstance().emplaceListener<BlacklistAddBeforeEvent>(
        [&](BlacklistAddBeforeEvent& published) -> void {
            called = true;

            EXPECT_EQ(&published.self(), sp);
            EXPECT_EQ(published.getCause(), cause);
            EXPECT_EQ(published.getTime(), 120);

            published.cancel();
        }
    );

    ll::event::EventBus::getInstance().publish(event);
    ll::event::EventBus::getInstance().removeListener(listener);

    EXPECT_TRUE(called);
    EXPECT_TRUE(event.isCancelled());
}

TEST(PlayerEventBusTest, BlacklistAddAfterEventRoundTripWithoutCancel) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    ASSERT_TRUE(sp);

    static_assert(!PlayerEventBusTestHasCancel<BlacklistAddAfterEvent>());

    std::string cause = "test_event_cause";

    BlacklistAddAfterEvent event(*sp, cause, 240);

    bool called = false;
    auto listener = ll::event::EventBus::getInstance().emplaceListener<BlacklistAddAfterEvent>(
        [&](BlacklistAddAfterEvent& published) -> void {
            called = true;

            EXPECT_EQ(&published.self(), sp);
            EXPECT_EQ(published.getCause(), cause);
            EXPECT_EQ(published.getTime(), 240);
        }
    );

    ll::event::EventBus::getInstance().publish(event);
    ll::event::EventBus::getInstance().removeListener(listener);

    EXPECT_TRUE(called);
}

TEST(PlayerEventBusTest, PlayerHurtEventRoundTrip) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    ASSERT_TRUE(sp);

    Actor& src = *sp;

    PlayerHurtEvent event(*sp, src, 7, true, true, PlayerHurtReason::Projectile);

    EXPECT_EQ(&event.self(), sp);
    EXPECT_EQ(&event.getSource(), &src);
    EXPECT_EQ(event.getDamage(), 7);
    EXPECT_TRUE(event.isKnock());
    EXPECT_TRUE(event.isIgnite());
    EXPECT_EQ(event.getReason(), PlayerHurtReason::Projectile);

    bool called = false;
    auto listener = ll::event::EventBus::getInstance().emplaceListener<PlayerHurtEvent>(
        [&](PlayerHurtEvent& published) -> void {
            called = true;

            EXPECT_EQ(&published.self(), sp);
            EXPECT_EQ(&published.getSource(), &src);
            EXPECT_EQ(published.getDamage(), 7);
            EXPECT_TRUE(published.isKnock());
            EXPECT_TRUE(published.isIgnite());
            EXPECT_EQ(published.getReason(), PlayerHurtReason::Projectile);
        },
        ll::event::EventPriority::Highest
    );
    ASSERT_TRUE(listener);

    ll::event::EventBus::getInstance().publish(event);
    ll::event::EventBus::getInstance().removeListener(listener);

    EXPECT_TRUE(called || event.isCancelled());
    EXPECT_EQ(event.getDamage(), 7);
    EXPECT_EQ(event.getReason(), PlayerHurtReason::Projectile);
}

TEST(PlayerEventBusTest, PlayerHurtEventDefaults) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    ASSERT_TRUE(sp);

    Actor& src = *sp;

    PlayerHurtEvent event(*sp, src);

    EXPECT_EQ(&event.self(), sp);
    EXPECT_EQ(&event.getSource(), &src);
    EXPECT_EQ(event.getDamage(), 0);
    EXPECT_FALSE(event.isKnock());
    EXPECT_FALSE(event.isIgnite());
    EXPECT_EQ(event.getReason(), PlayerHurtReason::Hurt);

    bool called = false;
    auto listener = ll::event::EventBus::getInstance().emplaceListener<PlayerHurtEvent>(
        [&](PlayerHurtEvent& published) -> void {
            called = true;

            EXPECT_EQ(&published.self(), sp);
            EXPECT_EQ(&published.getSource(), &src);
            EXPECT_EQ(published.getDamage(), 0);
            EXPECT_FALSE(published.isKnock());
            EXPECT_FALSE(published.isIgnite());
            EXPECT_EQ(published.getReason(), PlayerHurtReason::Hurt);
        },
        ll::event::EventPriority::Highest
    );
    ASSERT_TRUE(listener);

    ll::event::EventBus::getInstance().publish(event);
    ll::event::EventBus::getInstance().removeListener(listener);

    EXPECT_TRUE(called || event.isCancelled());
    EXPECT_EQ(event.getDamage(), 0);
    EXPECT_EQ(event.getReason(), PlayerHurtReason::Hurt);
}

TEST(PlayerEventBusTest, PlayerOpenContainerEventRoundTripAndCancel) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    ASSERT_TRUE(sp);

    BlockPos pos(1, 64, -3);

    PlayerOpenContainerEvent event(*sp, pos, 2);

    bool called = false;
    auto listener = ll::event::EventBus::getInstance().emplaceListener<PlayerOpenContainerEvent>(
        [&](PlayerOpenContainerEvent& published) -> void {
            called = true;

            EXPECT_EQ(&published.self(), sp);
            EXPECT_EQ(published.getPosition(), pos);
            EXPECT_EQ(&published.getPosition(), &pos);
            EXPECT_EQ(published.getDimensionId(), 2);

            published.cancel();
        }
    );

    ll::event::EventBus::getInstance().publish(event);
    ll::event::EventBus::getInstance().removeListener(listener);

    EXPECT_TRUE(called);
    EXPECT_TRUE(event.isCancelled());
}
