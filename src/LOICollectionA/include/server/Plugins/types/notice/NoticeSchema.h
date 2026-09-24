#pragma once

#include "LOICollectionA/data/sqlite/block/TypedTable.h"

namespace LOICollection::server::Plugins {

    using LOICollection::data::TypedTable;

    enum class NoticeCol { name, close };
    using NoticeTable = TypedTable<NoticeCol, 1>;

}
