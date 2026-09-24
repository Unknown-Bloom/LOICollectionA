#pragma once

#include "LOICollectionA/data/sqlite/block/TypedTable.h"

namespace LOICollection::server::Plugins {

    using LOICollection::data::TypedTable;

    enum class PvpCol { name, enable };
    using PvpTable = TypedTable<PvpCol, 1>;

}
