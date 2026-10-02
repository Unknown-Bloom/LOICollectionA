#include <gtest/gtest.h>

#include <string>

#include <ll/api/event/EventBus.h>

#include "LOICollectionA/include/server/Events/modules/MarketItemSoldEvent.h"
#include "LOICollectionA/include/server/Events/modules/RedEnvelopeCompletedEvent.h"
#include "LOICollectionA/include/server/Events/modules/WalletTransferEvent.h"

using namespace LOICollection::server::Events;

TEST(EventBusValueEventsTest, MarketItemSoldEventFieldRoundTrip) {
    MarketItemSoldEvent event("test_event_item", 1200, 60, "test_event_buyer", "test_event_seller", 1'700'000'000LL);

    bool called = false;
    std::string itemName;
    int price = 0;
    int tax = 0;
    std::string buyerUuid;
    std::string sellerUuid;
    long long time = 0;

    auto listener = ll::event::EventBus::getInstance().emplaceListener<MarketItemSoldEvent>(
        [&](MarketItemSoldEvent& published) -> void {
            called = true;

            itemName = published.getItemName();
            price = published.getPrice();
            tax = published.getTax();
            buyerUuid = published.getBuyerUuid();
            sellerUuid = published.getSellerUuid();
            time = published.getTime();

            EXPECT_EQ(&published, &event);
        }
    );

    ll::event::EventBus::getInstance().publish(event);
    ll::event::EventBus::getInstance().removeListener(listener);

    EXPECT_TRUE(called);
    EXPECT_EQ(itemName, "test_event_item");
    EXPECT_EQ(price, 1200);
    EXPECT_EQ(tax, 60);
    EXPECT_EQ(buyerUuid, "test_event_buyer");
    EXPECT_EQ(sellerUuid, "test_event_seller");
    EXPECT_EQ(time, 1'700'000'000LL);
}

TEST(EventBusValueEventsTest, WalletTransferEventFieldRoundTrip) {
    WalletTransferEvent event(
        "test_event_from_uuid",
        "test_event_from_name",
        "test_event_to_uuid",
        "test_event_to_name",
        2500LL,
        125LL,
        "test_event_type",
        1'700'000'001LL
    );

    bool called = false;
    std::string fromUuid;
    std::string fromName;
    std::string toUuid;
    std::string toName;
    long long amount = 0;
    long long fee = 0;
    std::string type;
    long long time = 0;

    auto listener = ll::event::EventBus::getInstance().emplaceListener<WalletTransferEvent>(
        [&](WalletTransferEvent& published) -> void {
            called = true;

            fromUuid = published.getFromUuid();
            fromName = published.getFromName();
            toUuid = published.getToUuid();
            toName = published.getToName();
            amount = published.getAmount();
            fee = published.getFee();
            type = published.getType();
            time = published.getTime();

            EXPECT_EQ(&published, &event);
        }
    );

    ll::event::EventBus::getInstance().publish(event);
    ll::event::EventBus::getInstance().removeListener(listener);

    EXPECT_TRUE(called);
    EXPECT_EQ(fromUuid, "test_event_from_uuid");
    EXPECT_EQ(fromName, "test_event_from_name");
    EXPECT_EQ(toUuid, "test_event_to_uuid");
    EXPECT_EQ(toName, "test_event_to_name");
    EXPECT_EQ(amount, 2500LL);
    EXPECT_EQ(fee, 125LL);
    EXPECT_EQ(type, "test_event_type");
    EXPECT_EQ(time, 1'700'000'001LL);
}

TEST(EventBusValueEventsTest, RedEnvelopeCompletedEventFieldRoundTrip) {
    RedEnvelopeCompletedEvent event(
        "test_event_envelope",
        "test_event_sender_uuid",
        "test_event_king_uuid",
        "test_event_king_name",
        30,
        100,
        1'700'000'002LL
    );

    bool called = false;
    std::string envelopeId;
    std::string senderUuid;
    std::string kingUuid;
    std::string kingName;
    int kingAmount = 0;
    int total = 0;
    long long time = 0;

    auto listener = ll::event::EventBus::getInstance().emplaceListener<RedEnvelopeCompletedEvent>(
        [&](RedEnvelopeCompletedEvent& published) -> void {
            called = true;

            envelopeId = published.getEnvelopeId();
            senderUuid = published.getSenderUuid();
            kingUuid = published.getKingUuid();
            kingName = published.getKingName();
            kingAmount = published.getKingAmount();
            total = published.getTotal();
            time = published.getTime();

            EXPECT_EQ(&published, &event);
        }
    );

    ll::event::EventBus::getInstance().publish(event);
    ll::event::EventBus::getInstance().removeListener(listener);

    EXPECT_TRUE(called);
    EXPECT_EQ(envelopeId, "test_event_envelope");
    EXPECT_EQ(senderUuid, "test_event_sender_uuid");
    EXPECT_EQ(kingUuid, "test_event_king_uuid");
    EXPECT_EQ(kingName, "test_event_king_name");
    EXPECT_EQ(kingAmount, 30);
    EXPECT_EQ(total, 100);
    EXPECT_EQ(time, 1'700'000'002LL);
}
