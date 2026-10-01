#include <string>
#include <vector>
#include <algorithm>

#include <magic_enum/magic_enum.hpp>

#include <ll/api/Versions.h>
#include <ll/api/memory/Memory.h>
#include <ll/api/service/Bedrock.h>

#include <mc/deps/core/math/Vec2.h>
#include <mc/deps/core/math/Vec3.h>

#include <mc/profile/ProfilerLite.h>

#include <mc/world/level/Level.h>
#include <mc/world/level/BlockPos.h>
#include <mc/world/actor/BuiltInActorComponents.h>
#include <mc/world/actor/player/Player.h>
#include <mc/world/attribute/AttributeInstanceConstRef.h>
#include <mc/world/attribute/AttributeInstance.h>
#include <mc/entity/components/ActorRotationComponent.h>
#include <mc/network/ServerNetworkHandler.h>

#include "LOICollectionA/include/server/Plugins/PvpPlugin.h"
#include "LOICollectionA/include/server/Plugins/ChatPlugin.h"
#include "LOICollectionA/include/server/Plugins/MutePlugin.h"
#include "LOICollectionA/include/server/Plugins/LanguagePlugin.h"
#include "LOICollectionA/include/server/Plugins/StatisticsPlugin.h"

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/mc-server/ScoreboardUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/include/CallbackUtils.h"

