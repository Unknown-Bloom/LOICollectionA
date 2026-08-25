#include "LOICollectionA/include/server/Plugins/wallet/WalletDetail.h"

namespace LOICollection::server::Plugins {
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
                event.getId(), event.getKingName(), event.getKingAmount(), event.getTotal());
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
            .transform([this, name, score, mTargetMoney, uuid = player.getUuid().asString(), playerName = player.getRealName()]() -> bool {
                this->getLogger()->info(fmt::runtime(tr({}, "wallet.log")), playerName, name, score);

                this->updateTransferCooldown(uuid);

                long long fee = static_cast<long long>(score) - mTargetMoney;
                if (fee > 0)
                    this->accumulateFee(fee).or_else(modules::defaultErrorHandler<WalletPlugin>);

                this->appendLedger(uuid, playerName, target, name, mTargetMoney, fee, "transfer")
                    .or_else(modules::defaultErrorHandler<WalletPlugin>);

                this->emitWalletTransfer(uuid, playerName, target, name, mTargetMoney, fee, "transfer");

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
            long long today = this->getTodayOutgoing(uuid);
            if (today + spend > options.TransferDailyLimit)
                return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::DailyLimitExceeded));
        }

        return {};
    }

    long long WalletPlugin::getTodayOutgoing(const std::string& uuid) {
        constexpr long long NS_PER_DAY = 86400LL * 1000000000LL;

        auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        long long todayStartNs = (nowNs / NS_PER_DAY) * NS_PER_DAY;

        auto ids = this->mImpl->db->find("WalletLedger", std::vector<std::pair<std::string, std::string>>{ { "from_uuid", uuid } });
        if (!ids.has_value())
            return 0;

        long long total = 0;
        for (const auto& id : ids.value()) {
            auto row = this->mImpl->db->get("WalletLedger", id);
            if (!row.has_value())
                continue;

            const auto& fields = row.value();
            if (!fields.contains("type") || fields.at("type") != "transfer")
                continue;
            if (!fields.contains("time_ns") || SystemUtils::toLongLong(fields.at("time_ns"), 0) < todayStartNs)
                continue;

            total += SystemUtils::toLongLong(fields.at("amount"), 0) + SystemUtils::toLongLong(fields.at("fee"), 0);
        }

        return total;
    }

    void WalletPlugin::updateTransferCooldown(const std::string& uuid) {
        if (this->mImpl->options.TransferCooldownSeconds <= 0)
            return;

        auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        this->mImpl->mLastTransferTime[uuid] = now;
    }

    ll::Expected<void> WalletPlugin::updateBalanceSnapshot(const std::string& uuid, long long balance) {
        if (!this->isValid() || uuid.empty())
            return {};

        return this->mImpl->db->set("Wallet", uuid, "balance", std::to_string(balance));
    }

    ll::Expected<void> WalletPlugin::setExecutor(const ll::coro::Executor& executor) {
        if (!this->isValid())
            return ll::makeErrorCodeError(makeErrorCode(WalletPluginErrorCode::Invalid));

        this->mImpl->mTimerManager->setExecutor(executor);

        return {};
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

    bool WalletPlugin::isValid() {
        return this->getLogger() != nullptr && this->mImpl->db != nullptr;
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

        return true;
    }

    ll::Expected<bool> WalletPlugin::unload() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

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
            return this->mImpl->db->create("RedEnvelope", [](SQLiteStorage::ColumnCallback ctor) -> void {
                ctor("chat_key");
                ctor("sender_uuid");
                ctor("sender_name");
                ctor("capacity");
                ctor("count");
                ctor("people");
                ctor("created_at");
                ctor("expire_at");
            });
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->db->create("RedEnvelopeGrab", [](SQLiteStorage::ColumnCallback ctor) -> void {
                ctor("name");
                ctor("amount");
            });
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->db->create("WalletFee", [](SQLiteStorage::ColumnCallback ctor) -> void {
                ctor("amount");
            });
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->db->create("WalletLedger", [](SQLiteStorage::ColumnCallback ctor) -> void {
                ctor("from_uuid");
                ctor("from_name");
                ctor("to_uuid");
                ctor("to_name");
                ctor("amount");
                ctor("fee");
                ctor("type");
                ctor("time_ns");
                ctor("time");
            });
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->db->create("WalletBank", [](SQLiteStorage::ColumnCallback ctor) -> void {
                ctor("principal");
                ctor("deposit_at");
                ctor("name");
            });
        }).and_then([this]() -> ll::Expected<void> {
            return this->mImpl->db->exec("CREATE INDEX IF NOT EXISTS idx_WalletLedger_time_ns ON WalletLedger(time_ns);")
                .and_then([this]() -> ll::Expected<void> {
                    return this->mImpl->db->exec("CREATE INDEX IF NOT EXISTS idx_WalletLedger_from_uuid ON WalletLedger(from_uuid);");
                })
                .and_then([this]() -> ll::Expected<void> {
                    return this->mImpl->db->exec("CREATE INDEX IF NOT EXISTS idx_WalletLedger_to_uuid ON WalletLedger(to_uuid);");
                });
        }).and_then([this]() -> ll::Expected<void> {
            return this->sweepExpiredEnvelopes();
        }).and_then([this]() -> ll::Expected<void> {
            return this->registeryUI();
        }).transform([this]() -> bool {
            this->registeryCommand();
            this->listenEvent();

            if (this->mImpl->options.WalletHistoryRetentionDays > 0)
                this->scheduleLedgerCleanup();

            this->rebuildWealthRanking().or_else(modules::defaultErrorHandler<WalletPlugin>);
            this->scheduleWealthRefresh();

            this->mImpl->mRegistered.store(true, std::memory_order_release);

            return true;
        });
    }

    ll::Expected<bool> WalletPlugin::unregistry() {
        if (!this->mImpl->options.ModuleEnabled)
            return false;

        this->unlistenEvent();

        return this->mImpl->db->list("RedEnvelope")
            .and_then([this](const std::vector<std::string>& ids) -> ll::Expected<void> {
                for (const auto& id : ids) {
                    auto result = this->refundEnvelope(id);
                    if (!result.has_value())
                        return ll::Unexpected(result.error());
                }

                return {};
            })
            .transform([this]() -> bool {
                this->mImpl->mRedEnvelopes.clear();

                this->mImpl->mRegistered.store(false, std::memory_order_release);

                return true;
            });
    }

    void WalletPlugin::emitWalletTransfer(const std::string& fromUuid, const std::string& fromName, const std::string& toUuid, const std::string& toName, long long amount, long long fee, const std::string& type) {
        ll::event::EventBus::getInstance().publish(LOICollection::server::Events::WalletTransferEvent(
            fromUuid,
            fromName,
            toUuid,
            toName,
            amount,
            fee,
            type,
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        ));
    }

}
