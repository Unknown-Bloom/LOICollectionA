#pragma once

#include "LOICollectionA/data/sqlite/block/TypedTable.h"

namespace LOICollection::server::Plugins {

    using LOICollection::data::TypedTable;

    enum class TpaCol { name, invite };
    using TpaTable = TypedTable<TpaCol, 1>;

    enum class TpaBlacklistCol { name, target, author, time };
    using TpaBlacklistTable = TypedTable<TpaBlacklistCol, 1>;

}
