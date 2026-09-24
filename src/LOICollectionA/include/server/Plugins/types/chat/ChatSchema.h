#pragma once

#include "LOICollectionA/data/sqlite/block/TypedTable.h"

namespace LOICollection::server::Plugins {

    using LOICollection::data::TypedTable;

    enum class ChatCol { name, title };
    using ChatTable = TypedTable<ChatCol, 1>;

    enum class TitleCol { title, author, time };
    using TitleTable = TypedTable<TitleCol, 1>;

    enum class ChatBlacklistCol { name, target, author, time };
    using ChatBlacklistTable = TypedTable<ChatBlacklistCol, 1>;

}
