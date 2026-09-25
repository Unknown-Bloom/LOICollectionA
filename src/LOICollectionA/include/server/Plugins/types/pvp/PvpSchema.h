#pragma once

#include "LOICollectionA/data/sqlite/block/TypedTable.h"

namespace LOICollection::server::Plugins {

    using LOICollection::data::TypedTable;

    enum class PvpCol { name, enable };

}

namespace LOICollection::data {

    template <>
    struct TypedColumn<LOICollection::server::Plugins::PvpCol> {
        using Types = std::tuple<std::string, bool>;
    };

}

namespace LOICollection::server::Plugins {

    using PvpTable = TypedTable<PvpCol, 1>;

}
