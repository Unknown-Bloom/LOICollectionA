#pragma once

#include "LOICollectionA/data/sqlite/block/TypedTable.h"

namespace LOICollection::server::Plugins {

    using LOICollection::data::TypedTable;

    enum class TpaCol { name, invite };
    enum class TpaBlacklistCol { name, target, author, time };

}

namespace LOICollection::data {

    template <>
    struct TypedColumn<LOICollection::server::Plugins::TpaCol> {
        using Types = std::tuple<std::string, bool>;
    };
    template <>
    struct TypedColumn<LOICollection::server::Plugins::TpaBlacklistCol> {
        using Types = std::tuple<std::string, std::string, std::string, std::string>;
    };

}

namespace LOICollection::server::Plugins {

    using TpaTable = TypedTable<TpaCol, 1>;
    using TpaBlacklistTable = TypedTable<TpaBlacklistCol, 1>;

}
