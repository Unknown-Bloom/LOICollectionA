#pragma once

#include "LOICollectionA/data/sqlite/block/TypedTable.h"

namespace LOICollection::server::Plugins {

    using LOICollection::data::TypedTable;

    enum class WalletCol { name, score, balance };
    using WalletTable = TypedTable<WalletCol, 1>;

    enum class WalletFeeCol { amount };
    using WalletFeeTable = TypedTable<WalletFeeCol, 1>;

    enum class WalletBankCol { principal, deposit_at, name };
    using WalletBankTable = TypedTable<WalletBankCol, 1>;

    enum class WalletLedgerCol {
        from_uuid, from_name, to_uuid, to_name,
        amount, fee, type, time_ns, time
    };
    using WalletLedgerTable = TypedTable<WalletLedgerCol, 1>;

    enum class RedEnvelopeCol {
        chat_key, sender_uuid, sender_name,
        capacity, total, count, people, targets, expire_at
    };
    using RedEnvelopeTable = TypedTable<RedEnvelopeCol, 1>;

    enum class RedEnvelopeGrabCol { name, amount };
    using RedEnvelopeGrabTable = TypedTable<RedEnvelopeGrabCol, 1>;

}
