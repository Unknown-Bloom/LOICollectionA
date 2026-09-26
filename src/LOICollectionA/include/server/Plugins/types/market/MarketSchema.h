#pragma once

#include "LOICollectionA/data/sqlite/block/TypedTable.h"

namespace LOICollection::server::Plugins {

    using LOICollection::data::TypedTable;

    enum class MarketCol { name, score };
    enum class MarketTaxCol { total, rate };
    enum class ItemCol { name, icon, introduce, score, data, player_name, player_uuid };
    enum class MarketBlacklistCol { name, target, author, time };
    enum class StoreCol { name, introduce, icon, owner_uuid, owner_name, store_created_at };
    enum class StoreItemCol { store_id, name, icon, introduce, score, data };
    enum class StoreSaleCol { store_id, item_name, price, tax,
        buyer_uuid, buyer_name, seller_uuid, time, source };
    enum class StoreReviewCol { store_id, buyer_uuid, buyer_name, rating, content, status, time };
    enum class StoreWantedCol { wanted_uuid, wanted_name, item_type, item_data, item_name,
        unit_price, amount_total, amount_filled, expire_at };
    enum class StoreAuctionCol { seller_uuid, seller_name, item_type, item_data, item_name,
        start_price, current_price, bidder_uuid, bidder_name,
        bid_count, end_at, settled };

}

namespace LOICollection::data {

    template <>
    struct TypedColumn<LOICollection::server::Plugins::MarketCol> {
        using Types = std::tuple<std::string, long long>;
    };
    template <>
    struct TypedColumn<LOICollection::server::Plugins::MarketTaxCol> {
        using Types = std::tuple<std::string, double>;
    };
    template <>
    struct TypedColumn<LOICollection::server::Plugins::ItemCol> {
        using Types = std::tuple<
            std::string, std::string, std::string, long long,
            std::string, std::string, std::string>;
    };
    template <>
    struct TypedColumn<LOICollection::server::Plugins::MarketBlacklistCol> {
        using Types = std::tuple<std::string, std::string, std::string, std::string>;
    };
    template <>
    struct TypedColumn<LOICollection::server::Plugins::StoreCol> {
        using Types = std::tuple<
            std::string, std::string, std::string, std::string, std::string, std::string>;
    };
    template <>
    struct TypedColumn<LOICollection::server::Plugins::StoreItemCol> {
        using Types = std::tuple<
            std::string, std::string, std::string, std::string, long long, std::string>;
    };
    template <>
    struct TypedColumn<LOICollection::server::Plugins::StoreSaleCol> {
        using Types = std::tuple<
            std::string, std::string, long long, long long,
            std::string, std::string, std::string, std::string, std::string>;
    };
    template <>
    struct TypedColumn<LOICollection::server::Plugins::StoreReviewCol> {
        using Types = std::tuple<
            std::string, std::string, std::string, long long,
            std::string, std::string, std::string>;
    };
    template <>
    struct TypedColumn<LOICollection::server::Plugins::StoreWantedCol> {
        using Types = std::tuple<
            std::string, std::string, std::string, std::string, std::string,
            long long, long long, long long, long long>;
    };
    template <>
    struct TypedColumn<LOICollection::server::Plugins::StoreAuctionCol> {
        using Types = std::tuple<
            std::string, std::string, std::string, std::string, std::string,
            long long, long long, std::string, std::string,
            long long, long long, bool>;
    };

}

namespace LOICollection::server::Plugins {

    using MarketTable = TypedTable<MarketCol, 1>;
    using MarketTaxTable = TypedTable<MarketTaxCol, 1>;
    using ItemTable = TypedTable<ItemCol, 1>;
    using MarketBlacklistTable = TypedTable<MarketBlacklistCol, 1>;
    using StoreTable = TypedTable<StoreCol, 1>;
    using StoreItemTable = TypedTable<StoreItemCol, 1>;
    using StoreSaleTable = TypedTable<StoreSaleCol, 1>;
    using StoreReviewTable = TypedTable<StoreReviewCol, 1>;
    using StoreWantedTable = TypedTable<StoreWantedCol, 1>;
    using StoreAuctionTable = TypedTable<StoreAuctionCol, 1>;

}
