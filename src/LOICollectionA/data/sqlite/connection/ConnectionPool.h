#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include <ll/api/Expected.h>

class PreparedStatements;

namespace SQLite {
    class Database;
}

class SQLiteConnection {
public:
    SQLiteConnection(std::string path, bool readOnly);

    ~SQLiteConnection();

    [[nodiscard]] SQLite::Database& database() noexcept { return *mDatabase; }
    [[nodiscard]] PreparedStatements& statements() noexcept { return *mStatements; }

private:
    std::unique_ptr<SQLite::Database> mDatabase;
    std::unique_ptr<PreparedStatements> mStatements;
};

class ConnectionPool {
public:
    ConnectionPool(std::string path, size_t size, bool readOnly = false);

    [[nodiscard]] ll::Expected<std::shared_ptr<SQLiteConnection>> acquire(int timeout = 5000);
    void release(std::shared_ptr<SQLiteConnection> conn);

private:
    std::mutex mMutex;
    std::condition_variable mCond;
    std::string mPath;
    bool mReadOnly;
    std::vector<std::shared_ptr<SQLiteConnection>> mAll;
    std::queue<std::shared_ptr<SQLiteConnection>> mAvailable;
};