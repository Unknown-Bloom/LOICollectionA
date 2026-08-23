#include <cmath>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <filesystem>
#include <unordered_map>

#include <fmt/core.h>

#include <ll/api/Expected.h>
#include <ll/api/io/Logger.h>
#include <ll/api/io/LoggerRegistry.h>

#include <ll/api/thread/ServerThreadExecutor.h>
#include <ll/api/base/Containers.h>

#include <ll/api/service/Bedrock.h>
#include <ll/api/command/Command.h>
#include <ll/api/command/CommandHandle.h>
#include <ll/api/command/CommandRegistrar.h>
#include <ll/api/event/EventBus.h>
#include <ll/api/event/ListenerBase.h>
#include <ll/api/event/player/PlayerJoinEvent.h>

#include <mc/deps/nbt/Tag.h>
#include <mc/deps/nbt/CompoundTag.h>

#include <mc/network/packet/TextPacket.h>

#include <mc/world/level/Level.h>
#include <mc/world/actor/player/Player.h>
#include <mc/world/actor/player/PlayerInventory.h>
#include <mc/world/actor/player/Inventory.h>

#include <mc/world/item/ItemStack.h>
#include <mc/world/item/SaveContext.h>
#include <mc/world/item/SaveContextFactory.h>

#include <mc/server/commands/CommandOrigin.h>
#include <mc/server/commands/CommandOutput.h>
#include <mc/server/commands/CommandPermissionLevel.h>

#include "LOICollectionA/include/CallbackUtils.h"
#include "LOICollectionA/include/server/Plugins/LanguagePlugin.h"

#include "LOICollectionA/coro/TimerManager.h"

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/mc-server/InventoryUtils.h"
#include "LOICollectionA/utils/mc-server/ScoreboardUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/data/SQLiteStorage.h"

#include "LOICollectionA/base/Cache.h"
#include "LOICollectionA/base/ScopeGuard.h"
#include "LOICollectionA/base/Wrapper.h"
#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/frontend/AST.h"

#include "LOICollectionA/include/form/GUIManager.h"

