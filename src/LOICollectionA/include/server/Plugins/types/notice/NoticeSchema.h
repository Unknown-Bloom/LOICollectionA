#pragma once

#include "LOICollectionA/data/sqlite/block/TypedTable.h"

namespace LOICollection::server::Plugins {

    using LOICollection::data::TypedTable;

    enum class NoticeCol { name, close };

}

namespace LOICollection::data {

    template <>
    struct TypedColumn<LOICollection::server::Plugins::NoticeCol> {
        using Types = std::tuple<std::string, bool>;
    };

}

namespace LOICollection::server::Plugins {

    using NoticeTable = TypedTable<NoticeCol, 1>;

}
