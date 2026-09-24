#pragma once

#include "LOICollectionA/data/sqlite/block/TypedTable.h"

namespace LOICollection::server::Plugins {

    using LOICollection::data::TypedTable;

    enum class MuteCol { name, cause, time, subtime, data };
    using MuteTable = TypedTable<MuteCol, 1>;

}