#include "LOICollectionA/include/server/Plugins/market/MarketPlugin.h"
#include "LOICollectionA/include/server/Plugins/market/MarketGui.h"
#include "LOICollectionA/include/server/Plugins/market/MarketStore.h"
#include "LOICollectionA/include/server/Plugins/market/MarketQuote.h"
#include "LOICollectionA/include/server/Events/modules/MarketItemSoldEvent.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    struct MarketPlugin::TradeEntry {
        std::string source;
        std::string target;

        MarketTradeType type = MarketTradeType::sell;
    };

    struct MarketPlugin::operation {
        int Days = 0;
    };



    struct MarketPlugin::Impl {
        std::shared_ptr<TimerManager> mTimerManager;

        ll::ConcurrentDenseMap<std::string, TradeEntry> mTrades;
        ll::ConcurrentDenseMap<std::string, TradeEntry> mTradeRequests;
        ll::ConcurrentDenseMap<std::string, bool> mSettling;

        LRUKCache<std::string, std::vector<std::string>> BlacklistCache;

        std::atomic<bool> mRegistered{ false };

        Config::C_Market options;

        std::shared_ptr<SQLiteStorage> db;
        std::shared_ptr<SQLiteStorage> db2;
        std::shared_ptr<ll::io::Logger> logger;

        std::string mGuiPath;
        std::string mGuiTradePath;
        std::string mGuiStorePath;

        ll::event::ListenerPtr PlayerJoinEventListener;
        ll::event::ListenerPtr MarketItemSoldEventListener;

        std::unique_ptr<MarketStore> mStore;
        std::unique_ptr<MarketQuote> mQuote;
        std::unique_ptr<MarketGui> mGui;

        Impl() : mTimerManager(std::make_shared<TimerManager>(ll::thread::ServerThreadExecutor::getDefault())),
            BlacklistCache(100, 100) {}
    };

    MarketPlugin::MarketPlugin() : mImpl(std::make_unique<Impl>()) {};
    MarketPlugin::~MarketPlugin() = default;

    std::shared_ptr<MarketPlugin> MarketPlugin::getShared() {
        static auto instance = std::shared_ptr<MarketPlugin>(new MarketPlugin());
        return instance;
    }

    std::error_code MarketPlugin::makeErrorCode(MarketPluginErrorCode e) {
        static MarketPluginErrorCategory cat;
        return std::error_code{ static_cast<int>(e), cat };
    }

    std::shared_ptr<SQLiteStorage> MarketPlugin::getDatabase() {
        return this->mImpl->db;
    }

    std::shared_ptr<ll::io::Logger> MarketPlugin::getLogger() {
        return this->mImpl->logger;
    }

    void MarketPlugin::registeryCommand() {
        ll::command::CommandHandle& command = ll::command::CommandRegistrar::getInstance(false)
            .getOrCreateCommand("market", tr({}, "commands.market.description"), CommandPermissionLevel::Any, CommandFlagValue::NotCheat | CommandFlagValue::Async);
        command.overload().text("gui").execute([](CommandOrigin const& origin, CommandOutput& output) -> void {
            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);

            form::GUIManager::getInstance().open("market", "market.open", form::GUIManagerType::CustomForm, player)
                .or_else(modules::defaultErrorHandler<MarketPlugin>);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
        command.overload().text("reload").execute([this](CommandOrigin const& origin, CommandOutput& output) -> void {
            if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors)
                return output.error(tr(origin.getLocaleCode(), "commands.generic.permission"));

            output.success(tr(origin.getLocaleCode(), "commands.generic.reload"));
            
            form::GUIManager::getInstance().load("market", this->mImpl->mGuiPath)
                .and_then([this]() -> ll::Expected<void> {
                    return form::GUIManager::getInstance().load("market.trade", this->mImpl->mGuiTradePath);
                })
                .and_then([this]() -> ll::Expected<void> {
                    return form::GUIManager::getInstance().load("market.store", this->mImpl->mGuiStorePath);
                })
                .transform([&origin, &output]() -> void {
                    output.success(tr(origin.getLocaleCode(), "commands.generic.reload.success"));
                })
                .or_else(modules::defaultErrorHandler<MarketPlugin>);
        });
        command.overload<operation>().text("report").optional("Days").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors)
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.permission"));

                int days = param.Days > 0 ? param.Days : 7;

                this->getReport(days)
                    .transform([&origin, &output, days](QuoteReport report) -> void {
                        output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.market.report.header")), days);
                        output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.market.report.count")), report.count);
                        output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.market.report.turnover")), report.turnover);
                        output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.market.report.tax")), report.tax);
                        output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.market.report.sellers")), report.activeSellers);
                    })
                    .or_else(modules::defaultErrorHandler<MarketPlugin>);
            });
    }


    ll::Expected<void> MarketPlugin::registeryUI() {
        return form::GUIManager::getInstance().load("market", this->mImpl->mGuiPath)
            .and_then([this]() -> ll::Expected<void> {
                return form::GUIManager::getInstance().load("market.trade", this->mImpl->mGuiTradePath);
            })
            .and_then([this]() -> ll::Expected<void> {
                return form::GUIManager::getInstance().load("market.store", this->mImpl->mGuiStorePath);
            })
            .and_then([this]() -> ll::Expected<void> {
                return this->mImpl->mGui->registerAll(*this);
            });
    }
    void MarketPlugin::listenEvent() {
        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        this->mImpl->PlayerJoinEventListener = eventBus.emplaceListener<ll::event::PlayerJoinEvent>([this](ll::event::PlayerJoinEvent& event) mutable -> void {
            if (event.self().isSimulatedPlayer())
                return;

            std::string uuid = event.self().getUuid().asString();

            this->mImpl->db2->has("Market", uuid)
                .and_then([this, uuid, name = event.self().getRealName()](bool exists) -> ll::Expected<void> {
                    if (!exists) {
                        std::unordered_map<std::string, std::string> data = {
                            { "name", name },
                            { "score", "0" }
                        };

                        return this->mImpl->db2->set("Market", uuid, data);
                    }

                    return {};
                })
                .or_else(modules::defaultErrorHandler<MarketPlugin>);
            
            this->mImpl->db2->get("Market", uuid, "score")
                .and_then([this, uuid, &event](const std::string& value) -> ll::Expected<void> {
                    int mScore = SystemUtils::toInt(value, 0);
                    if (mScore <= 0)
                        return {};

                    // 防重入：结算期间对该玩家加内存锁，join 事件重入时直接跳过
                    if (this->mImpl->mSettling.contains(uuid))
                        return {};

                    this->mImpl->mSettling[uuid] = true;
                    auto guard = make_scope_guard([this, uuid]() -> void {
                        this->mImpl->mSettling.erase(uuid);
                    });

                    // 先销账再发钱：先销账成功而发钱前崩溃，钱留在库里，下次 join 重发——宁可迟发不可多发
                    return this->mImpl->db2->set("Market", uuid, "score", "0")
                        .transform([this, uuid, mScore, &event]() -> void {
                            ScoreboardUtils::addScore(event.self(), this->mImpl->options.TargetScoreboard, mScore);
                        });
                })
                .or_else(modules::defaultErrorHandler<MarketPlugin>);
        });
        this->mImpl->MarketItemSoldEventListener = eventBus.emplaceListener<LOICollection::server::Events::MarketItemSoldEvent>([this](LOICollection::server::Events::MarketItemSoldEvent& event) mutable -> void {
            if (!this->mImpl->mQuote)
                return;

            this->mImpl->mQuote->onItemSold(
                event.getItemName(),
                event.getPrice(),
                event.getTax(),
                event.getTime(),
                event.getSellerUuid()
            );
        });
    }

    void MarketPlugin::unlistenEvent() {
        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        eventBus.removeListener(this->mImpl->PlayerJoinEventListener);
        eventBus.removeListener(this->mImpl->MarketItemSoldEventListener);

        this->mImpl->mTimerManager->cancelAll();
    }

    ll::Expected<bool> MarketPlugin::buyItem(Player& player, const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->getItemData(id)
            .and_then([this, id, &player]( std::unordered_map<std::string, std::string> data) -> ll::Expected<bool> {
                std::string mScoreboard = this->mImpl->options.TargetScoreboard;

                int mScore = SystemUtils::toInt(data.at("score"), 0);
                if (ScoreboardUtils::getScore(player, mScoreboard) < mScore) {
                    return LanguagePlugin::getShared()->getLanguage(player)
                        .transform([&player](const std::string& language) -> bool {
                            player.sendMessage(tr(language, "market.gui.sell.sellItem.tips3"));

                            return false;
                        });
                }

                ScoreboardUtils::reduceScore(player, mScoreboard, mScore);

                ItemStack mItemStack = ItemStack::fromTag(CompoundTag::fromSnbt(data.at("data"))->mTags);
                InventoryUtils::giveItem(player, mItemStack, static_cast<int>(mItemStack.mCount));

                player.refreshInventory();

                std::string mObject = data.at("player_uuid");
                if (Player* mPlayer = ll::service::getLevel()->getPlayer(mce::UUID::fromString(mObject)); mPlayer) {
                    return LanguagePlugin::getShared()->getLanguage(*mPlayer)
                        .transform([mScoreboard, mScore, &data, &mPlayer](const std::string& language) -> bool {
                            mPlayer->sendMessage(fmt::format(fmt::runtime(tr(language, "market.gui.sell.sellItem.tips1")), data.at("name")));

                            ScoreboardUtils::addScore(*mPlayer, mScoreboard, mScore);

                            return true;
                        });
                } else {
                    return this->mImpl->db2->get("Market", mObject, "score", "0")
                        .and_then([this, mScore, mObject](const std::string& value) -> ll::Expected<bool> {
                            int mMarketScore = SystemUtils::toInt(value, 0);

                            return this->mImpl->db2->set("Market", mObject, "score", std::to_string(mMarketScore + mScore))
                                .transform([]() -> bool {
                                    return true;
                                });
                        });
                }

                return this->delItem(id)
                    .transform([this, &data, &player]() -> bool {
                        this->getLogger()->info(fmt::runtime(LOICollectionAPI::CallbackUtils::getInstance().translate(tr({}, "market.log2"), player)), data.at("name"));
    
                        return true;
                    });
            });
    }

    ll::Expected<bool> MarketPlugin::offshelfItem(Player& player, const std::string& id, bool returnItem) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->getItemData(id)
            .and_then([this, id, returnItem, &player](std::unordered_map<std::string, std::string> data) -> ll::Expected<bool> {
                if (returnItem) {
                    ItemStack mItemStack = ItemStack::fromTag(CompoundTag::fromSnbt(data.at("data"))->mTags);
                    InventoryUtils::giveItem(player, mItemStack, static_cast<int>(mItemStack.mCount));
                }

                return LanguagePlugin::getShared()->getLanguage(player)
                    .and_then([this, id, &data, &player](const std::string& language) -> ll::Expected<void> {
                        player.sendMessage(fmt::format(fmt::runtime(tr(language, "market.gui.sell.sellItem.tips2")), data.at("name")));

                        return this->delItem(id);
                    })
                    .transform([this, &data, &player]() -> bool {
                        this->getLogger()->info(fmt::runtime(LOICollectionAPI::CallbackUtils::getInstance().translate(tr({}, "market.log3"), player)), data.at("name"));

                        return true;
                    });
            });
    }
    
    ll::Expected<bool> MarketPlugin::sellItem(Player& player, int slot, const std::string& name, const std::string& icon, const std::string& intr, int score) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        ItemStack mItemStack = player.mInventory->mInventory->getItem(slot);
        if (!mItemStack || mItemStack.isNull())
            return false;

        return this->addItem(player, mItemStack, name, icon, intr, score)
            .transform([slot, &player]() -> bool {
                player.mInventory->mInventory->removeItem(slot, 64);
                player.refreshInventory();

                return true;
            });
    }

    ll::Expected<void> MarketPlugin::addBlacklist(Player& player, Player& target) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        std::string mObject = player.getUuid().asString();
        std::string mTargetObject = target.getUuid().asString();
        std::string mTimestamp = SystemUtils::getCurrentTimestamp();

        std::unordered_map<std::string, std::string> mData = {
            { "name", target.getRealName() },
            { "target", mTargetObject },
            { "author", mObject },
            { "time", mTimestamp }
        };

        return this->getDatabase()->set("Blacklist", mTimestamp, mData)
            .transform([this, mObject, mTargetObject, mTimestamp, &player]() -> void {
                this->getLogger()->info(fmt::runtime(LOICollectionAPI::CallbackUtils::getInstance().translate(tr({}, "market.log4"), player)), mTargetObject);

                if (this->mImpl->BlacklistCache.contains(mObject))
                    this->mImpl->BlacklistCache.update(mObject, [mTimestamp](std::shared_ptr<std::vector<std::string>> mList) -> void {
                        mList->push_back(mTimestamp);
                    });
            });
    }

    ll::Expected<void> MarketPlugin::addItem(Player& player, ItemStack& item, const std::string& name, const std::string& icon, const std::string& intr, int score) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        std::string mTimestamp = SystemUtils::getCurrentTimestamp();

        std::unordered_map<std::string, std::string> mData = {
            { "name", name },
            { "icon", icon },
            { "introduce", intr },
            { "score", std::to_string(score) },
            { "data", item.save(*SaveContextFactory::createCloneSaveContext())->toSnbt(SnbtFormat::Minimize, 0) },
            { "player_name", player.getRealName() },
            { "player_uuid", player.getUuid().asString() }
        };

        return this->getDatabase()->set("Item", mTimestamp, mData)
            .transform([this, name, &player]() -> void {
                this->getLogger()->info(fmt::runtime(LOICollectionAPI::CallbackUtils::getInstance().translate(tr({}, "market.log2"), player)), name);
            });
    }

    ll::Expected<void> MarketPlugin::delBlacklist(Player& player, const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->hasBlacklist(player, id)
            .and_then([this, id](bool exists) -> ll::Expected<void> {
                if (!exists) {
                    this->getLogger()->warn(fmt::runtime(tr({}, "console.log.error.object")), this->getName());

                    return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::BlacklistNotFound));
                }

                return this->getDatabase()->del("Blacklist", id);
            })
            .transform([this, id, &player]() -> void {
                this->getLogger()->info(fmt::runtime(LOICollectionAPI::CallbackUtils::getInstance().translate(tr({}, "market.log5"), player)), id);

                this->mImpl->BlacklistCache.update(player.getUuid().asString(), [id](std::shared_ptr<std::vector<std::string>> mList) -> void {
                    mList->erase(std::remove(mList->begin(), mList->end(), id), mList->end());
                });
            });
    }

    ll::Expected<void> MarketPlugin::delItem(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->hasItem(id)
            .and_then([this, id](bool exists) -> ll::Expected<void> {
                if (!exists) {
                    this->getLogger()->warn(fmt::runtime(tr({}, "console.log.error.object")), this->getName());

                    return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::ItemNotFound));
                }

                return this->getDatabase()->del("Item", id);
            });
    }

    ll::Expected<void> MarketPlugin::setExecutor(const ll::coro::Executor& executor) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        this->mImpl->mTimerManager->setExecutor(executor);

        return {};
    }

    ll::Expected<bool> MarketPlugin::acceptRequest(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        std::string mObject = player.getUuid().asString();
        if (!this->mImpl->mTradeRequests.contains(mObject))
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::RequestNotFound));

        TradeEntry mEntry = this->mImpl->mTradeRequests.at(mObject);

        Player* sourcePlayer = ll::service::getLevel()->getPlayer(mce::UUID::fromString(mEntry.source));
        if (!sourcePlayer) {
            return LanguagePlugin::getShared()->getLanguage(player)
                .transform([&player](const std::string& language) -> bool {
                    player.sendMessage(tr(language, "market.gui.error"));

                    return false;
                });
        }

        auto& mover = (mEntry.type == MarketTradeType::sell) ? player : *sourcePlayer;

        return LanguagePlugin::getShared()->getLanguage(*sourcePlayer)
            .and_then([&sourcePlayer, &mover](const std::string& language) -> ll::Expected<std::string> {
                sourcePlayer->sendMessage(tr(language, "market.yes.tips"));

                return LanguagePlugin::getShared()->getLanguage(mover);
            })
            .and_then([&mover](const std::string& language) -> ll::Expected<void> {
                mover.sendMessage(tr(language, "market.tips4"));

                return {};
            })
            .and_then([this, &sourcePlayer, &player, type = mEntry.type]() -> ll::Expected<void> { 
                return this->sendTrade(*sourcePlayer, player, type);
            })
            .transform([this, mObject, source = mEntry.source]() -> bool {
                this->mImpl->mTradeRequests.erase(mObject);
                this->mImpl->mTradeRequests.erase(source);

                this->mImpl->mTimerManager->cancel(source);

                return true;
            });
    }

    ll::Expected<bool> MarketPlugin::rejectRequest(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        std::string mObject = player.getUuid().asString();
        if (!this->mImpl->mTradeRequests.contains(mObject))
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::RequestNotFound));

        TradeEntry mEntry = this->mImpl->mTradeRequests.at(mObject);

        if (Player* sourcePlayer = ll::service::getLevel()->getPlayer(mce::UUID::fromString(mEntry.source)); sourcePlayer) {
            auto language = LanguagePlugin::getShared()->getLanguage(*sourcePlayer);
            if (!language.has_value())
                return ll::Unexpected(language.error());

            sourcePlayer->sendMessage(tr(language.value(), "market.no.tips"));
        }
        
        this->mImpl->mTradeRequests.erase(mObject);
        this->mImpl->mTradeRequests.erase(mEntry.source);

        this->mImpl->mTimerManager->cancel(mEntry.source);

        return true;
    }

    ll::Expected<bool> MarketPlugin::cancelRequest(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        std::string mObject = player.getUuid().asString();
        if (!this->mImpl->mTradeRequests.contains(mObject))
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::RequestNotFound));

        TradeEntry mEntry = this->mImpl->mTradeRequests.at(mObject);
        
        this->mImpl->mTradeRequests.erase(mObject);
        this->mImpl->mTradeRequests.erase(mEntry.target);

        this->mImpl->mTimerManager->cancel(mEntry.source);

        return true;
    }

    ll::Expected<bool> MarketPlugin::acceptTrade(Player& player, int slot, int score) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        std::string mObject = player.getUuid().asString();
        if (!this->mImpl->mTrades.contains(mObject))
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::TradeNotFound));

        TradeEntry mEntry = this->mImpl->mTrades.at(mObject);

        ItemStack mItemStack = player.getInventory().getItem(slot);
        if (!mItemStack || mItemStack.isNull())
            return false;

        Player* mPlayer = (mEntry.source == mObject) ?
            ll::service::getLevel()->getPlayer(mce::UUID::fromString(mEntry.target)) :
            ll::service::getLevel()->getPlayer(mce::UUID::fromString(mEntry.source));

        if (!mPlayer)
            return false;

        if (ScoreboardUtils::getScore(*mPlayer, this->mImpl->options.TargetScoreboard) < score || score <= 0)
            return false;

        player.mInventory->mInventory->removeItem(slot, 64);
        mPlayer->getInventory().addItem(mItemStack);

        player.refreshInventory();
        mPlayer->refreshInventory();

        ScoreboardUtils::addScore(player, this->mImpl->options.TargetScoreboard, score);
        ScoreboardUtils::reduceScore(*mPlayer, this->mImpl->options.TargetScoreboard, score);

        this->mImpl->mTrades.erase(mObject);
        this->mImpl->mTrades.erase(mEntry.target);

        this->mImpl->mTimerManager->cancel(mEntry.source + "_trade");

        return true;
    }

    ll::Expected<bool> MarketPlugin::cancelTrade(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        std::string mObject = player.getUuid().asString();
        if (!this->mImpl->mTrades.contains(mObject))
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::TradeNotFound));

        TradeEntry mEntry = this->mImpl->mTrades.at(mObject);

        Player* mPlayer = (mEntry.source == mObject) ?
            ll::service::getLevel()->getPlayer(mce::UUID::fromString(mEntry.target)) :
            ll::service::getLevel()->getPlayer(mce::UUID::fromString(mEntry.source));
        if (mPlayer) {
            auto language = LanguagePlugin::getShared()->getLanguage(*mPlayer);
            if (!language.has_value())
                return ll::Unexpected(language.error());
            
            mPlayer->sendMessage(tr(language.value(), "market.tips6"));
        }

        this->mImpl->mTrades.erase(mEntry.source);
        this->mImpl->mTrades.erase(mEntry.target);

        this->mImpl->mTimerManager->cancel(mEntry.source + "_trade");

        this->getLogger()->info(fmt::runtime(tr({}, "market.log8")), player.getRealName());

        return true;
    }

    ll::Expected<void> MarketPlugin::sendRequest(Player& player, Player& target, MarketTradeType type) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        std::string mObject = player.getUuid().asString();
        std::string mTargetObject = target.getUuid().asString();

        TradeEntry mEntry{ mObject, mTargetObject, type };
        this->mImpl->mTradeRequests[mObject] = mEntry;
        this->mImpl->mTradeRequests[mTargetObject] = mEntry;

        this->mImpl->mTimerManager->schedule(mObject, std::chrono::seconds(this->mImpl->options.TradeRequestTimeout), [this, mObject, mTargetObject]() -> void {
            if (!this->mImpl->mTradeRequests.contains(mObject) || !this->mImpl->mTradeRequests.contains(mTargetObject))
                return;

            if (Player* mPlayer = ll::service::getLevel()->getPlayer(mce::UUID::fromString(mObject)); mPlayer) {
                LanguagePlugin::getShared()->getLanguage(*mPlayer)
                    .transform([&mPlayer](const std::string& language) -> void {
                        mPlayer->sendMessage(tr(language, "market.tips2"));
                    })
                    .or_else(modules::defaultErrorHandler<MarketPlugin>);
            }
            
            if (Player* mPlayer = ll::service::getLevel()->getPlayer(mce::UUID::fromString(mTargetObject)); mPlayer) {
                LanguagePlugin::getShared()->getLanguage(*mPlayer)
                    .transform([&mPlayer](const std::string& language) -> void {
                        mPlayer->sendMessage(tr(language, "market.tips2"));
                    })
                    .or_else(modules::defaultErrorHandler<MarketPlugin>);
            }

            this->mImpl->mTradeRequests.erase(mObject);
            this->mImpl->mTradeRequests.erase(mTargetObject);
        });

        return LanguagePlugin::getShared()->getLanguage(player)
            .transform([this, &player, name = target.getRealName()](const std::string& language) -> void {
                player.sendMessage(tr(language, "market.tips1"));

                this->getLogger()->info(fmt::runtime(tr({}, "market.log6")), player.getRealName(), name);
            });
    }

    ll::Expected<void> MarketPlugin::sendTrade(Player& player, Player& target, MarketTradeType type) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        std::string mObject = player.getUuid().asString();
        std::string mTargetObject = target.getUuid().asString();

        TradeEntry mEntry{ mObject, mTargetObject, type };
        this->mImpl->mTrades[mObject] = mEntry;
        this->mImpl->mTrades[mTargetObject] = mEntry;

        this->mImpl->mTimerManager->schedule(mObject + "_trade", std::chrono::seconds(this->mImpl->options.TradeTimeout), [this, mObject, mTargetObject]() -> void {
            if (!this->mImpl->mTrades.contains(mObject) || !this->mImpl->mTrades.contains(mTargetObject))
                return;

            if (Player* mPlayer = ll::service::getLevel()->getPlayer(mce::UUID::fromString(mObject)); mPlayer) {
                LanguagePlugin::getShared()->getLanguage(*mPlayer)
                    .transform([&mPlayer](const std::string& language) -> void {
                        mPlayer->sendMessage(tr(language, "market.tips5"));
                    })
                    .or_else(modules::defaultErrorHandler<MarketPlugin>);
            }
            
            if (Player* mPlayer = ll::service::getLevel()->getPlayer(mce::UUID::fromString(mTargetObject)); mPlayer) {
                LanguagePlugin::getShared()->getLanguage(*mPlayer)
                    .transform([&mPlayer](const std::string& language) -> void {
                        mPlayer->sendMessage(tr(language, "market.tips5"));
                    })
                    .or_else(modules::defaultErrorHandler<MarketPlugin>);
            }

            this->mImpl->mTrades.erase(mObject);
            this->mImpl->mTrades.erase(mTargetObject);
        });

        this->getLogger()->info(fmt::runtime(tr({}, "market.log7")), player.getRealName(), target.getRealName());

        return {};
    }

    ll::Expected<bool> MarketPlugin::hasTrade(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->mTradeRequests.contains(player.getUuid().asString()) || this->mImpl->mTrades.contains(player.getUuid().asString());
    }

    ll::Expected<std::string> MarketPlugin::getBlacklist(Player& player, Player& target) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->getDatabase()->find("Blacklist", {
            { "target", target.getUuid().asString() },
            { "author", player.getUuid().asString() }
        }, "", SQLiteStorage::FindCondition::AND);
    }

    ll::Expected<std::vector<std::string>> MarketPlugin::getBlacklist(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->getBlacklist(player.getUuid().asString());
    }

    ll::Expected<std::vector<std::string>> MarketPlugin::getBlacklist(const std::string& target) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        if (this->mImpl->BlacklistCache.contains(target))
            return *this->mImpl->BlacklistCache.get(target).value();

        return this->getDatabase()->find("Blacklist", {
            { "target", target }
        }, SQLiteStorage::FindCondition::AND)
            .transform([this, target](const std::vector<std::string>& ids) -> std::vector<std::string> {
                this->mImpl->BlacklistCache.put(target, ids);

                return ids;
            });
    }

    ll::Expected<std::vector<std::string>> MarketPlugin::getItems() {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->getDatabase()->list("Item");
    }

    ll::Expected<std::vector<std::string>> MarketPlugin::getItems(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->getDatabase()->find("Item", {
            { "player_uuid", player.getUuid().asString() }
        }, SQLiteStorage::FindCondition::AND);
    }

    ll::Expected<std::unordered_map<std::string, std::string>> MarketPlugin::getItemData(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->getDatabase()->get("Item", id);
    }

    ll::Expected<std::unordered_map<std::string, std::string>> MarketPlugin::getBlacklistData(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->getDatabase()->get("Blacklist", id);
    }

    ll::Expected<std::unordered_map<std::string, std::unordered_map<std::string, std::string>>> MarketPlugin::getItemsData(const std::vector<std::string>& ids) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->getDatabase()->get("Item", ids);
    }

    ll::Expected<bool> MarketPlugin::hasItem(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->getDatabase()->has("Item", id);
    }

    ll::Expected<bool> MarketPlugin::hasBlacklist(Player& player, const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        std::string mObject = player.getUuid().asString();
        if (this->mImpl->BlacklistCache.contains(mObject)) {
            auto mKeys = this->mImpl->BlacklistCache.get(mObject).value();
            return std::find(mKeys->begin(), mKeys->end(), id) != mKeys->end();
        }

        return this->getDatabase()->has("Blacklist", id);
    }


    const Config::C_Market& MarketPlugin::getOptions() const {
        return this->mImpl->options;
    }

    ll::Expected<std::vector<std::string>> MarketPlugin::getStoreRanking() {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->mStore->getStoreRanking();
    }

    ll::Expected<std::unordered_map<std::string, std::string>> MarketPlugin::getStore(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->mStore->getStore(id);
    }

    ll::Expected<std::unordered_map<std::string, std::string>> MarketPlugin::getStore(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->mStore->getStore(player);
    }

    ll::Expected<std::vector<std::string>> MarketPlugin::getStoreItems(const std::string& storeId) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->mStore->getStoreItems(storeId);
    }

    ll::Expected<std::unordered_map<std::string, std::string>> MarketPlugin::getStoreItemData(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->mStore->getStoreItemData(id);
    }

    ll::Expected<bool> MarketPlugin::hasPurchasedInStore(Player& player, const std::string& storeId) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->mStore->hasPurchasedInStore(player, storeId);
    }

    ll::Expected<std::vector<std::string>> MarketPlugin::getReviews(const std::string& storeId, MarketStoreReviewStatus status) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->mStore->getReviews(storeId, status);
    }

    ll::Expected<std::unordered_map<std::string, std::string>> MarketPlugin::getReviewData(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->mStore->getReviewData(id);
    }

    ll::Expected<bool> MarketPlugin::createStore(Player& player, const std::string& name, const std::string& icon, const std::string& introduce) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->mStore->createStore(player, name, icon, introduce);
    }

    ll::Expected<bool> MarketPlugin::dissolveStore(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->mStore->dissolveStore(player);
    }

    ll::Expected<bool> MarketPlugin::uploadStoreItem(Player& player, int slot, const std::string& name, const std::string& icon, const std::string& intr, int score) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->mStore->uploadStoreItem(player, slot, name, icon, intr, score);
    }

    ll::Expected<bool> MarketPlugin::offshelfStoreItem(Player& player, const std::string& id, bool returnItem) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->mStore->offshelfStoreItem(player, id, returnItem);
    }

    ll::Expected<bool> MarketPlugin::buyStoreItem(Player& player, const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->mStore->buyStoreItem(player, id);
    }

    ll::Expected<bool> MarketPlugin::addReview(Player& player, const std::string& storeId, int rating, const std::string& content) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->mStore->addReview(player, storeId, rating, content);
    }

    ll::Expected<bool> MarketPlugin::auditReview(Player& player, const std::string& id, bool approve) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->mStore->auditReview(player, id, approve);
    }

    double MarketPlugin::computeStoreScore(const StoreScoreInput& input, const Config::C_Market& options) {
        return MarketStore::computeStoreScore(input, options);
    }

    ll::Expected<std::optional<QuoteInfo>> MarketPlugin::getQuote(const std::string& itemName) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->mQuote->getQuote(itemName);
    }

    ll::Expected<std::vector<std::pair<std::string, long long>>> MarketPlugin::getTopVolume(int limit, int days) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->mQuote->getTopVolume(limit, days);
    }

    ll::Expected<std::vector<std::pair<std::string, long long>>> MarketPlugin::getTopTurnover(int limit, int days) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->mQuote->getTopTurnover(limit, days);
    }

    ll::Expected<QuoteReport> MarketPlugin::getReport(int days) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(MarketPluginErrorCode::Invalid));

        return this->mImpl->mQuote->getReport(days);
    }

    void MarketPlugin::clearStoreRankCache() {
        if (this->mImpl->mStore)
            this->mImpl->mStore->clearRankCache();
    }

    void MarketPlugin::startStoreRankRefresh() {
        if (this->mImpl->mStore)
            this->mImpl->mStore->startRankRefresh();
    }

    bool MarketPlugin::isValid() {
        return this->getLogger() != nullptr && this->getDatabase() != nullptr && this->mImpl->db2 != nullptr;
    }

    std::vector<std::string> MarketPlugin::getProhibitedItems() {
        return this->mImpl->options.ProhibitedItems;
    }

    int MarketPlugin::getBlacklistUpload() {
        return this->mImpl->options.BlacklistUpload;
    }

    int MarketPlugin::getMaximumUpload() {
        return this->mImpl->options.MaximumUpload;
    }

    std::string MarketPlugin::getName() {
        return "MarketPlugin";
    }

    modules::ModulePriority MarketPlugin::getPriority() {
        return modules::ModulePriority::High;
    }

    ll::Expected<bool> MarketPlugin::load() {
        if (!ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Market.ModuleEnabled)
            return false;
        
        auto mDataPath = std::filesystem::path(ServiceProvider::getInstance().getService<std::string>("DataPath")->data());

        this->mImpl->db = std::make_shared<SQLiteStorage>((mDataPath / "market.db").string());
        this->mImpl->db2 = ServiceProvider::getInstance().getService<SQLiteStorage>("SettingsDB");
        this->mImpl->logger = ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA");
        this->mImpl->options = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Market;
        
        auto mGuiPath = std::filesystem::path(ServiceProvider::getInstance().getService<std::string>("GuiPath")->data());

        this->mImpl->mGuiPath = (mGuiPath / "market.lcui").string();
        this->mImpl->mGuiTradePath = (mGuiPath / "market_trade.lcui").string();
        this->mImpl->mGuiStorePath = (mGuiPath / "market_store.lcui").string();

        this->mImpl->mStore = std::make_unique<MarketStore>(
            this->mImpl->db,
            this->mImpl->db2,
            this->mImpl->options,
            this->mImpl->logger,
            *this->mImpl->mTimerManager,
            [this](const std::string& target) -> ll::Expected<std::vector<std::string>> {
                return this->getBlacklist(target);
            }
        );
        this->mImpl->mQuote = std::make_unique<MarketQuote>(
            this->mImpl->db,
            this->mImpl->options,
            this->mImpl->logger,
            *this->mImpl->mTimerManager
        );
        this->mImpl->mGui = std::make_unique<MarketGui>();

        return true;
    }

    ll::Expected<bool> MarketPlugin::unload() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        this->mImpl->mStore.reset();
        this->mImpl->mQuote.reset();
        this->mImpl->mGui.reset();

        this->mImpl->db.reset();
        this->mImpl->db2.reset();
        this->mImpl->logger.reset();
        this->mImpl->options = {};

        if (this->mImpl->mRegistered.load(std::memory_order_acquire))
            this->unlistenEvent();

        return true;
    }

    ll::Expected<bool> MarketPlugin::registry() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        return this->mImpl->db2->create("Market", [](SQLiteStorage::ColumnCallback ctor) -> void {
            ctor("name");
            ctor("score");
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->db2->create("MarketTax", [](SQLiteStorage::ColumnCallback ctor) -> void {
                ctor("total");
            });
        }).and_then([this]() -> ll::Expected<void> {
            return this->getDatabase()->create("Item", [](SQLiteStorage::ColumnCallback ctor) -> void {
                ctor("name");
                ctor("icon");
                ctor("introduce");
                ctor("score");
                ctor("data");
                ctor("player_name");
                ctor("player_uuid");
            });
        }).and_then([this]() -> ll::Expected<void> {
            return this->getDatabase()->create("Blacklist", [](SQLiteStorage::ColumnCallback ctor) -> void {
                ctor("name");
                ctor("target");
                ctor("author");
                ctor("time");
            });
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->mStore->createTables();
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->mQuote->start();
        }).and_then([this]() -> ll::Expected<void> {
            return this->registeryUI();
        }).transform([this]() -> bool {
            this->registeryCommand();
            this->listenEvent();
            this->startStoreRankRefresh();

            this->mImpl->mRegistered.store(true, std::memory_order_release);

            return true;
        }); 
    }

    ll::Expected<bool> MarketPlugin::unregistry() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        this->unlistenEvent();

        return this->getDatabase()->exec("VACUUM;")
            .transform([this]() -> bool {
                this->mImpl->mRegistered.store(false, std::memory_order_release);

                return true;
            });
    }
}
