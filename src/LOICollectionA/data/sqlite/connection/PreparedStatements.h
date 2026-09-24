#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace SQLite {
    class Database;
    class Statement;
}

class PreparedStatements {
public:
    explicit PreparedStatements(SQLite::Database& db);

    [[nodiscard]] SQLite::Statement& get(std::string_view name);

    [[nodiscard]] SQLite::Statement& ensure(std::string_view name, std::string_view sql);

    void resetAll();

private:
    SQLite::Database& mDb;
    std::unordered_map<std::string, std::unique_ptr<SQLite::Statement>> mStatements;
};