#pragma once

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"

namespace LOICollection::server::Plugins {
    class WalletPlugin;

    class WalletGui {
    public:
        WalletGui() = default;
        ~WalletGui() = default;

        WalletGui(WalletGui const&) = delete;
        WalletGui(WalletGui&&) = delete;
        WalletGui& operator=(WalletGui const&) = delete;
        WalletGui& operator=(WalletGui&&) = delete;

    public:
        LOICOLLECTION_A_NDAPI ll::Expected<void> registerAll(WalletPlugin& owner);

    private:
        void registerInfo(WalletPlugin& owner);
        void registerTransfer(WalletPlugin& owner);
        void registerRedEnvelope(WalletPlugin& owner);
        void registerRank(WalletPlugin& owner);
        void registerBank(WalletPlugin& owner);
    };
}
