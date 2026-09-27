#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "LOICollectionA/base/Macro.h"
#include "LOICollectionA/base/Ownership.h"

namespace SQLite {
    class Database;
    class Statement;
}

class PreparedStatements {
public:
    struct Entry {
        std::unique_ptr<SQLite::Statement> statement;
        std::string sql;
    };

    LOICOLLECTION_A_API explicit PreparedStatements(SQLite::Database& db);

    LOICOLLECTION_A_API ~PreparedStatements();

    LOICOLLECTION_A_NDAPI observer<SQLite::Statement> get(std::string_view name) noexcept;

    LOICOLLECTION_A_NDAPI observer<SQLite::Statement> ensure(
        std::string_view name, std::string_view sql) noexcept;

    LOICOLLECTION_A_API void resetAll();

private:
    SQLite::Database& mDb;
    std::unordered_map<std::string, Entry> mStatements;
};
