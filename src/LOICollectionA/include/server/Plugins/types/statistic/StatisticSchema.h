#pragma once

#include "LOICollectionA/data/sqlite/block/TypedTable.h"

namespace LOICollection::server::Plugins {

    using LOICollection::data::TypedTable;

    enum class StatisticsCol { onlinetime, kill, death, place, destroy, respawn, joins };

}

namespace LOICollection::data {

    template <>
    struct TypedColumn<LOICollection::server::Plugins::StatisticsCol> {
        using Types = std::tuple<int, int, int, int, int, int, int>;
    };

}

namespace LOICollection::server::Plugins {

    using StatisticsTable = TypedTable<StatisticsCol, 1>;

}
