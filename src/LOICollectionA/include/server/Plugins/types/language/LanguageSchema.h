#pragma once

#include "LOICollectionA/data/sqlite/block/TypedTable.h"

namespace LOICollection::server::Plugins {

    using LOICollection::data::TypedTable;

    enum class LanguageCol { name, value };

}

namespace LOICollection::data {

    template <>
    struct TypedColumn<LOICollection::server::Plugins::LanguageCol> {
        using Types = std::tuple<std::string, std::string>;
    };

}

namespace LOICollection::server::Plugins {

    using LanguageTable = TypedTable<LanguageCol, 1>;

}
