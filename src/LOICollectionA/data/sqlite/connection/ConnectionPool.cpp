#include "LOICollectionA/data/sqlite/connection/ConnectionPool.h"

#include <chrono>

#include <SQLiteCpp/SQLiteCpp.h>

#include "LOICollectionA/data/sqlite/block/BlockError.h"
#include "LOICollectionA/data/sqlite/connection/PreparedStatements.h"

SQLiteConnection::SQLiteConnection(std::string path, bool readOnly)
    : mDatabase(std::make_unique<SQLite::Database>(
          path.c_str(), readOnly ? SQLite::OPEN_READONLY : SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)) {
    if (!readOnly) {
        this->database().exec("PRAGMA journal_mode = WAL;");
        this->database().exec("PRAGMA synchronous = NORMAL;");
        this->database().exec("PRAGMA temp_store = MEMORY;");
        this->database().exec("PRAGMA cache_size = 8096;");
        this->database().exec("PRAGMA mmap_size = 268435456;");
        this->database().exec("PRAGMA busy_timeout = 5000;");
    }

    mStatements = std::make_unique<PreparedStatements>(this->database());
}

SQLiteConnection::~SQLiteConnection() = default;

ConnectionPool::ConnectionPool(std::string path, size_t size, bool readOnly)
    : mPath(std::move(path)), mReadOnly(readOnly) {
    std::unique_lock lock(this->mMutex);

    for (size_t i = 0; i < size; ++i) {
        auto conn = std::make_shared<SQLiteConnection>(mPath, mReadOnly);
        this->mAll.push_back(conn);
        this->mAvailable.push(conn);
    }
}

ll::Expected<std::shared_ptr<SQLiteConnection>> ConnectionPool::acquire(int timeout) {
    std::unique_lock lock(this->mMutex);

    if (!this->mCond.wait_for(lock, std::chrono::milliseconds(timeout), [this] {
            return !this->mAvailable.empty();
        })) {
        return ll::makeErrorCodeError(BlockError::makeErrorCode(BlockError::BlockErrorCode::PoolTimeout));
    }

    auto conn = this->mAvailable.front();
    this->mAvailable.pop();
    return conn;
}

void ConnectionPool::release(std::shared_ptr<SQLiteConnection> conn) {
    {
        std::unique_lock lock(this->mMutex);
        this->mAvailable.push(std::move(conn));
    }
    this->mCond.notify_one();
}