namespace LOICollection::LOICollectionAPI {
    void CallbackUtils::compile() {
        auto& cbu = CallbackUtils::getInstance();

        cbu.registerVariable("version_mc", []() -> std::string {
            return ll::getGameVersion().to_string();
        });
        cbu.registerVariable("version_ll", []() -> std::string {
            return ll::getLoaderVersion().to_string();
        });
        cbu.registerVariable("version_protocol", []() -> int {
            return ll::getNetworkProtocolVersion(); 
        });
        cbu.registerVariable("player", [](Player& player) -> std::string {
            return std::string{player.mName};
        });
        cbu.registerVariable("player_title", [](Player& player) -> ll::Expected<std::string> {
            return server::Plugins::ChatPlugin::getShared()->getTitle(player);
        });
        cbu.registerVariable("player_title_time", [](Player& player) -> ll::Expected<std::string> {
            return server::Plugins::ChatPlugin::getShared()->getTitle(player)
                .and_then([&player](const std::string& title) -> ll::Expected<std::string> {
                    return server::Plugins::ChatPlugin::getShared()->getTitleTime(player, title);
                })
                .transform([](const std::string& time) -> std::string {
                    return SystemUtils::toFormatTime(time);
                });
        });
        cbu.registerVariable("player_pvp", [](Player& player) -> ll::Expected<bool> {
            return server::Plugins::PvpPlugin::getShared()->isEnable(player);
        });
        cbu.registerVariable("player_mute", [](Player& player) -> ll::Expected<bool> {
            return server::Plugins::MutePlugin::getShared()->isMute(player);
        });
        cbu.registerVariable("player_language", [](Player& player) -> ll::Expected<std::string> { 
            return server::Plugins::LanguagePlugin::getShared()->getLanguage(player);
        });
        cbu.registerVariable("player_language_name", [](Player& player) -> ll::Expected<std::string> {
            return server::Plugins::LanguagePlugin::getShared()->getLanguage(player)
                .transform([](const std::string& language) -> std::string {
                    return I18nUtils::getInstance()->get(language, "name");
                });
        });
        cbu.registerVariable("player_statistcs_onlinetime", [](Player& player) -> ll::Expected<std::string> {
            return server::Plugins::StatisticsPlugin::getShared()->getStatistic(player, server::Plugins::StatisticType::onlinetime)
                .transform([](int time) -> std::string {
                    return SystemUtils::toFormatSecond(std::to_string(time), "None");
                });
        });
        cbu.registerVariable("player_statistcs_kills", [](Player& player) -> ll::Expected<int> {
            return server::Plugins::StatisticsPlugin::getShared()->getStatistic(player, server::Plugins::StatisticType::kills);
        });
        cbu.registerVariable("player_statistcs_deaths", [](Player& player) -> ll::Expected<int> {
            return server::Plugins::StatisticsPlugin::getShared()->getStatistic(player, server::Plugins::StatisticType::deaths);
        });
        cbu.registerVariable("player_statistcs_place", [](Player& player) -> ll::Expected<int> {
            return server::Plugins::StatisticsPlugin::getShared()->getStatistic(player, server::Plugins::StatisticType::place);
        });
        cbu.registerVariable("player_statistcs_destroy", [](Player& player) -> ll::Expected<int> {
            return server::Plugins::StatisticsPlugin::getShared()->getStatistic(player, server::Plugins::StatisticType::destroy);
        });
        cbu.registerVariable("player_statistcs_respawn", [](Player& player) -> ll::Expected<int> {
            return server::Plugins::StatisticsPlugin::getShared()->getStatistic(player, server::Plugins::StatisticType::respawn);
        });
        cbu.registerVariable("player_statistcs_join", [](Player& player) -> ll::Expected<int> {
            return server::Plugins::StatisticsPlugin::getShared()->getStatistic(player, server::Plugins::StatisticType::join);
        });
        cbu.registerVariable("player_gamemode", [](Player& player) -> std::string {
            return std::string(magic_enum::enum_name(player.getPlayerGameType()));
        });
        cbu.registerVariable("player_pos", [](Player& player) -> std::string {
            return player.getPosition().toString();
        });
        cbu.registerVariable("player_pos_x", [](Player& player) -> int {
            return static_cast<int>(player.getPosition().x);
        });
        cbu.registerVariable("player_pos_y", [](Player& player) -> int {
            return static_cast<int>(player.getPosition().y);
        });
        cbu.registerVariable("player_pos_z", [](Player& player) -> int {
            return static_cast<int>(player.getPosition().z);
        });
        cbu.registerVariable("player_pos_respawn", [](Player& player) -> std::string {
            return player.getExpectedSpawnPosition().toString();
        });
        cbu.registerVariable("player_pos_respawn_x", [](Player& player) -> frontend::TypedValue {
            return static_cast<int>(player.getExpectedSpawnPosition().x);
        });
        cbu.registerVariable("player_pos_respawn_y", [](Player& player) -> frontend::TypedValue {
            return static_cast<int>(player.getExpectedSpawnPosition().y);
        });
        cbu.registerVariable("player_pos_respawn_z", [](Player& player) -> frontend::TypedValue {
            return static_cast<int>(player.getExpectedSpawnPosition().z);
        });
        cbu.registerVariable("player_pos_block", [](Player& player) -> std::string {
            return player.getFeetBlockPos().toString();
        });
        cbu.registerVariable("player_pos_lastdeath", [](Player& player) -> std::string {
            return player.getLastDeathPos() ? player.getLastDeathPos()->toString() : "None";
        });
        cbu.registerVariable("player_realname", [](Player& player) -> std::string {
            return player.getRealName();
        });
        cbu.registerVariable("player_xuid", [](Player& player) -> std::string {
            return player.getXuid();
        });
        cbu.registerVariable("player_uuid", [](Player& player) -> std::string {
            return player.getUuid().asString();
        });
        cbu.registerVariable("player_is_op", [](Player& player) -> bool {
            return player.isOperator();
        });
        cbu.registerVariable("player_can_fly", [](Player& player) -> bool {
            return player.canFly();
        });
        cbu.registerVariable("player_health", [](Player& player) -> int {
            return player.getHealth();
        });
        cbu.registerVariable("player_max_health", [](Player& player) -> int {
            return static_cast<int>(player.getMaxHealth());
        });
        cbu.registerVariable("player_hunger", [](Player& player) -> frontend::TypedValue {
            if (auto attribute = player.getAttribute(Player::HUNGER()).mPtr; attribute)
                return static_cast<int>(attribute->mCurrentValue);

            return "None";
        });
        cbu.registerVariable("player_max_hunger", [](Player& player) -> frontend::TypedValue {
            if (auto attribute = player.getAttribute(Player::HUNGER()).mPtr; attribute)
                return static_cast<int>(attribute->mCurrentMaxValue);

            return "None";
        });
        cbu.registerVariable("player_saturation", [](Player& player) -> frontend::TypedValue {
            if (auto attribute = player.getAttribute(Player::SATURATION()).mPtr; attribute)
                return static_cast<int>(attribute->mCurrentValue);

            return "None";
        });
        cbu.registerVariable("player_max_saturation", [](Player& player) -> frontend::TypedValue{
            if (auto attribute = player.getAttribute(Player::SATURATION()).mPtr; attribute)
                return static_cast<int>(attribute->mCurrentMaxValue);

            return "None";
        });
        cbu.registerVariable("player_speed", [](Player& player) -> float {
            return player.getSpeed();
        });
        cbu.registerVariable("player_direction", [](Player& player) -> std::string {
            return player.mBuiltInComponents->mActorRotationComponent->mRot->toString();
        });
        cbu.registerVariable("player_dimension", [](Player& player) -> int {
            return player.getDimensionId();
        });
        cbu.registerVariable("player_os", [](Player& player) -> std::string {
            return magic_enum::enum_name(player.mBuildPlatform).data();
        });
        cbu.registerVariable("player_ip", [](Player& player) -> std::string {
            return player.getIPAndPort();
        });
        cbu.registerVariable("player_exp_xp", [](Player& player) -> frontend::TypedValue {
            if (auto attribute = player.getAttribute(Player::EXPERIENCE()).mPtr; attribute)
                return static_cast<int>(attribute->mCurrentValue);

            return "None";
        });
        cbu.registerVariable("player_exp_level", [](Player& player) -> frontend::TypedValue {
            if (auto attribute = player.getAttribute(Player::LEVEL()).mPtr; attribute)
                return static_cast<int>(attribute->mCurrentValue);

            return "None";
        });
        cbu.registerVariable("player_exp_level_next", [](Player& player) -> int {
            return player.getXpNeededForNextLevel();
        });
        cbu.registerVariable("player_handitem", [](Player& player) -> std::string {
            return player.getCarriedItem().getName();
        });
        cbu.registerVariable("player_offhand", [](Player& player) -> std::string {
            return player.getOffhandSlot().getName();
        });
        cbu.registerVariable("player_ms", [](Player& player) -> int {
            return static_cast<int>(std::min(player.getNetworkStatus()->mAveragePing->count(), static_cast<long long>(300000)));
        });
        cbu.registerVariable("player_ms_avg", [](Player& player) -> int {
            return static_cast<int>(std::min(player.getNetworkStatus()->mCurrentPing->count(), static_cast<long long>(300000)));
        });
        cbu.registerVariable("player_packet", [](Player& player) -> float {
            return player.getNetworkStatus()->mAveragePacketLoss;
        });
        cbu.registerVariable("player_packet_avg", [](Player& player) -> float {
            return player.getNetworkStatus()->mCurrentPacketLoss;
        });
        cbu.registerVariable("server_tps", []() -> float {
            auto mMspt = static_cast<double>(ProfilerLite::gProfilerLiteInstance().mDebugServerTickTime->count()) / 1e6;
            return static_cast<float>(mMspt <= 50.0 ? 20.0 : static_cast<double>(1000.0 / mMspt));
        });
        cbu.registerVariable("server_mspt", []() -> float { 
            return static_cast<float>(static_cast<double>(ProfilerLite::gProfilerLiteInstance().mDebugServerTickTime->count()) / 1e6);
        });
        cbu.registerVariable("server_time", []() -> std::string {
            return SystemUtils::getNowTime();
        });
        cbu.registerVariable("server_player_max", []() -> int {
            return ll::service::getServerNetworkHandler()->mMaxNumPlayers;
        });
        cbu.registerVariable("server_player_online", []() -> int {
            return ll::service::getLevel()->getActivePlayerCount();
        });
        cbu.registerVariable("server_entity", []() -> int {
            return static_cast<int>(ll::service::getLevel()->getRuntimeActorList().size());
        });
        cbu.registerVariable("score", [](Player& player, const frontend::CallbackTypeValues& args) -> int {
            std::string name = std::get<std::string>(args[0]);

            return ScoreboardUtils::getScore(player, name);
        }, { frontend::ParamType::STRING });
        cbu.registerVariable("tr", [](Player& player, const frontend::CallbackTypeValues& args) -> ll::Expected<std::string> {
            std::string name = std::get<std::string>(args[0]);

            return server::Plugins::LanguagePlugin::getShared()->getLanguage(player)
                .transform([name](const std::string& language) -> std::string {
                    return I18nUtils::getInstance()->get(language, name);
                });
        }, { frontend::ParamType::STRING });
        cbu.registerVariable("tr", [](const frontend::CallbackTypeValues& args) -> std::string {
            std::string langcode = std::get<std::string>(args[0]);
            std::string name = std::get<std::string>(args[1]);

            return I18nUtils::getInstance()->get(langcode, name);
        }, { frontend::ParamType::STRING, frontend::ParamType::STRING });
        cbu.registerVariable("entity", [](const frontend::CallbackTypeValues& args) -> int {
            std::string name = std::get<std::string>(args[0]);

            std::vector<Actor*> mRuntimeActorList = ll::service::getLevel()->getRuntimeActorList();
            int count = static_cast<int>(std::count_if(mRuntimeActorList.begin(), mRuntimeActorList.end(), [name](Actor* actor) -> bool {
                return actor->getTypeName() == name;
            }));
            
            return count;
        }, { frontend::ParamType::STRING });
    }
}
