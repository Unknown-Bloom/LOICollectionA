#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
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
#include <ll/api/event/player/PlayerChatEvent.h>
#include <ll/api/event/player/PlayerDisconnectEvent.h>

#include <mc/deps/core/string/HashedString.h>

#include <mc/world/level/Level.h>
#include <mc/world/actor/ActorDefinitionIdentifier.h>
#include <mc/world/actor/player/Player.h>

#include <mc/network/packet/TextPacket.h>

#include <mc/server/commands/CommandOrigin.h>
#include <mc/server/commands/CommandOutput.h>
#include <mc/server/commands/CommandSelector.h>
#include <mc/server/commands/CommandPermissionLevel.h>

#include "LOICollectionA/include/CallbackUtils.h"
#include "LOICollectionA/include/server/Plugins/LanguagePlugin.h"

#include "LOICollectionA/include/server/Events/modules/WalletTransferEvent.h"
#include "LOICollectionA/include/server/Events/modules/RedEnvelopeCompletedEvent.h"

#include "LOICollectionA/coro/TimerManager.h"

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/mc-server/ScoreboardUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/data/SQLiteStorage.h"

#include "LOICollectionA/include/form/GUIManager.h"

#include "LOICollectionA/base/ScopeGuard.h"
#include "LOICollectionA/base/Wrapper.h"
#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/wallet/WalletPlugin.h"
#include "LOICollectionA/include/server/Plugins/wallet/WalletLedger.h"
#include "LOICollectionA/include/server/Plugins/wallet/WalletRedEnvelope.h"
#include "LOICollectionA/include/server/Plugins/wallet/WalletBank.h"
#include "LOICollectionA/include/server/Plugins/wallet/WalletGui.h"

using I18nUtilsTools::tr;

namespace LOICollection::server::Plugins {
    struct WalletPlugin::operation {
        CommandSelector<Player> Target;
        int Score = 0;
    };

    struct WalletPlugin::operationQuery {
        std::string PlayerName;
    };

    struct WalletPlugin::operationQueryId {
        std::string EnvelopeId;
    };

    struct WalletPlugin::Impl {
        std::shared_ptr<TimerManager> mTimerManager;

        ll::ConcurrentDenseMap<std::string, bool> mSettling;
        ll::ConcurrentDenseMap<std::string, int64_t> mLastTransferTime;

        std::atomic<bool> mRegistered{ false };

        Config::C_Wallet options;

        std::shared_ptr<SQLiteStorage> db;
        std::shared_ptr<ll::io::Logger> logger;

        std::string mGuiPath;

        ll::event::ListenerPtr PlayerJoinEventListener;
        ll::event::ListenerPtr PlayerChatEventListener;
        ll::event::ListenerPtr PlayerDisconnectEventListener;
        ll::event::ListenerPtr WalletTransferEventListener;
        ll::event::ListenerPtr RedEnvelopeCompletedEventListener;

        std::unique_ptr<WalletLedger> mLedger;
        std::unique_ptr<WalletRedEnvelope> mRedEnvelope;
        std::unique_ptr<WalletBank> mBank;
        std::unique_ptr<WalletGui> mGui;

        Impl() : mTimerManager(std::make_shared<TimerManager>(ll::thread::ServerThreadExecutor::getDefault())) {}
    };

    WalletPlugin::WalletPlugin() : mImpl(std::make_unique<Impl>()) {};
    WalletPlugin::~WalletPlugin() = default;

    std::shared_ptr<WalletPlugin> WalletPlugin::getShared() {
        static auto instance = std::shared_ptr<WalletPlugin>(new WalletPlugin());
        return instance;
    }

    std::error_code WalletPlugin::makeErrorCode(WalletPluginErrorCode e) {
        static WalletPluginErrorCategory cat;
        return std::error_code{ static_cast<int>(e), cat };
    }

    std::shared_ptr<ll::io::Logger> WalletPlugin::getLogger() {
        return this->mImpl->logger;
    }

    const Config::C_Wallet& WalletPlugin::getOptions() const {
        return this->mImpl->options;
    }

    bool WalletPlugin::isValid() {
        return this->getLogger() != nullptr && this->mImpl->db != nullptr;
    }

