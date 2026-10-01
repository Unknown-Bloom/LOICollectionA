#include <atomic>
#include <memory>
#include <ranges>
#include <string>
#include <algorithm>
#include <unordered_map>

#include <ll/api/Expected.h>
#include <ll/api/io/Logger.h>
#include <ll/api/io/LoggerRegistry.h>

#include <ll/api/coro/CoroTask.h>
#include <ll/api/coro/InterruptableSleep.h>
#include <ll/api/thread/ServerThreadExecutor.h>

#include <ll/api/chrono/GameChrono.h>
#include <ll/api/service/Bedrock.h>
#include <ll/api/service/ServerInfo.h>

#include <ll/api/event/EventBus.h>
#include <ll/api/event/ListenerBase.h>
#include <ll/api/event/player/PlayerConnectEvent.h>
#include <ll/api/event/player/PlayerDisconnectEvent.h>
#include <ll/api/event/command/ExecuteCommandEvent.h>

#include <mc/world/scores/Objective.h>
#include <mc/world/scores/IdentityDefinition.h>

#include <mc/world/actor/Actor.h>
#include <mc/world/level/Level.h>
#include <mc/world/actor/DataItem.h>
#include <mc/world/actor/ActorDataIDs.h>
#include <mc/world/actor/SynchedActorDataEntityWrapper.h>
#include <mc/world/actor/player/Player.h>

#include <mc/network/MinecraftPackets.h>
#include <mc/network/NetworkIdentifier.h>
#include <mc/network/MinecraftPacketIds.h>
#include <mc/network/packet/TextPacket.h>
#include <mc/network/packet/SetScorePacket.h>
#include <mc/network/packet/SetActorDataPacket.h>
#include <mc/network/packet/ChangeFakePlayerScore.h>
#include <mc/network/packet/RemoveObjectivePacket.h>
#include <mc/network/packet/AvailableCommandsPacket.h>
#include <mc/network/packet/SetDisplayObjectivePacket.h>

#include <mc/server/ServerPlayer.h>
#include <mc/server/commands/CommandOrigin.h>
#include <mc/server/commands/CommandContext.h>

#include <mc/common/SubClientId.h>

#include "LOICollectionA/include/CallbackUtils.h"
#include "LOICollectionA/include/server/Plugins/LanguagePlugin.h"

#include "LOICollectionA/include/server/Events/server/NetworkPacketEvent.h"
#include "LOICollectionA/include/server/Events/player/PlayerScoreChangedEvent.h"

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/mc-server/ScoreboardUtils.h"

