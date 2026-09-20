#pragma once

#include "LOICollectionA/data/sqlite/block/TypedTable.h"

namespace LOICollection::server::Plugins {

    using LOICollection::data::TypedTable;

    enum class PvpCol { name, enable };
    using PvpTable = TypedTable<PvpCol, 1>;

    enum class LanguageCol { name, value };
    using LanguageTable = TypedTable<LanguageCol, 1>;

    enum class NoticeCol { name, close };
    using NoticeTable = TypedTable<NoticeCol, 1>;

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

    enum class ChatCol { name, title };
    using ChatTable = TypedTable<ChatCol, 1>;

    enum class TitleCol { title, author, time };
    using TitleTable = TypedTable<TitleCol, 1>;

    enum class ChatBlacklistCol { name, target, author, time };
    using ChatBlacklistTable = TypedTable<ChatBlacklistCol, 1>;

    enum class TpaCol { name, invite };
    using TpaTable = TypedTable<TpaCol, 1>;

    enum class TpaBlacklistCol { name, target, author, time };
    using TpaBlacklistTable = TypedTable<TpaBlacklistCol, 1>;

    enum class BlacklistCol { name, cause, time, subtime, data_uuid, data_ip, data_clientid };
    using BlacklistTable = TypedTable<BlacklistCol, 1>;

    enum class MuteCol { name, cause, time, subtime, data };
    using MuteTable = TypedTable<MuteCol, 1>;

    enum class StatisticsCol { onlinetime, kill, death, place, destroy, respawn, joins };
    using StatisticsTable = TypedTable<StatisticsCol, 1>;

    enum class MarketCol { name, score };
    using MarketTable = TypedTable<MarketCol, 1>;

    enum class MarketTaxCol { total, rate };
    using MarketTaxTable = TypedTable<MarketTaxCol, 1>;

    enum class ItemCol { name, icon, introduce, score, data, player_name, player_uuid };
    using ItemTable = TypedTable<ItemCol, 1>;

    enum class MarketBlacklistCol { name, target, author, time };
    using MarketBlacklistTable = TypedTable<MarketBlacklistCol, 1>;

    enum class StoreCol { name, introduce, icon, owner_uuid, owner_name, store_created_at };
    using StoreTable = TypedTable<StoreCol, 1>;

    enum class StoreItemCol { store_id, name, icon, introduce, score, data };
    using StoreItemTable = TypedTable<StoreItemCol, 1>;

    enum class StoreSaleCol {
        store_id, item_name, price, tax,
        buyer_uuid, buyer_name, seller_uuid, time, source
    };
    using StoreSaleTable = TypedTable<StoreSaleCol, 1>;

    enum class StoreReviewCol { store_id, buyer_uuid, buyer_name, rating, content, status, time };
    using StoreReviewTable = TypedTable<StoreReviewCol, 1>;

    enum class StoreWantedCol {
        wanted_uuid, wanted_name, item_type, item_data, item_name,
        unit_price, amount_total, amount_filled, expire_at
    };
    using StoreWantedTable = TypedTable<StoreWantedCol, 1>;

    enum class StoreAuctionCol {
        seller_uuid, seller_name, item_type, item_data, item_name,
        start_price, current_price, bidder_uuid, bidder_name,
        bid_count, end_at, settled
    };
    using StoreAuctionTable = TypedTable<StoreAuctionCol, 1>;

}
