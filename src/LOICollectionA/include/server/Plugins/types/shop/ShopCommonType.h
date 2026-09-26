#pragma once

namespace LOICollection::server::Plugins {
    enum class ShopType {
        buy,
        sell
    };

    enum class ShopAwardType {
        commodity,
        title,
        from
    };

    enum class ShopActionResult {
        Success,
        InsufficientScore,
        MissingItem,
        MissingTitle,
        InvalidNumber
    };
}
