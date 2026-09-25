#pragma once

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"

class PreparedStatements;

namespace SQLite {
    class Database;
}

class SQLiteConnection {
public:
    LOICOLLECTION_A_NDAPI static ll::Expected<std::shared_ptr<SQLiteConnection>> create(
        std::string path, bool readOnly);

    LOICOLLECTION_A_API ~SQLiteConnection();

    [[nodiscard]] SQLite::Database& database() noexcept { return *mDatabase; }
    [[nodiscard]] PreparedStatements& statements() noexcept { return *mStatements; }

private:
    SQLiteConnection(std::string path, bool readOnly);

    [[nodiscard]] ll::Expected<void> applyPragmas();

    std::unique_ptr<SQLite::Database> mDatabase;
    std::unique_ptr<PreparedStatements> mStatements;
};

class ConnectionPool {
public:
    LOICOLLECTION_A_API ~ConnectionPool();

    LOICOLLECTION_A_NDAPI static ll::Expected<std::shared_ptr<ConnectionPool>> create(
        std::string path, std::size_t size, bool readOnly = false);

    LOICOLLECTION_A_NDAPI ll::Expected<std::shared_ptr<SQLiteConnection>> acquire(
        int timeout = 5000);

    LOICOLLECTION_A_API void release(std::shared_ptr<SQLiteConnection> conn);

private:
    explicit ConnectionPool(std::string path);

    std::mutex mMutex;
    std::condition_variable mCond;
    std::string mPath;
    std::vector<std::shared_ptr<SQLiteConnection>> mAll;
    std::queue<std::shared_ptr<SQLiteConnection>> mAvailable;
};
