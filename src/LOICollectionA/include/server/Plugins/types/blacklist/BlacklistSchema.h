#pragma once

#include "LOICollectionA/data/sqlite/block/TypedTable.h"

namespace LOICollection::server::Plugins {

    using LOICollection::data::TypedTable;

    enum class BlacklistCol { name, cause, time, subtime, data_uuid, data_ip, data_clientid };
    using BlacklistTable = TypedTable<BlacklistCol, 1>;

}