    ll::Expected<std::string> WalletPlugin::getPlayerInfo(const std::string& uuid) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->db->get("Wallet", uuid, "name", "Unknown");
    }

    ll::Expected<std::vector<std::pair<std::string, std::string>>> WalletPlugin::getPlayerInfo() {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->db->list("Wallet")
            .and_then([this](const std::vector<std::string>& ids) -> ll::Expected<std::unordered_map<std::string, std::unordered_map<std::string, std::string>>> {
                return this->mImpl->db->get("Wallet", ids);
            })
            .transform([](std::unordered_map<std::string, std::unordered_map<std::string, std::string>> mData) -> std::vector<std::pair<std::string, std::string>> {
                std::vector<std::pair<std::string, std::string>> result;

                result.reserve(mData.size());
                for (auto& [id, data] : mData)
                    result.emplace_back(id, data.at("name"));

                return result;
            });
    }

    ll::Expected<void> WalletPlugin::updateBalanceSnapshot(const std::string& uuid, long long balance) {
        if (!this->isValid() || uuid.empty())
            return {};

        return this->mImpl->db->set("Wallet", uuid, "balance", std::to_string(balance));
    }

    ll::Expected<bool> WalletPlugin::forTransfer(Player& player, const std::string& target, const std::string& name, int score, bool confirmed) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        if (this->mImpl->options.TransferConfirmThreshold > 0 && score > this->mImpl->options.TransferConfirmThreshold && !confirmed)
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::ConfirmRequired));

        if (auto verification = this->validateTransfer(player.getUuid().asString(), score); !verification.has_value())
            return ll::Unexpected(verification.error());

        std::string mScoreboard = this->mImpl->options.TargetScoreboard;
        if (ScoreboardUtils::getScore(player, mScoreboard) < score || score <= 0)
            return false;

        ScoreboardUtils::reduceScore(player, mScoreboard, score);

        int mTargetMoney = static_cast<int>(score * (1 - this->mImpl->options.ExchangeRate));

        return this->transfer(target, mTargetMoney)
            .transform([this, target, name, score, mTargetMoney, uuid = player.getUuid().asString(), playerName = player.getRealName()]() -> bool {
                this->getLogger()->info(fmt::runtime(tr({}, "wallet.log")), playerName, name, score);

                this->updateTransferCooldown(uuid);

                long long fee = static_cast<long long>(score) - mTargetMoney;
                if (fee > 0)
                    this->mImpl->mLedger->accumulateFee(fee).or_else(modules::defaultErrorHandler<WalletPlugin>);

                this->mImpl->mLedger->record(uuid, playerName, target, name, mTargetMoney, fee, "transfer");

                return true;
            });
    }

    ll::Expected<void> WalletPlugin::validateTransfer(const std::string& uuid, int spend) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        const auto& options = this->mImpl->options;

        if (options.TransferMinAmount > 0 && spend < options.TransferMinAmount)
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::BelowMinimum));

        if (options.TransferCooldownSeconds > 0) {
            auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            if (auto it = this->mImpl->mLastTransferTime.find(uuid); it != this->mImpl->mLastTransferTime.end() && now - it->second < options.TransferCooldownSeconds)
                return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::CooldownActive));
        }

        if (options.TransferDailyLimit > 0) {
            long long today = this->mImpl->mLedger->getTodayOutgoing(uuid).value_or(0);
            if (today + spend > options.TransferDailyLimit)
                return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::DailyLimitExceeded));
        }

        return {};
    }

    void WalletPlugin::updateTransferCooldown(const std::string& uuid) {
        if (this->mImpl->options.TransferCooldownSeconds <= 0)
            return;

        auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        this->mImpl->mLastTransferTime[uuid] = now;
    }

    ll::Expected<void> WalletPlugin::transfer(const std::string& target, int score) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        if (Player* mObject = ll::service::getLevel()->getPlayer(mce::UUID::fromString(target)); mObject) {
            ScoreboardUtils::addScore(*mObject, this->mImpl->options.TargetScoreboard, score);

            return {};
        }

        return this->mImpl->db->get("Wallet", target, "score", "0")
            .and_then([this, score, target](const std::string& value) -> ll::Expected<void> {
                int walletScore = SystemUtils::toInt(value);

                return this->mImpl->db->set("Wallet", target, "score", std::to_string(walletScore + score))
                    .transform([this, target, walletScore, score]() -> void {
                        this->updateBalanceSnapshot(target, static_cast<long long>(walletScore) + score)
                            .or_else(modules::defaultErrorHandler<WalletPlugin>);
                    });
            });
    }

    ll::Expected<void> WalletPlugin::wealth(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        int score = ScoreboardUtils::getScore(player, this->mImpl->options.TargetScoreboard);

        ll::service::getLevel()->forEachPlayer([score, &player](Player& target) -> bool {
            LanguagePlugin::getShared()->getLanguage(target)
                .transform([score, &player, &target](const std::string& language) -> void {
                    std::string mMessage = LOICollectionAPI::CallbackUtils::getInstance().translate(
                        tr(language, "wallet.showOff"), player
                    );

                    TextPacket::createRawMessage(
                        fmt::format(fmt::runtime(mMessage), score)
                    ).sendTo(target);
                })
                .or_else(modules::defaultErrorHandler<WalletPlugin>);

            return true;
        });

        return {};
    }

    ll::Expected<void> WalletPlugin::tryGrabRedEnvelope(Player& player, const std::string& message) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->mRedEnvelope->tryGrab(player, message);
    }

    ll::Expected<void> WalletPlugin::redenvelope(Player& player, const std::string& key, int score, int count, const std::vector<std::string>& targets) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->mRedEnvelope->send(player, key, score, count, targets);
    }

    ll::Expected<void> WalletPlugin::sweepExpiredEnvelopes() {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->mRedEnvelope->sweepExpired();
    }

    ll::Expected<std::vector<std::string>> WalletPlugin::getEnvelopeStats(const std::string& id) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->mRedEnvelope->getEnvelopeStats(id);
    }

    ll::Expected<std::vector<std::string>> WalletPlugin::getRedEnvelopeDailyStats() {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->mLedger->getRedEnvelopeDailyStats();
    }

    int WalletPlugin::computeGiftAmount(int remainingCapacity, int remainingPeople) {
        return WalletRedEnvelope::computeGiftAmount(remainingCapacity, remainingPeople);
    }

    ll::Expected<void> WalletPlugin::bankDeposit(Player& player, int amount) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->mBank->deposit(player, amount);
    }

    ll::Expected<void> WalletPlugin::bankWithdraw(Player& player) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->mBank->withdraw(player);
    }

    ll::Expected<long long> WalletPlugin::getBankPrincipal(const std::string& uuid) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->mBank->getPrincipal(uuid);
    }

    ll::Expected<long long> WalletPlugin::getBankInterest(const std::string& uuid) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->mBank->getInterest(uuid);
    }

    ll::Expected<std::vector<std::pair<std::string, long long>>> WalletPlugin::getWealthRanking(int limit) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->mBank->getWealthRanking(limit);
    }

    ll::Expected<std::pair<int, long long>> WalletPlugin::getWealthRank(const std::string& uuid) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->mBank->getWealthRank(uuid);
    }

    ll::Expected<void> WalletPlugin::rebuildWealthRanking() {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->mBank->rebuildWealthRanking();
    }

    ll::Expected<long long> WalletPlugin::getFeePool() {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->mLedger->getFeePool();
    }

    ll::Expected<std::vector<std::string>> WalletPlugin::getPlayerLedger(const std::string& uuid, int limit) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->mLedger->getPlayerLedger(uuid, limit);
    }

    ll::Expected<void> WalletPlugin::sendHistory(Player& receiver, const std::string& uuid, const std::string& name, int limit) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        return this->mImpl->mLedger->sendHistory(receiver, uuid, name, limit);
    }

    ll::Expected<void> WalletPlugin::setExecutor(const ll::coro::Executor& executor) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        this->mImpl->mTimerManager->setExecutor(executor);

        return {};
    }

    std::string WalletPlugin::getTargetScoreboard() {
        return this->mImpl->options.TargetScoreboard;
    }

    double WalletPlugin::getExchangeRate() {
        return this->mImpl->options.ExchangeRate;
    }

    void WalletPlugin::setOptionsForTest(const Config::C_Wallet& options) {
        this->mImpl->options = options;
    }

    void WalletPlugin::registeryCommand() {
        ll::command::CommandHandle& command = ll::command::CommandRegistrar::getInstance(false)
            .getOrCreateCommand("wallet", tr({}, "commands.wallet.description"), CommandPermissionLevel::Any, CommandFlagValue::NotCheat | CommandFlagValue::Async);
        command.overload<operation>().text("transfer").required("Target").required("Score").execute(
            [this](CommandOrigin const& origin, CommandOutput& output, operation const& param) -> void {
                Actor* entity = origin.getEntity();
                if (entity == nullptr || !entity->isType(ActorType::Player))
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
                Player& player = *static_cast<Player*>(entity);

                CommandSelectorResults<Player> results = param.Target.results(origin);
                if (results.empty())
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));

                std::string mScoreboard = this->mImpl->options.TargetScoreboard;

                if (this->mImpl->options.TransferMinAmount > 0 && param.Score < this->mImpl->options.TransferMinAmount)
                    return output.error(fmt::runtime(tr(origin.getLocaleCode(), "wallet.limit.min")), param.Score, this->mImpl->options.TransferMinAmount);

                if (this->mImpl->options.TransferConfirmThreshold > 0 && param.Score > this->mImpl->options.TransferConfirmThreshold)
                    return output.error(tr(origin.getLocaleCode(), "wallet.limit.confirm"));

                int mMoney = param.Score * static_cast<int>(results.size());
                if (this->mImpl->options.TransferDailyLimit > 0 || this->mImpl->options.TransferCooldownSeconds > 0) {
                    if (auto verification = this->validateTransfer(player.getUuid().asString(), mMoney); !verification.has_value())
                        return output.error(verification.error().message(origin.getLocaleCode()));
                }

                if (ScoreboardUtils::getScore(player, mScoreboard) < mMoney || param.Score < 0)
                    return output.error(tr(origin.getLocaleCode(), "commands.wallet.error.score"));

                ScoreboardUtils::reduceScore(player, mScoreboard, mMoney);

                int mTargetMoney = static_cast<int>(param.Score * (1 - this->mImpl->options.ExchangeRate));
                for (Player*& target : results) {
                    ScoreboardUtils::addScore(*target, mScoreboard, mTargetMoney);

                    long long perTargetFee = static_cast<long long>(param.Score) - mTargetMoney;
                    this->mImpl->mLedger->record(player.getUuid().asString(), player.getRealName(), target->getUuid().asString(), target->getRealName(), mTargetMoney, perTargetFee, "transfer");
                }

                long long fee = static_cast<long long>(mMoney) - static_cast<long long>(mTargetMoney) * results.size();
                if (fee > 0)
                    this->mImpl->mLedger->accumulateFee(fee).or_else(modules::defaultErrorHandler<WalletPlugin>);

                this->updateTransferCooldown(player.getUuid().asString());

                output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.wallet.success.transfer")), param.Score, results.size());
            });
        command.overload().text("gui").execute([](CommandOrigin const& origin, CommandOutput& output) -> void {
            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);

            form::GUIManager::getInstance().open("wallet", "wallet.open", form::GUIManagerType::CustomForm, player)
                .or_else(modules::defaultErrorHandler<WalletPlugin>);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
        command.overload().text("wealth").execute([this](CommandOrigin const& origin, CommandOutput& output) -> void {
            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);

            this->wealth(player).or_else(modules::defaultErrorHandler<WalletPlugin>);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
        command.overload().text("history").execute([this](CommandOrigin const& origin, CommandOutput& output) -> void {
            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);

            if (!this->mImpl->options.WalletHistoryEnabled)
                return output.error(tr(origin.getLocaleCode(), "wallet.history.disabled"));

            this->sendHistory(player, player.getUuid().asString(), player.getRealName(), 20)
                .or_else(modules::defaultErrorHandler<WalletPlugin>);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
        command.overload().text("bank").execute([this](CommandOrigin const& origin, CommandOutput& output) -> void {
            if (!this->mImpl->options.WalletBankEnabled)
                return output.error(tr(origin.getLocaleCode(), "wallet.bank.disabled"));

            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);

            form::GUIManager::getInstance().open("wallet", "wallet.bank", form::GUIManagerType::CustomForm, player)
                .or_else(modules::defaultErrorHandler<WalletPlugin>);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
        command.overload().text("rank").execute([](CommandOrigin const& origin, CommandOutput& output) -> void {
            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);

            form::GUIManager::getInstance().open("wallet", "wallet.rank", form::GUIManagerType::PaginatedForm, player)
                .or_else(modules::defaultErrorHandler<WalletPlugin>);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
        command.overload<operationQuery>().text("query").required("PlayerName").execute([this](CommandOrigin const& origin, CommandOutput& output, operationQuery const& param) -> void {
            if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors)
                return output.error(tr(origin.getLocaleCode(), "commands.generic.permission"));

            if (!this->mImpl->options.WalletHistoryEnabled)
                return output.error(tr(origin.getLocaleCode(), "wallet.history.disabled"));

            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& receiver = *static_cast<Player*>(entity);

            std::string name = param.PlayerName;

            std::string uuid;
            if (Player* candidate = ll::service::getLevel()->getPlayer(name); candidate)
                uuid = candidate->getUuid().asString();

            if (uuid.empty()) {
                auto result = this->getPlayerInfo();
                if (!result.has_value())
                    return output.error(tr(origin.getLocaleCode(), "commands.generic.unknown"));
                for (const auto& [id, playerName] : result.value())
                    if (playerName == name) {
                        uuid = id;
                        break;
                    }
            }

            if (uuid.empty())
                return output.error(fmt::runtime(tr(origin.getLocaleCode(), "wallet.query.notfound")), name);

            this->sendHistory(receiver, uuid, name, 50).or_else(modules::defaultErrorHandler<WalletPlugin>);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), name);
        });
        command.overload<operationQueryId>().text("rinfo").required("EnvelopeId").execute([this](CommandOrigin const& origin, CommandOutput& output, operationQueryId const& param) -> void {
            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);

            auto lines = this->getEnvelopeStats(param.EnvelopeId);
            if (!lines.has_value())
                return output.error(tr(origin.getLocaleCode(), "commands.generic.unknown"));

            if (lines.value().empty())
                return output.error(fmt::runtime(tr(origin.getLocaleCode(), "wallet.rinfo.notfound")), param.EnvelopeId);

            for (const auto& line : lines.value())
                player.sendMessage(line);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
        command.overload().text("rstat").execute([this](CommandOrigin const& origin, CommandOutput& output) -> void {
            Actor* entity = origin.getEntity();
            if (entity == nullptr || !entity->isType(ActorType::Player))
                return output.error(tr(origin.getLocaleCode(), "commands.generic.target"));
            Player& player = *static_cast<Player*>(entity);

            auto lines = this->getRedEnvelopeDailyStats();
            if (!lines.has_value())
                return output.error(tr(origin.getLocaleCode(), "commands.generic.unknown"));

            for (const auto& line : lines.value())
                player.sendMessage(line);

            output.success(fmt::runtime(tr(origin.getLocaleCode(), "commands.generic.ui")), player.getRealName());
        });
        command.overload().text("reload").execute([this](CommandOrigin const& origin, CommandOutput& output) -> void {
            if (origin.getPermissionsLevel() < CommandPermissionLevel::GameDirectors)
                return output.error(tr(origin.getLocaleCode(), "commands.generic.permission"));

            output.success(tr(origin.getLocaleCode(), "commands.generic.reload"));

            form::GUIManager::getInstance().load("wallet", this->mImpl->mGuiPath)
                .transform([&origin, &output]() -> void {
                    output.success(tr(origin.getLocaleCode(), "commands.generic.reload.success"));
                })
                .or_else(modules::defaultErrorHandler<WalletPlugin>);
        });
    }

    ll::Expected<void> WalletPlugin::registeryUI() {
        return form::GUIManager::getInstance().load("wallet", this->mImpl->mGuiPath)
            .and_then([this]() -> ll::Expected<void> {
                return this->mImpl->mGui->registerAll(*this);
            });
    }

    void WalletPlugin::listenEvent() {
        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        this->mImpl->PlayerJoinEventListener = eventBus.emplaceListener<ll::event::PlayerJoinEvent>([this](ll::event::PlayerJoinEvent& event) mutable -> void {
            if (event.self().isSimulatedPlayer())
                return;

            std::string uuid = event.self().getUuid().asString();

            this->mImpl->db->has("Wallet", uuid)
                .and_then([this, uuid, name = event.self().getRealName()](bool exists) -> ll::Expected<void> {
                    if (!exists) {
                        std::unordered_map<std::string, std::string> data = {
                            { "name", name },
                            { "score", "0" },
                            { "balance", "0" }
                        };

                        return this->mImpl->db->set("Wallet", uuid, data);
                    }

                    return {};
                })
                .or_else(modules::defaultErrorHandler<WalletPlugin>);

            this->mImpl->db->get("Wallet", uuid, "score", "0")
                .and_then([this, uuid, &event](const std::string& value) -> ll::Expected<void> {
                    int score = SystemUtils::toInt(value, 0);
                    if (score <= 0) {
                        this->updateBalanceSnapshot(uuid, ScoreboardUtils::getScore(event.self(), this->mImpl->options.TargetScoreboard))
                            .or_else(modules::defaultErrorHandler<WalletPlugin>);

                        return {};
                    }

                    if (this->mImpl->mSettling.contains(uuid))
                        return {};

                    this->mImpl->mSettling[uuid] = true;
                    auto guard = make_scope_guard([this, uuid]() -> void {
                        this->mImpl->mSettling.erase(uuid);
                    });

                    return this->mImpl->db->set("Wallet", uuid, "score", "0")
                        .transform([this, uuid, score, &event]() -> void {
                            ScoreboardUtils::addScore(event.self(), this->mImpl->options.TargetScoreboard, score);

                            this->updateBalanceSnapshot(uuid, ScoreboardUtils::getScore(event.self(), this->mImpl->options.TargetScoreboard))
                                .or_else(modules::defaultErrorHandler<WalletPlugin>);
                        });
                })
                .or_else(modules::defaultErrorHandler<WalletPlugin>);
        });
        this->mImpl->PlayerChatEventListener = eventBus.emplaceListener<ll::event::PlayerChatEvent>([this](ll::event::PlayerChatEvent& event) mutable -> void {
            if (event.self().isSimulatedPlayer())
                return;

            this->tryGrabRedEnvelope(event.self(), event.message())
                .or_else([&event](ll::Error e) -> ll::Expected<void> {
                    if (e.isA<ll::ErrorCodeError>() && e.as<ll::ErrorCodeError>().ec == makeErrorCode(WalletPluginErrorCode::NotInTargetList)) {
                        return LanguagePlugin::getShared()->getLanguage(event.self())
                            .and_then([&event](const std::string& language) -> ll::Expected<void> {
                                event.self().sendMessage(tr(language, "wallet.redenvelope.not.target"));

                                return {};
                            });
                    }

                    return ll::Unexpected(e);
                })
                .or_else(modules::defaultErrorHandler<WalletPlugin>);
        }, ll::event::EventPriority::High);
        this->mImpl->PlayerDisconnectEventListener = eventBus.emplaceListener<ll::event::PlayerDisconnectEvent>([this](ll::event::PlayerDisconnectEvent& event) mutable -> void {
            if (event.self().isSimulatedPlayer())
                return;

            this->updateBalanceSnapshot(event.self().getUuid().asString(), ScoreboardUtils::getScore(event.self(), this->mImpl->options.TargetScoreboard))
                .or_else(modules::defaultErrorHandler<WalletPlugin>);
        });
        this->mImpl->WalletTransferEventListener = eventBus.emplaceListener<LOICollection::server::Events::WalletTransferEvent>([this](LOICollection::server::Events::WalletTransferEvent& event) mutable -> void {
            this->mImpl->logger->info(fmt::runtime(tr({}, "wallet.event.transfer")),
                event.getType(), event.getFromName(), event.getToName(), event.getAmount(), event.getFee());
        });
        this->mImpl->RedEnvelopeCompletedEventListener = eventBus.emplaceListener<LOICollection::server::Events::RedEnvelopeCompletedEvent>([this](LOICollection::server::Events::RedEnvelopeCompletedEvent& event) mutable -> void {
            this->mImpl->logger->info(fmt::runtime(tr({}, "wallet.event.envelope")),
                event.getEnvelopeId(), event.getKingName(), event.getKingAmount(), event.getTotal());
        });
    }

    void WalletPlugin::unlistenEvent() {
        ll::event::EventBus& eventBus = ll::event::EventBus::getInstance();
        eventBus.removeListener(this->mImpl->PlayerJoinEventListener);
        eventBus.removeListener(this->mImpl->PlayerChatEventListener);
        eventBus.removeListener(this->mImpl->PlayerDisconnectEventListener);
        eventBus.removeListener(this->mImpl->WalletTransferEventListener);
        eventBus.removeListener(this->mImpl->RedEnvelopeCompletedEventListener);

        this->mImpl->mTimerManager->cancelAll();
    }

    std::string WalletPlugin::getName() {
        return "WalletPlugin";
    }

    modules::ModulePriority WalletPlugin::getPriority() {
        return modules::ModulePriority::High;
    }

    ll::Expected<bool> WalletPlugin::load() {
        if (!ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Wallet.ModuleEnabled)
            return false;

        this->mImpl->db = ServiceProvider::getInstance().getService<SQLiteStorage>("SettingsDB");
        this->mImpl->logger = ll::io::LoggerRegistry::getInstance().getOrCreate("LOICollectionA");
        this->mImpl->options = ServiceProvider::getInstance().getService<ReadOnlyWrapper<Config::C_Config>>("Config")->get().ServerConfig.Plugins.Wallet;
        this->mImpl->mGuiPath = (std::filesystem::path(ServiceProvider::getInstance().getService<std::string>("GuiPath")->data()) / "wallet.lcui").string();

        this->mImpl->mLedger = std::make_unique<WalletLedger>(
            this->mImpl->db,
            this->mImpl->options,
            this->mImpl->logger,
            *this->mImpl->mTimerManager
        );
        this->mImpl->mRedEnvelope = std::make_unique<WalletRedEnvelope>(
            this->mImpl->db,
            this->mImpl->options,
            this->mImpl->logger,
            *this->mImpl->mTimerManager,
            *this->mImpl->mLedger,
            [this](const std::string& target, int score) -> ll::Expected<void> {
                return this->transfer(target, score);
            }
        );
        this->mImpl->mBank = std::make_unique<WalletBank>(
            this->mImpl->db,
            this->mImpl->options,
            this->mImpl->logger,
            *this->mImpl->mTimerManager,
            *this->mImpl->mLedger
        );
        this->mImpl->mGui = std::make_unique<WalletGui>();

        return true;
    }

    ll::Expected<bool> WalletPlugin::unload() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        this->mImpl->mLedger.reset();
        this->mImpl->mRedEnvelope.reset();
        this->mImpl->mBank.reset();
        this->mImpl->mGui.reset();

        this->mImpl->db.reset();
        this->mImpl->logger.reset();
        this->mImpl->options = {};

        if (this->mImpl->mRegistered.load(std::memory_order_acquire))
            this->unlistenEvent();

        return true;
    }

    ll::Expected<bool> WalletPlugin::registry() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        return this->mImpl->db->create("Wallet", [](SQLiteStorage::ColumnCallback ctor) -> void {
            ctor("name");
            ctor("score");
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->mLedger->createTables();
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->mRedEnvelope->createTables();
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->mBank->createTables();
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->mRedEnvelope->sweepExpired();
        }).and_then([this]() -> ll::Expected<void> {
            return this->registeryUI();
        }).transform([this]() -> bool {
            this->registeryCommand();
            this->listenEvent();

            this->mImpl->mLedger->startCleanupSchedule();

            this->mImpl->mBank->rebuildWealthRanking().or_else(modules::defaultErrorHandler<WalletPlugin>);
            this->mImpl->mBank->startWealthRefresh();

            this->mImpl->mRegistered.store(true, std::memory_order_release);

            return true;
        });
    }

    ll::Expected<bool> WalletPlugin::unregistry() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        this->unlistenEvent();

        return this->mImpl->mRedEnvelope->refundAll()
            .transform([this]() -> bool {
                this->mImpl->mRegistered.store(false, std::memory_order_release);

                return true;
            });
    }
}
