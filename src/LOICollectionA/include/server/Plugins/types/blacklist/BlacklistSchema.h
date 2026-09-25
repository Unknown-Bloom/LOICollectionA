#pragma once

#include "LOICollectionA/data/sqlite/block/TypedTable.h"

namespace LOICollection::server::Plugins {

    using LOICollection::data::TypedTable;

    enum class BlacklistCol { name, cause, time, subtime, data_uuid, data_ip, data_clientid };

}

namespace LOICollection::data {

    template <>
    struct TypedColumn<LOICollection::server::Plugins::BlacklistCol> {
        using Types = std::tuple<
            std::string, std::string, long long, long long,
            std::string, std::string, std::string>;
    };

}

namespace LOICollection::server::Plugins {

    using BlacklistTable = TypedTable<BlacklistCol, 1>;

}
