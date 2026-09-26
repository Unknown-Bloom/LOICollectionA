#pragma once

#include "LOICollectionA/data/sqlite/block/TypedTable.h"

namespace LOICollection::server::Plugins {

    using LOICollection::data::TypedTable;

    enum class MuteCol { name, cause, time, subtime, data };

}

namespace LOICollection::data {

    template <>
    struct TypedColumn<LOICollection::server::Plugins::MuteCol> {
        using Types = std::tuple<std::string, std::string, long long, long long, std::string>;
    };

}

namespace LOICollection::server::Plugins {

    using MuteTable = TypedTable<MuteCol, 1>;

}
