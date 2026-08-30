#include <string>
#include <vector>

#include <ll/api/Expected.h>

#include "LOICollectionA/frontend/AST.h"
#include "LOICollectionA/frontend/Callback.h"

#include "LOICollectionA/include/server/Plugins/form/ScoreData.h"

using namespace LOICollection::frontend;

namespace LOICollection::server::Plugins {
    namespace {
        std::string scoreReadString(const ObjectRef& obj, const std::string& field, const std::string& def = "") {
            auto it = obj->find(field);
            if (it == nullptr)
                return def;
            return std::holds_alternative<std::string>(*it) ? std::get<std::string>(*it) : def;
        }

        int scoreReadInt(const ObjectRef& obj, const std::string& field, int def = 0) {
            auto it = obj->find(field);
            if (it == nullptr)
                return def;
            if (std::holds_alternative<int>(*it))
                return std::get<int>(*it);
            if (std::holds_alternative<float>(*it))
                return static_cast<int>(std::get<float>(*it));
            return def;
        }

        ObjectRef makeScoreObject(const ScoreRequirement& score) {
            auto obj = std::make_shared<Object>();
            obj->className = "ScoreRequirement";
            obj->classIndex = -1;
            obj->assign("objective", score.objective);
            obj->assign("value", score.value);
            return obj;
        }
    }

    std::vector<ScoreRequirement> readScores(const frontend::ObjectRef& obj, const std::string& field) {
        std::vector<ScoreRequirement> result;

        auto it = obj->find(field);
        if (it == nullptr || !std::holds_alternative<ArrayRef>(*it))
            return result;

        for (const auto& element : std::get<ArrayRef>(*it)->elements) {
            if (!std::holds_alternative<ObjectRef>(element))
                continue;

            auto score = std::get<ObjectRef>(element);
            if (score->className != "ScoreRequirement")
                continue;

            result.push_back(ScoreRequirement{
                scoreReadString(score, "objective"),
                scoreReadInt(score, "value")
            });
        }

        return result;
    }

    frontend::ArrayRef makeScoreArray(const std::vector<ScoreRequirement>& scores) {
        auto array = std::make_shared<ArrayValue>();
        for (const auto& score : scores)
            array->elements.emplace_back(makeScoreObject(score));
        
        return array;
    }

    void registerScoreDataClasses(const std::string&) {
        ClassCall& classes = ClassCall::getInstance();
        classes.registerClass("ScoreRequirement", { "objective", "value" });
        classes.registerField("ScoreRequirement", "objective", std::string(""));
        classes.registerField("ScoreRequirement", "value", 0);
    }
}

REGISTER_CALLBACK(ScoreData, LOICollection::server::Plugins::registerScoreDataClasses)
