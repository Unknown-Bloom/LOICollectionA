#pragma once

#include "LOICollectionA/data/sqlite/block/TypedTable.h"

namespace LOICollection::server::Plugins {

    using LOICollection::data::TypedTable;

    enum class ChatCol { name, title };
    enum class TitleCol { title, author, time };
    enum class ChatBlacklistCol { name, target, author, time };

}

namespace LOICollection::data {

    template <>
    struct TypedColumn<LOICollection::server::Plugins::ChatCol> {
        using Types = std::tuple<std::string, std::string>;
    };
    template <>
    struct TypedColumn<LOICollection::server::Plugins::TitleCol> {
        using Types = std::tuple<std::string, std::string, std::string>;
    };
    template <>
    struct TypedColumn<LOICollection::server::Plugins::ChatBlacklistCol> {
        using Types = std::tuple<std::string, std::string, std::string, std::string>;
    };

}

namespace LOICollection::server::Plugins {

    using ChatTable = TypedTable<ChatCol, 1>;
    using TitleTable = TypedTable<TitleCol, 1>;
    using ChatBlacklistTable = TypedTable<ChatBlacklistCol, 1>;

}