#include "LOICollectionA/base/Wrapper.h"
#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/MonitorPlugin.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    struct MonitorPlugin::Impl {
        std::vector<std::string> mInterceptTextObjectPacket;

        std::atomic<bool> mRegistered{ false };

        Config::C_Monitor options;

        std::shared_ptr<ll::io::Logger> logger;

        std::unordered_map<std::string, ll::event::ListenerPtr> mListeners;

        ll::coro::InterruptableSleep NameTaskSleep;
        ll::coro::InterruptableSleep BelowNameTaskSleep;
        ll::coro::InterruptableSleep DynamicMotdTaskSleep;
        ll::coro::InterruptableSleep SiebarTaskSleep;

        std::atomic<bool> NameTaskRunning{ true };
        std::atomic<bool> BelowNameTaskRunning{ true };
        std::atomic<bool> DynamicMotdTaskRunning{ true };
        std::atomic<bool> SiebarTaskRunning{ true };
    };

    MonitorPlugin::MonitorPlugin() : mImpl(std::make_unique<Impl>()) {};
    MonitorPlugin::~MonitorPlugin() = default;

    std::shared_ptr<MonitorPlugin> MonitorPlugin::getShared() {
        static auto instance = std::shared_ptr<MonitorPlugin>(new MonitorPlugin());
        return instance;
    }

    std::error_code MonitorPlugin::makeErrorCode(MonitorPluginErrorCode e) {
        static MonitorPluginErrorCategory cat;
        return std::error_code{ static_cast<int>(e), cat };
    }

    std::shared_ptr<ll::io::Logger> MonitorPlugin::getLogger() {
        return this->mImpl->logger;
    }

    void MonitorPlugin::listenEvent() {
        this->mImpl->NameTaskRunning.store(true, std::memory_order_release);
        this->mImpl->BelowNameTaskRunning.store(true, std::memory_order_release);
        this->mImpl->DynamicMotdTaskRunning.store(true, std::memory_order_release);
        this->mImpl->SiebarTaskRunning.store(true, std::memory_order_release);

        if (this->mImpl->options.BelowName.ModuleEnabled) {
            std::shared_ptr<std::string> mName = std::make_shared<std::string>();

            ll::coro::keepThis([this, option = this->mImpl->options.BelowName, mName]() -> ll::coro::CoroTask<> {
                size_t index = 0;
                size_t maxIndex = option.Pages.size() - 1;

                while (this->mImpl->NameTaskRunning.load(std::memory_order_acquire)) {
                    co_await this->mImpl->NameTaskSleep.sleepFor(ll::chrono::ticks(option.RefreshDisplayInterval));

                    if (!this->mImpl->NameTaskRunning.load(std::memory_order_acquire))
                        break;

                    std::string result;
                    for (const std::string& page : option.Pages[index]) 
                        result.append((result.empty() ? "" : " + '\n' + ") + page);

                    *mName = result;
                    index = index < maxIndex ? index + 1 : 0;
                }
            }).launch(ll::thread::ServerThreadExecutor::getDefault());

            ll::coro::keepThis([this, option = this->mImpl->options.BelowName, mName]() -> ll::coro::CoroTask<> {
                while (this->mImpl->BelowNameTaskRunning.load(std::memory_order_acquire)) {
                    co_await this->mImpl->BelowNameTaskSleep.sleepFor(ll::chrono::ticks(option.RefreshInterval));

                    if (!this->mImpl->BelowNameTaskRunning.load(std::memory_order_acquire))
                        break;

                    ll::service::getLevel()->forEachPlayer([mName](Player& mTarget) -> bool {
                        if (mTarget.isSimulatedPlayer())
                            return true;

                        std::string mNameTag = LOICollectionAPI::CallbackUtils::getInstance().translate(*mName, mTarget);
                        
                        SetActorDataPacket packet(mTarget.getRuntimeID(), mTarget.mEntityData, 
                            nullptr, mTarget.mLevel->getCurrentTick().tickID, false
                        );
                        packet.mPackedItems->clear();
                        packet.mPackedItems->emplace_back(DataItem::create(ActorDataIDs::FilteredName, mNameTag));
                        packet.sendToClients();

                        return true;
                    });
                }
            }).launch(ll::thread::ServerThreadExecutor::getDefault());
        }

        if (this->mImpl->options.DynamicMotd.ModuleEnabled) {
            ll::coro::keepThis([this, option = this->mImpl->options.DynamicMotd]() -> ll::coro::CoroTask<> {
                size_t index = 0;
                size_t maxIndex = option.Pages.size() - 1;

                while (this->mImpl->DynamicMotdTaskRunning.load(std::memory_order_acquire)) {
                    co_await this->mImpl->DynamicMotdTaskSleep.sleepFor(ll::chrono::ticks(option.RefreshInterval));

                    if (!this->mImpl->DynamicMotdTaskRunning.load(std::memory_order_acquire))
                        break;

                    ll::setServerMotd(LOICollectionAPI::CallbackUtils::getInstance().translate(option.Pages[index]));

                    index = index < maxIndex ? index + 1 : 0;
                }
            }).launch(ll::thread::ServerThreadExecutor::getDefault());
        }

        if (this->mImpl->options.Sidebar.ModuleEnabled) {
            ll::coro::keepThis([this, option = this->mImpl->options.Sidebar]() -> ll::coro::CoroTask<> {
                size_t mTitleIndex = 0;
                size_t mPageIndex = 0;
                size_t mTitleMaxIndex = option.Titles.size() - 1;
                size_t mPageMaxIndex = option.Pages.size() - 1;

                while (this->mImpl->SiebarTaskRunning.load(std::memory_order_acquire)) {
                    co_await this->mImpl->SiebarTaskSleep.sleepFor(ll::chrono::ticks(option.RefreshInterval));

                    if (!this->mImpl->SiebarTaskRunning.load(std::memory_order_acquire))
                        break;

                    ll::service::getLevel()->forEachPlayer([this, option, mTitleIndex, mPageIndex](Player& mTarget) -> bool {
                        if (mTarget.isSimulatedPlayer())
                            return true;

                        std::vector<std::pair<std::string, int>> data;

                        for (const auto& [index, page] : std::views::enumerate(option.Pages[mPageIndex]))
                            data.emplace_back(LOICollectionAPI::CallbackUtils::getInstance().translate(page, mTarget), static_cast<int>(index));

                        std::string mTitle = LOICollectionAPI::CallbackUtils::getInstance().translate(option.Titles[mTitleIndex], mTarget);

                        this->removeSidebar(mTarget, "LOICollectionA").or_else(modules::defaultErrorHandler<MonitorPlugin>);
                        this->addSidebar(mTarget, "LOICollectionA", mTitle, SidebarType::Descending).or_else(modules::defaultErrorHandler<MonitorPlugin>);
                        this->setSidebar(mTarget, "LOICollectionA", data).or_else(modules::defaultErrorHandler<MonitorPlugin>);

                        return true;
                    });

                    mTitleIndex = mTitleIndex < mTitleMaxIndex ? mTitleIndex + 1 : 0;
                    mPageIndex = mPageIndex < mPageMaxIndex ? mPageIndex + 1 : 0;
                }
            }).launch(ll::thread::ServerThreadExecutor::getDefault());
        }

        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        this->mImpl->mListeners.emplace("PlayerConnect", eventBus.emplaceListener<ll::event::PlayerConnectEvent>([this, option = this->mImpl->options.ServerToast](ll::event::PlayerConnectEvent& event) mutable -> void {
            if (event.self().isSimulatedPlayer() || !option.ModuleEnabled)
                return;

            if (option.Messager.join) {
                ll::service::getLevel()->forEachPlayer([&player = event.self()](Player& target) -> bool {
                    LanguagePlugin::getShared()->getLanguage(target)
                        .transform([&player, &target](const std::string& language) -> void {
                            TextPacket::createRawMessage(
                                LOICollectionAPI::CallbackUtils::getInstance().translate(tr(language, "monitor.servertoast.join"), player)
                            ).sendTo(target);
                        })
                        .or_else(modules::defaultErrorHandler<MonitorPlugin>);

                    return true;
                });
            }

            this->mImpl->mInterceptTextObjectPacket.push_back(event.self().getUuid().asString());
        }));

        this->mImpl->mListeners.emplace("PlayerDisconnect", eventBus.emplaceListener<ll::event::PlayerDisconnectEvent>([this, option = this->mImpl->options.ServerToast](ll::event::PlayerDisconnectEvent& event) mutable -> void {
            if (event.self().isSimulatedPlayer() || !option.ModuleEnabled)
                return;

            if (option.Messager.leave) {
                ll::service::getLevel()->forEachPlayer([&player = event.self()](Player& target) -> bool {
                    LanguagePlugin::getShared()->getLanguage(target)
                        .transform([&player, &target](const std::string& language) -> void {
                            TextPacket::createRawMessage(
                                LOICollectionAPI::CallbackUtils::getInstance().translate(tr(language, "monitor.servertoast.leave"), player)
                            ).sendTo(target);
                        })
                        .or_else(modules::defaultErrorHandler<MonitorPlugin>);

                    return true;
                });
            }

            this->mImpl->mInterceptTextObjectPacket.erase(std::remove(this->mImpl->mInterceptTextObjectPacket.begin(), this->mImpl->mInterceptTextObjectPacket.end(), event.self().getUuid().asString()), this->mImpl->mInterceptTextObjectPacket.end());
        }));

        this->mImpl->mListeners.emplace("PlayerScoreChanged", eventBus.emplaceListener<LOICollection::server::Events::PlayerScoreChangedEvent>([option = this->mImpl->options.ChangeScore](LOICollection::server::Events::PlayerScoreChangedEvent& event) -> void {
            using LOICollection::server::Events::ScoreChangedType;

            if (event.self().isSimulatedPlayer() || !option.ModuleEnabled || !event.getScore())
                return;

            int mScore = event.getScore();
            std::string mId = event.getObjective().mName;
            ScoreChangedType mType = event.getScoreChangedType();

            std::vector<std::string> mObjectScoreboards = option.ScoreboardLists;
            if (mObjectScoreboards.empty() || std::find(mObjectScoreboards.begin(), mObjectScoreboards.end(), mId) != mObjectScoreboards.end()) {
                int mOriScore = ScoreboardUtils::getScore(event.self(), mId);

                LanguagePlugin::getShared()->getLanguage(event.self())
                    .transform([&event, mScore, mId, mType, mOriScore](const std::string& language) -> void {
                        std::string mMessage = LOICollectionAPI::CallbackUtils::getInstance().translate(
                            tr(language, "monitor.changescore.text"), event.self()
                        );
                        
                        event.self().sendMessage(fmt::format(fmt::runtime(mMessage), mId,
                            (mType == ScoreChangedType::add ? (mOriScore - mScore) : (mType == ScoreChangedType::reduce ? (mOriScore + mScore) : mScore)),
                            (mType == ScoreChangedType::add ? "+" : (mType == ScoreChangedType::reduce ? "-" : "")) + std::to_string(mScore),
                            mOriScore
                        ));
                    })
                    .or_else(modules::defaultErrorHandler<MonitorPlugin>);
            }
        }));

        this->mImpl->mListeners.emplace("ExecutingCommand", eventBus.emplaceListener<ll::event::ExecutingCommandEvent>([option = this->mImpl->options.DisableCommand](ll::event::ExecutingCommandEvent& event) -> void {
            std::string mCommand = event.commandContext().mCommand.substr(1);

            if (!option.ModuleEnabled || event.commandContext().mOrigin == nullptr || mCommand.empty())
                return;

            auto mCommandArgs = mCommand
                | std::views::split(' ')
                | std::ranges::to<std::vector<std::string>>();

            std::vector<std::string> mObjectCommands = option.CommandLists;
            if (std::find(mObjectCommands.begin(), mObjectCommands.end(), mCommandArgs.front()) != mObjectCommands.end()) {
                event.cancel();

                Actor* entity = event.commandContext().mOrigin->getEntity();
                if (entity == nullptr || !entity->isType(ActorType::Player))
                    return;
                
                auto player = static_cast<Player*>(entity);

                LanguagePlugin::getShared()->getLanguage(*player)
                    .transform([&player](const std::string& language) -> void {
                        player->sendMessage(LOICollectionAPI::CallbackUtils::getInstance().translate(
                            tr(language, "monitor.disablecommand.text"), *player
                        ));
                    })
                    .or_else(modules::defaultErrorHandler<MonitorPlugin>);
            }
        }));

        this->mImpl->mListeners.emplace("NetworkPacketEvent", eventBus.emplaceListener<LOICollection::server::Events::NetworkPacketBeforeEvent>([option = this->mImpl->options.DisableCommand](LOICollection::server::Events::NetworkPacketBeforeEvent& event) -> void {
            if (!option.ModuleEnabled || event.getPacket().getId() != MinecraftPacketIds::AvailableCommands)
                return;

            auto& packet = static_cast<AvailableCommandsPacket&>(const_cast<Packet&>(event.getPacket()));

            std::vector<std::string> mObjectCommands = option.CommandLists;
            std::erase_if(packet.mCommands.get(), [mObjectCommands](AvailableCommandsPacket::CommandData& item) -> bool {
                return std::find(mObjectCommands.begin(), mObjectCommands.end(), item.name.get()) != mObjectCommands.end();
            });
        }));

        std::vector<std::string> mTextPacketType{"multiplayer.player.joined", "multiplayer.player.joined.renamed", "multiplayer.player.left"};
        this->mImpl->mListeners.emplace("NetworkBroadcastPacketEvent", eventBus.emplaceListener<LOICollection::server::Events::NetworkBroadcastPacketEvent>([this, mTextPacketType, option = this->mImpl->options.ServerToast](LOICollection::server::Events::NetworkBroadcastPacketEvent& event) -> void {
            if (!option.ModuleEnabled || event.getPacket().getId() != MinecraftPacketIds::Text)
                return;

            auto packet = static_cast<TextPacket const&>(event.getPacket());
            
            bool result = std::any_of(mTextPacketType.begin(), mTextPacketType.end(), [target = packet.mBody.get()](const std::string& item) -> bool {
                return std::visit([](auto&& arg) -> std::string {
                    return arg.mMessage;
                }, target).find(item) != std::string::npos;
            });

            std::string mAuthor = std::visit([](auto&& arg) -> std::string {
                using T = std::decay_t<decltype(arg)>;

                if constexpr (std::is_same_v<T, TextPacket::MessageAndParams>)
                    return arg.mParams->at(0);
                if constexpr (std::is_same_v<T, TextPacket::AuthorAndMessage>)
                    return arg.mAuthor;
                
                return "";
            }, packet.mBody.get());

            if (!result || mAuthor.empty())
                return;
            
            if (Player* player = ll::service::getLevel()->getPlayer(mAuthor); player) {
                std::string mUuid = player->getUuid().asString();
                if (std::find(this->mImpl->mInterceptTextObjectPacket.begin(), this->mImpl->mInterceptTextObjectPacket.end(), mUuid) != this->mImpl->mInterceptTextObjectPacket.end())
                    event.cancel();
            }
        }));
    }

    void MonitorPlugin::unlistenEvent() {
        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        for (auto& listener : this->mImpl->mListeners)
            eventBus.removeListener(listener.second);

        this->mImpl->mListeners.clear();

        this->mImpl->NameTaskRunning.store(false, std::memory_order_release);
        this->mImpl->BelowNameTaskRunning.store(false, std::memory_order_release);
        this->mImpl->DynamicMotdTaskRunning.store(false, std::memory_order_release);
        this->mImpl->SiebarTaskRunning.store(false, std::memory_order_release);

        this->mImpl->NameTaskSleep.interrupt();
        this->mImpl->BelowNameTaskSleep.interrupt();
        this->mImpl->DynamicMotdTaskSleep.interrupt();
        this->mImpl->SiebarTaskSleep.interrupt();
    }

    ll::Expected<void> MonitorPlugin::addSidebar(Player& player, const std::string& id, const std::string& name, SidebarType type) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MonitorPluginErrorCode::Invalid));

        SetDisplayObjectivePacket packet;
        packet.mDisplaySlotName = "sidebar";
        packet.mObjectiveName = id;
        packet.mObjectiveDisplayName = name;
        packet.mCriteriaName = "dummy";
        packet.mSortOrder = type == SidebarType::Ascending ? ObjectiveSortOrder::Ascending : ObjectiveSortOrder::Descending;
        
        packet.sendTo(player);

        return {};
    }

    ll::Expected<void> MonitorPlugin::setSidebar(Player& player, const std::string& id, std::vector<std::pair<std::string, int>> data) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MonitorPluginErrorCode::Invalid));

        auto packet = static_pointer_cast<SetScorePacket>(MinecraftPackets::createPacket(MinecraftPacketIds::SetScore));

        for (auto& item : data) {
            ChangeFakePlayerScore change;
            change.mScoreboardId->mRawID = item.second;
            change.mObjectiveName        = id;
            change.mScoreValue           = item.second;
            change.mFakePlayerName       = item.first;
            packet->mScoreInfo->emplace_back(change);
        }

        packet->sendTo(player);

        return {};
    }
    
    ll::Expected<void> MonitorPlugin::removeSidebar(Player& player, const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MonitorPluginErrorCode::Invalid));

        RemoveObjectivePacket packet;
        packet.mObjectiveName = id;

        packet.sendTo(player);

        return {};
    }

    bool MonitorPlugin::isValid() {
        return this->mImpl->options.ModuleEnabled;
    }

    std::string MonitorPlugin::getName() {
        return "MonitorPlugin";
    }

    modules::ModulePriority MonitorPlugin::getPriority() {
        return modules::ModulePriority::High;
    }

    ll::Expected<bool> MonitorPlugin::load() {
        if (!ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Monitor.ModuleEnabled)
            return false;

        this->mImpl->logger = ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA");
        this->mImpl->options = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Monitor;

        return true;
    }

    ll::Expected<bool> MonitorPlugin::unload() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        this->mImpl->options = {};

        if (this->mImpl->mRegistered.load(std::memory_order_acquire))
            this->unlistenEvent();

        return true;
    }

    ll::Expected<bool> MonitorPlugin::registry() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        this->listenEvent();

        this->mImpl->mRegistered.store(true, std::memory_order_release);

        return true;
    }

    ll::Expected<bool> MonitorPlugin::unregistry() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        this->unlistenEvent();

        this->mImpl->mRegistered.store(false, std::memory_order_release);

        return true;
    }
}
