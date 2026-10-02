#include <string>

#include <ll/api/service/Bedrock.h>

#include <mc/world/level/Level.h>

#include <mc/world/actor/player/Player.h>

#include <mc/world/scores/Objective.h>
#include <mc/world/scores/ScoreInfo.h>
#include <mc/world/scores/Scoreboard.h>
#include <mc/world/scores/ScoreboardId.h>
#include <mc/world/scores/ScoreboardOperationResult.h>
#include <mc/world/scores/PlayerScoreSetFunction.h>

#include "LOICollectionA/utils/mc-server/ScoreboardUtils.h"

namespace ScoreboardUtils {
    bool hasScoreboard(const std::string& name) {
        return ll::service::getLevel()->getScoreboard().getObjective(name) ? true : false;
    }

    int getScore(Player& player, const std::string& name) {
        auto level = ll::service::getLevel();

        ScoreboardId identity = level->getScoreboard().getScoreboardId(player);
        if (identity.mRawID == ScoreboardId::INVALID().mRawID)
            return 0;
        
        Objective* obj = level->getScoreboard().getObjective(name);
        return !obj ? 0 : obj->getPlayerScore(identity).mValue;
    }

    void create(const std::string& name) {
        auto level = ll::service::getLevel();

        if (hasScoreboard(name))
            return;

        level->getScoreboard().addObjective(
            name, name, *level->getScoreboard().getCriteria("dummy")
        );
    }

    void remove(const std::string& name) {
        auto level = ll::service::getLevel();

        if (!hasScoreboard(name))
            return;

        level->getScoreboard().removeObjective(level->getScoreboard().getObjective(name));
    }

    void modifyScore(ScoreboardId& identity, const std::string& name, int score, ScoreType action) {
        auto level = ll::service::getLevel();

        Objective* obj = level->getScoreboard().getObjective(name);
        if (!obj) {
            obj = static_cast<Objective*>(level->getScoreboard().addObjective(
                name, name, *level->getScoreboard().getCriteria("dummy")
            ));
        }

        ScoreboardOperationResult result;
        level->getScoreboard().modifyPlayerScore(result, identity, *obj, score, 
            action == ScoreType::set ? PlayerScoreSetFunction::Set : 
            action == ScoreType::add ? PlayerScoreSetFunction::Add : 
            PlayerScoreSetFunction::Subtract
        );
    }

    void addScore(Player& player, const std::string& name, int score) {
        auto level = ll::service::getLevel();

        ScoreboardId identity = level->getScoreboard().getScoreboardId(player);
        if (identity.mRawID == ScoreboardId::INVALID().mRawID) 
            identity = level->getScoreboard().createScoreboardId(player);
            
        modifyScore(identity, name, score, ScoreType::add);
    }

    void reduceScore(Player& player, const std::string& name, int score) {
        auto level = ll::service::getLevel();

        ScoreboardId identity = level->getScoreboard().getScoreboardId(player);
        if (identity.mRawID == ScoreboardId::INVALID().mRawID)
            identity = level->getScoreboard().createScoreboardId(player);

        modifyScore(identity, name, score, ScoreType::reduce);
    }

    void setScore(Player& player, const std::string &name, int score) {
        auto level = ll::service::getLevel();

        ScoreboardId identity = level->getScoreboard().getScoreboardId(player);
        if (identity.mRawID == ScoreboardId::INVALID().mRawID)
            identity = level->getScoreboard().createScoreboardId(player);

        modifyScore(identity, name, score, ScoreType::set);
    }
}