#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/include/ModuleBase.h"
#include "LOICollectionA/include/ModManager.h"

class Player;

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
        RedEnvelopeCompleted = 3
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

        LOICOLLECTION_A_NDAPI ll::Expected<std::string> getPlayerInfo(const std::string& uuid);

        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::pair<std::string, std::string>>> getPlayerInfo();

        LOICOLLECTION_A_NDAPI ll::Expected<bool> forTransfer(Player& player, const std::string& target, const std::string& name, int score);

        LOICOLLECTION_A_NDAPI ll::Expected<void> setExecutor(const ll::coro::Executor& executor);

        LOICOLLECTION_A_NDAPI ll::Expected<void> tryGrabRedEnvelope(Player& player, const std::string& message);

        LOICOLLECTION_A_NDAPI ll::Expected<void> transfer(const std::string& target, int score);
        LOICOLLECTION_A_NDAPI ll::Expected<void> wealth(Player& player);
        LOICOLLECTION_A_NDAPI ll::Expected<void> redenvelope(Player& player, const std::string& key, int score, int count);

        LOICOLLECTION_A_NDAPI ll::Expected<long long> getFeePool();

        LOICOLLECTION_A_NDAPI ll::Expected<void> sweepExpiredEnvelopes();

        LOICOLLECTION_A_NDAPI bool isValid();

    public:
        LOICOLLECTION_A_NDAPI std::string getTargetScoreboard();

        LOICOLLECTION_A_NDAPI double getExchangeRate();

    public:
        LOICOLLECTION_A_NDAPI std::string getName() override;

        LOICOLLECTION_A_NDAPI modules::ModulePriority getPriority() override;

        LOICOLLECTION_A_API   ll::Expected<bool> load() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unload() override;
        LOICOLLECTION_A_API   ll::Expected<bool> registry() override;
        LOICOLLECTION_A_API   ll::Expected<bool> unregistry() override;

    private:
        WalletPlugin();

        ll::Expected<void> registeryUI();

        void registeryCommand();
        void listenEvent();
        void unlistenEvent();

        static int computeGiftAmount(int remainingCapacity, int remainingPeople);

        ll::Expected<bool> grabEnvelope(Player& player, const std::string& uuid, struct RedEnvelopeEntry& entry);
        void broadcastContent(Player& sender, const std::string& key, const std::string& id, int score, int count);
        void broadcastReceive(const struct RedEnvelopeEntry& entry, Player& player, int amount, int people);
        void announceKing(struct RedEnvelopeEntry& entry);
        ll::Expected<void> deleteEnvelope(const std::string& id);
        ll::Expected<bool> refundEnvelope(const std::string& id);
        ll::Expected<void> accumulateFee(long long amount);

        struct RedEnvelopeEntry;

        struct operation;

        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
