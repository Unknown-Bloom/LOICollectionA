#include <gtest/gtest.h>

#include <string>

#include <ll/api/service/Bedrock.h>
#include <ll/api/event/EventBus.h>

#include <mc/world/level/Level.h>
#include <mc/world/scores/Objective.h>
#include <mc/world/actor/player/Player.h>

#include "LOICollectionA/utils/mc-server/ScoreboardUtils.h"

#include "LOICollectionA/include/server/Events/player/PlayerScoreChangedEvent.h"

using namespace LOICollection::server::Events;

class PlayerScoreChangedHookTest : public testing::Test {
protected:
    struct ScoreEventCapture {
        int calls = 0;
        bool hasLast = false;
        int lastScore = 0;
        std::string lastObjective;
        ScoreChangedType lastType = ScoreChangedType::add;
    };

    std::string mObjective = "test_event_score";
    bool mCreated = false;

    void SetUp() override {
        if (!ScoreboardUtils::hasScoreboard(this->mObjective)) {
            ScoreboardUtils::create(this->mObjective);
            this->mCreated = true;
        }
    }

    void TearDown() override {
        if (this->mCreated)
            ScoreboardUtils::remove(this->mObjective);
    }
};

TEST_F(PlayerScoreChangedHookTest, PositiveAddPublishesAddDeltaAndReducePublishesReduceDelta) {
    auto sp = ll::service::getLevel()->getPlayer("test_player");
    ASSERT_TRUE(sp);

    ScoreEventCapture capture;

    ScoreboardUtils::setScore(*sp, this->mObjective, 0);
    ScoreboardUtils::addScore(*sp, this->mObjective, 10);
    ScoreboardUtils::addScore(*sp, this->mObjective, 10);

    auto listener = ll::event::EventBus::getInstance().emplaceListener<PlayerScoreChangedEvent>(
        [&capture](PlayerScoreChangedEvent& event) -> void {
            capture.calls += 1;
            capture.hasLast = true;
            capture.lastScore = event.getScore();
            capture.lastObjective = event.getObjective().mName;
            capture.lastType = event.getScoreChangedType();
        }
    );

    ScoreboardUtils::addScore(*sp, this->mObjective, 5);

    ScoreEventCapture addCapture = capture;

    EXPECT_EQ(ScoreboardUtils::getScore(*sp, this->mObjective), 25);

    capture.calls = 0;
    capture.hasLast = false;

    ScoreboardUtils::reduceScore(*sp, this->mObjective, 7);

    ll::event::EventBus::getInstance().removeListener(listener);

    EXPECT_EQ(ScoreboardUtils::getScore(*sp, this->mObjective), 18);

    ASSERT_TRUE(addCapture.hasLast);
    EXPECT_GE(addCapture.calls, 1);
    EXPECT_EQ(addCapture.lastObjective, this->mObjective);
    EXPECT_EQ(addCapture.lastType, ScoreChangedType::add);
    EXPECT_EQ(addCapture.lastScore, 5);

    ASSERT_TRUE(capture.hasLast);
    EXPECT_GE(capture.calls, 1);
    EXPECT_EQ(capture.lastObjective, this->mObjective);
    EXPECT_EQ(capture.lastType, ScoreChangedType::reduce);
    EXPECT_EQ(capture.lastScore, 7);
}
