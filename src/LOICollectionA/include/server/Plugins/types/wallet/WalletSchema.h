#pragma once

#include "LOICollectionA/data/sqlite/block/TypedTable.h"

namespace LOICollection::server::Plugins {

    using LOICollection::data::TypedTable;

    enum class WalletCol { name, score, balance };
    enum class WalletFeeCol { amount };
    enum class WalletBankCol { principal, deposit_at, name };
    enum class WalletLedgerCol { from_uuid, from_name, to_uuid, to_name,
        amount, fee, type, time_ns, time };
    enum class RedEnvelopeCol { chat_key, sender_uuid, sender_name,
        capacity, total, count, people, targets, expire_at };
    enum class RedEnvelopeGrabCol { name, amount };

}

namespace LOICollection::data {

    template <>
    struct TypedColumn<LOICollection::server::Plugins::WalletCol> {
        using Types = std::tuple<std::string, long long, long long>;
    };
    template <>
    struct TypedColumn<LOICollection::server::Plugins::WalletFeeCol> {
        using Types = std::tuple<long long>;
    };
    template <>
    struct TypedColumn<LOICollection::server::Plugins::WalletBankCol> {
        using Types = std::tuple<long long, long long, std::string>;
    };
    template <>
    struct TypedColumn<LOICollection::server::Plugins::WalletLedgerCol> {
        using Types = std::tuple<
            std::string, std::string, std::string, std::string,
            long long, long long, std::string, long long, std::string>;
    };
    template <>
    struct TypedColumn<LOICollection::server::Plugins::RedEnvelopeCol> {
        using Types = std::tuple<
            std::string, std::string, std::string,
            long long, long long, long long, long long, std::string, long long>;
    };
    template <>
    struct TypedColumn<LOICollection::server::Plugins::RedEnvelopeGrabCol> {
        using Types = std::tuple<std::string, long long>;
    };

}

namespace LOICollection::server::Plugins {

    using WalletTable = TypedTable<WalletCol, 1>;
    using WalletFeeTable = TypedTable<WalletFeeCol, 1>;
    using WalletBankTable = TypedTable<WalletBankCol, 1>;
    using WalletLedgerTable = TypedTable<WalletLedgerCol, 1>;
    using RedEnvelopeTable = TypedTable<RedEnvelopeCol, 1>;
    using RedEnvelopeGrabTable = TypedTable<RedEnvelopeGrabCol, 1>;

}
