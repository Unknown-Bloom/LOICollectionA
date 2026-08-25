#pragma once

#include <string>
#include <system_error>

#include "LOICollectionA/utils/I18nUtils.h"

#include "LOICollectionA/include/server/Plugins/wallet/WalletPlugin.h"

namespace LOICollection::server::Plugins::walletGui {
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
}
