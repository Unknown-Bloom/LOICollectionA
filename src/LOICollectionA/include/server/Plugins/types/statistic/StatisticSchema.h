#pragma once

#include "LOICollectionA/data/sqlite/block/TypedTable.h"

namespace LOICollection::server::Plugins {

    using LOICollection::data::TypedTable;

    enum class StatisticsCol { onlinetime, kill, death, place, destroy, respawn, joins };
    using StatisticsTable = TypedTable<StatisticsCol, 1>;

}
