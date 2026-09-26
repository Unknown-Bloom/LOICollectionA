#include <array>
#include <chrono>
#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include <SQLiteCpp/SQLiteCpp.h>

#include "LOICollectionA/data/sqlite/block/BlockError.h"
#include "LOICollectionA/data/sqlite/connection/PreparedStatements.h"

#include "LOICollectionA/data/sqlite/connection/ConnectionPool.h"

namespace {
    constexpr auto kPragmas = std::to_array<std::string_view>({
        "PRAGMA journal_mode = WAL;",
        "PRAGMA synchronous = NORMAL;",
        "PRAGMA temp_store = MEMORY;",
        "PRAGMA cache_size = 8096;",
        "PRAGMA busy_timeout = 5000;",
    });
}

SQLiteConnection::SQLiteConnection(std::string path, bool readOnly)
    : mDatabase(std::make_unique<SQLite::Database>(
          path.c_str(),
          readOnly ? SQLite::OPEN_READONLY : SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE)) {
    mStatements = std::make_unique<PreparedStatements>(this->database());
}

SQLiteConnection::~SQLiteConnection() = default;

ll::Expected<std::shared_ptr<SQLiteConnection>> SQLiteConnection::create(
    std::string path, bool readOnly) {
    std::shared_ptr<SQLiteConnection> conn;
    try {
        conn = std::shared_ptr<SQLiteConnection>(new SQLiteConnection(path, readOnly));
    } catch (const std::exception& e) {
        return ll::makeStringError("open database '" + path + "' failed: " + e.what());
    } catch (...) {
        return ll::makeStringError("open database '" + path + "' failed: unknown error");
    }

    if (!readOnly) {
        auto pragmas = conn->applyPragmas();
        if (!pragmas)
            return ll::makeStringError(pragmas.error().message());
    }

    return conn;
}

ll::Expected<void> SQLiteConnection::applyPragmas() {
    for (const auto& pragma : kPragmas) {
        const int rc = this->mDatabase->tryExec(std::string(pragma));
        if (rc != SQLite::OK) {
            return ll::makeStringError(
                "PRAGMA '" + std::string(pragma) + "' failed: " + this->mDatabase->getErrorMsg()
            );
        }
    }

    return {};
}

ConnectionPool::~ConnectionPool() = default;

ConnectionPool::ConnectionPool(std::string path) : mPath(std::move(path)) {}

ll::Expected<std::shared_ptr<ConnectionPool>> ConnectionPool::create(
    std::string path, std::size_t size, bool readOnly) {
    if (size == 0) {
        return ll::makeStringError("connection pool size must be at least 1");
    }

    std::shared_ptr<ConnectionPool> pool(new ConnectionPool(std::move(path)));
    pool->mAll.reserve(size);

    for (std::size_t i = 0; i < size; ++i) {
        auto conn = SQLiteConnection::create(pool->mPath, readOnly);
        if (!conn) {
            return ll::makeStringError(
                "connection " + std::to_string(i + 1) + "/" + std::to_string(size) + " of '" +
                pool->mPath + "' failed: " + conn.error().message()
            );
        }

        pool->mAll.push_back(*conn);
        pool->mAvailable.push(*conn);
    }

    return pool;
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
