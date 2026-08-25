#pragma once

#include <atomic>
#include <cmath>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
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
#include <ll/api/event/player/PlayerChatEvent.h>
#include <ll/api/event/player/PlayerDisconnectEvent.h>
#include <ll/api/utils/RandomUtils.h>
#include <ll/api/utils/HashUtils.h>

#include <mc/deps/core/string/HashedString.h>

#include <mc/world/level/Level.h>
#include <mc/world/actor/ActorDefinitionIdentifier.h>
#include <mc/world/actor/player/Player.h>

#include <mc/network/packet/TextPacket.h>

#include <mc/server/commands/CommandOrigin.h>
#include <mc/server/commands/CommandOutput.h>
#include <mc/server/commands/CommandSelector.h>
#include <mc/server/commands/CommandPermissionLevel.h>
#include <mc/server/commands/CommandOutputMessageType.h>

#include "LOICollectionA/include/CallbackUtils.h"
#include "LOICollectionA/include/server/Plugins/LanguagePlugin.h"

#include "LOICollectionA/include/server/Events/modules/WalletTransferEvent.h"
#include "LOICollectionA/include/server/Events/modules/RedEnvelopeCompletedEvent.h"

#include "LOICollectionA/coro/TimerManager.h"

#include "LOICollectionA/utils/I18nUtils.h"
#include "LOICollectionA/utils/mc-server/ScoreboardUtils.h"
#include "LOICollectionA/utils/core/SystemUtils.h"

#include "LOICollectionA/data/SQLiteStorage.h"

#include "LOICollectionA/frontend/AST.h"

#include "LOICollectionA/include/form/GUIManager.h"

#include "LOICollectionA/base/ScopeGuard.h"
#include "LOICollectionA/base/Wrapper.h"
#include "LOICollectionA/base/ServiceProvider.h"

#include "LOICollectionA/ConfigPlugin.h"

#include "LOICollectionA/include/server/Plugins/WalletPlugin.h"

namespace LOICollection::server::Plugins {
    using I18nUtilsTools::tr;

    inline std::string walletLimitMessage(const std::string& locale, const std::error_code& error) {
        switch (static_cast<WalletPluginErrorCode>(error.value())) {
            case WalletPluginErrorCode::BelowMinimum: return tr(locale, "wallet.limit.min");
            case WalletPluginErrorCode::DailyLimitExceeded: return tr(locale, "wallet.limit.daily");
            case WalletPluginErrorCode::CooldownActive: return tr(locale, "wallet.limit.cooldown");
            case WalletPluginErrorCode::ConfirmRequired: return tr(locale, "wallet.limit.confirm");
            case WalletPluginErrorCode::BankEmpty: return tr(locale, "wallet.bank.empty");
            case WalletPluginErrorCode::BelowMinDeposit: return tr(locale, "wallet.bank.min.deposit");
            case WalletPluginErrorCode::RedEnvelopeCountExceeded: return tr(locale, "wallet.redenvelope.max.count");
            case WalletPluginErrorCode::NotInTargetList: return tr(locale, "wallet.redenvelope.not.target");
            default: return error.message();
        }
    }

    struct WalletPlugin::WealthEntry {
        std::string uuid;
        std::string name;
        long long balance;
    };

    struct WalletPlugin::RedEnvelopeEntry {
        std::string id;
        std::string chatKey;
        std::string senderUuid;
        std::string senderName;

        int count;
        long long expireAt;

        std::string kingUuid;
        std::string kingName;
        int kingAmount;

        int total;
    };

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

        ll::ConcurrentDenseMap<std::string, std::vector<RedEnvelopeEntry>> mRedEnvelopes;
        ll::ConcurrentDenseMap<std::string, bool> mSettling;
        ll::ConcurrentDenseMap<std::string, int64_t> mLastTransferTime;

        std::atomic<uint64_t> mLedgerSeq{ 0 };
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

        mutable std::mutex mRankMutex;
        std::vector<WealthEntry> mWealthRank;
        std::unordered_map<std::string, size_t> mRankOf;

        Impl() : mTimerManager(std::make_shared<TimerManager>(ll::thread::ServerThreadExecutor::getDefault())) {}
    };
}
