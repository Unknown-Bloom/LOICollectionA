#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/include/ModuleBase.h"
#include "LOICollectionA/include/ModManager.h"

class Player;

namespace Config {
    struct C_Wallet;
}

namespace ll {
    namespace io {
        class Logger;
    }

    namespace coro {
        class Executor;
    }
}

namespace LOICollection::server::Plugins {
    enum class WalletPluginErrorCode : int {
        Invalid = 1,
        NotFound = 2,
        RedEnvelopeCompleted = 3,
        BelowMinimum = 4,
        DailyLimitExceeded = 5,
        CooldownActive = 6,
        ConfirmRequired = 7,
        BankEmpty = 8,
        BelowMinDeposit = 9,
        RedEnvelopeCountExceeded = 10,
        NotInTargetList = 11
    };

    struct WalletPluginErrorCategory : std::error_category {
        [[nodiscard]] const char* name() const noexcept override {
            return "WalletPluginError";
        }

        [[nodiscard]] std::string message(int ev) const override {
            switch (static_cast<WalletPluginErrorCode>(ev)) {
                case WalletPluginErrorCode::Invalid: return "Plugin is invalid";
                case WalletPluginErrorCode::NotFound: return "Red envelope data not found";
                case WalletPluginErrorCode::RedEnvelopeCompleted: return "Red envelope already completed";
                case WalletPluginErrorCode::BelowMinimum: return "Transfer amount below minimum";
                case WalletPluginErrorCode::DailyLimitExceeded: return "Daily transfer limit exceeded";
                case WalletPluginErrorCode::CooldownActive: return "Transfer cooldown is active";
                case WalletPluginErrorCode::ConfirmRequired: return "Large transfer requires confirmation";
                case WalletPluginErrorCode::BankEmpty: return "No bank deposit found";
                case WalletPluginErrorCode::BelowMinDeposit: return "Deposit below the minimum amount";
                case WalletPluginErrorCode::RedEnvelopeCountExceeded: return "Red envelope share count exceeds the limit";
                case WalletPluginErrorCode::NotInTargetList: return "Player is not in the red envelope target list";
                default:
                    return "Unknown";
            }
        }
    };

    class WalletPlugin : public std::enable_shared_from_this<WalletPlugin>,
                         public modules::ModuleBase,
                         public modules::AutoRegister<WalletPlugin> {
    public:
        ~WalletPlugin();

        WalletPlugin(WalletPlugin const&) = delete;
        WalletPlugin(WalletPlugin&&) = delete;
        WalletPlugin& operator=(WalletPlugin const&) = delete;
        WalletPlugin& operator=(WalletPlugin&&) = delete;

    public:
        LOICOLLECTION_A_NDAPI static std::shared_ptr<WalletPlugin> getShared();
        LOICOLLECTION_A_NDAPI static std::error_code makeErrorCode(WalletPluginErrorCode e);

        LOICOLLECTION_A_NDAPI std::shared_ptr<ll::io::Logger> getLogger();

        LOICOLLECTION_A_NDAPI const Config::C_Wallet& getOptions() const;

        LOICOLLECTION_A_NDAPI bool isValid();

    public:
        LOICOLLECTION_A_NDAPI ll::Expected<std::string> getPlayerInfo(const std::string& uuid);

        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::pair<std::string, std::string>>> getPlayerInfo();

        LOICOLLECTION_A_NDAPI ll::Expected<void> updateBalanceSnapshot(const std::string& uuid, long long balance);

    public:
        LOICOLLECTION_A_NDAPI ll::Expected<bool> forTransfer(Player& player, const std::string& target, const std::string& name, int score, bool confirmed = false);

        LOICOLLECTION_A_NDAPI ll::Expected<void> transfer(const std::string& target, int score);

        LOICOLLECTION_A_NDAPI ll::Expected<void> wealth(Player& player);

    public:
        LOICOLLECTION_A_NDAPI ll::Expected<void> tryGrabRedEnvelope(Player& player, const std::string& message);

        LOICOLLECTION_A_NDAPI ll::Expected<void> redenvelope(Player& player, const std::string& key, int score, int count, const std::vector<std::string>& targets = {});

        LOICOLLECTION_A_NDAPI ll::Expected<void> sweepExpiredEnvelopes();

        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getEnvelopeStats(const std::string& id);

        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getRedEnvelopeDailyStats();

        LOICOLLECTION_A_NDAPI static int computeGiftAmount(int remainingCapacity, int remainingPeople);

    public:
        LOICOLLECTION_A_NDAPI ll::Expected<void> bankDeposit(Player& player, int amount);

        LOICOLLECTION_A_NDAPI ll::Expected<void> bankWithdraw(Player& player);

        LOICOLLECTION_A_NDAPI ll::Expected<long long> getBankPrincipal(const std::string& uuid);

        LOICOLLECTION_A_NDAPI ll::Expected<long long> getBankInterest(const std::string& uuid);

    public:
        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::pair<std::string, long long>>> getWealthRanking(int limit);

        LOICOLLECTION_A_NDAPI ll::Expected<std::pair<int, long long>> getWealthRank(const std::string& uuid);

        LOICOLLECTION_A_NDAPI ll::Expected<void> rebuildWealthRanking();

    public:
        LOICOLLECTION_A_NDAPI ll::Expected<long long> getFeePool();

        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::string>> getPlayerLedger(const std::string& uuid, int limit);

        LOICOLLECTION_A_NDAPI ll::Expected<void> sendHistory(Player& receiver, const std::string& uuid, const std::string& name, int limit);

    public:
        LOICOLLECTION_A_NDAPI ll::Expected<void> setExecutor(const ll::coro::Executor& executor);

    public:
        LOICOLLECTION_A_NDAPI std::string getTargetScoreboard();

        LOICOLLECTION_A_NDAPI double getExchangeRate();

        LOICOLLECTION_A_API   void setOptionsForTest(const Config::C_Wallet& options);

    public:
        LOICOLLECTION_A_NDAPI std::string getName() override;

        LOICOLLECTION_A_NDAPI modules::ModulePriority getPriority() override;

        LOICOLLECTION_A_API   ll::Expected<bool> load() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unload() override;
        LOICOLLECTION_A_API   ll::Expected<bool> registry() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unregistry() override;

    private:
        struct operation;

        struct operationQuery;

        struct operationQueryId;

        struct Impl;

        WalletPlugin();

        ll::Expected<void> registeryUI();

        void registeryCommand();
        void listenEvent();
        void unlistenEvent();

        ll::Expected<void> validateTransfer(const std::string& uuid, int spend);
        void updateTransferCooldown(const std::string& uuid);

        std::unique_ptr<Impl> mImpl;
    };
}
