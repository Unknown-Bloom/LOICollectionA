#include <gtest/gtest.h>

#include <string>

#include <ll/api/event/EventBus.h>

#include <mc/network/Packet.h>
#include <mc/network/MinecraftPacketIds.h>
#include <mc/network/packet/ModalFormRequestPacket.h>

#include "LOICollectionA/include/server/Events/modules/MuteEvent.h"
#include "LOICollectionA/include/server/Events/modules/NoticeEvent.h"
#include "LOICollectionA/include/server/Events/modules/BlacklistEvent.h"
#include "LOICollectionA/include/server/Events/server/NetworkPacketEvent.h"

using namespace LOICollection::server::Events;

TEST(ReferencedEventsTest, NoticeCreateEventRoundTripAndCancel) {
    std::string target = "test_event_notice_target";
    std::string title = "test_event_notice_title";

    NoticeCreateEvent event(target, title, 7, true);

    bool called = false;
    auto listener = ll::event::EventBus::getInstance().emplaceListener<NoticeCreateEvent>(
        [&](NoticeCreateEvent& published) -> void {
            called = true;

            EXPECT_EQ(published.getTarget(), target);
            EXPECT_EQ(published.getTitle(), title);
            EXPECT_EQ(published.getPriority(), 7);
            EXPECT_TRUE(published.isPoiontout());

            published.cancel();
        }
    );

    ll::event::EventBus::getInstance().publish(event);
    ll::event::EventBus::getInstance().removeListener(listener);

    EXPECT_TRUE(called);
    EXPECT_TRUE(event.isCancelled());
}

TEST(ReferencedEventsTest, NoticeDeleteEventRoundTripWithoutCancel) {
    std::string target = "test_event_notice_target";

    NoticeDeleteEvent event(target);

    bool called = false;
    auto listener = ll::event::EventBus::getInstance().emplaceListener<NoticeDeleteEvent>(
        [&](NoticeDeleteEvent& published) -> void {
            called = true;

            EXPECT_EQ(published.getTarget(), target);
        }
    );

    ll::event::EventBus::getInstance().publish(event);
    ll::event::EventBus::getInstance().removeListener(listener);

    EXPECT_TRUE(called);
    EXPECT_FALSE(event.isCancelled());
}

TEST(ReferencedEventsTest, MuteRemoveEventRoundTripAndCancel) {
    std::string target = "test_event_mute_target";

    MuteRemoveEvent event(target);

    bool called = false;
    auto listener = ll::event::EventBus::getInstance().emplaceListener<MuteRemoveEvent>(
        [&](MuteRemoveEvent& published) -> void {
            called = true;

            EXPECT_EQ(published.getTarget(), target);

            published.cancel();
        }
    );

    ll::event::EventBus::getInstance().publish(event);
    ll::event::EventBus::getInstance().removeListener(listener);

    EXPECT_TRUE(called);
    EXPECT_TRUE(event.isCancelled());
}

TEST(ReferencedEventsTest, BlacklistRemoveEventRoundTripWithoutCancel) {
    std::string target = "test_event_blacklist_target";

    BlacklistRemoveEvent event(target);

    bool called = false;
    auto listener = ll::event::EventBus::getInstance().emplaceListener<BlacklistRemoveEvent>(
        [&](BlacklistRemoveEvent& published) -> void {
            called = true;

            EXPECT_EQ(published.getTarget(), target);
        }
    );

    ll::event::EventBus::getInstance().publish(event);
    ll::event::EventBus::getInstance().removeListener(listener);

    EXPECT_TRUE(called);
    EXPECT_FALSE(event.isCancelled());
}

TEST(ReferencedEventsTest, NetworkBroadcastPacketEventPacketIdentity) {
    ModalFormRequestPacket packet(42, "{\"type\":\"form\"}");
    NetworkBroadcastPacketEvent event(packet);

    bool called = false;
    auto listener = ll::event::EventBus::getInstance().emplaceListener<NetworkBroadcastPacketEvent>(
        [&](NetworkBroadcastPacketEvent& published) -> void {
            called = true;

            EXPECT_EQ(&published.getPacket(), static_cast<const Packet*>(&packet));
            EXPECT_EQ(published.getPacket().getId(), MinecraftPacketIds::ShowModalForm);
        }
    );

    ll::event::EventBus::getInstance().publish(event);
    ll::event::EventBus::getInstance().removeListener(listener);

    EXPECT_TRUE(called);
    EXPECT_FALSE(event.isCancelled());
}

TEST(ReferencedEventsTest, NetworkBroadcastPacketEventCancel) {
    ModalFormRequestPacket packet(7, "{}");
    NetworkBroadcastPacketEvent event(packet);

    bool called = false;
    auto listener = ll::event::EventBus::getInstance().emplaceListener<NetworkBroadcastPacketEvent>(
        [&](NetworkBroadcastPacketEvent& published) -> void {
            called = true;

            EXPECT_EQ(&published.getPacket(), static_cast<const Packet*>(&packet));

            published.cancel();
        }
    );

    ll::event::EventBus::getInstance().publish(event);
    ll::event::EventBus::getInstance().removeListener(listener);

    EXPECT_TRUE(called);
    EXPECT_TRUE(event.isCancelled());
}
