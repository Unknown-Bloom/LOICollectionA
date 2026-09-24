#pragma once

#include "LOICollectionA/data/sqlite/block/TypedTable.h"

namespace LOICollection::server::Plugins {

    using LOICollection::data::TypedTable;

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
