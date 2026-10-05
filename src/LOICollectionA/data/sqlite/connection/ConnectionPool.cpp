#include <array>
#include <chrono>
#include <ranges>
#include <string>
#include <utility>
#include <exception>
#include <string_view>

#include <SQLiteCpp/SQLiteCpp.h>

#include "LOICollectionA/data/sqlite/block/BlockError.h"
#include "LOICollectionA/data/sqlite/connection/PreparedStatements.h"

#include "LOICollectionA/data/sqlite/connection/ConnectionPool.h"

#include "LOICollectionA/base/Ownership.h"

namespace LOICollection::data::detail {
    constexpr auto kPragmas = std::to_array<std::string_view>({
        "PRAGMA journal_mode = WAL;",
        "PRAGMA synchronous = NORMAL;",
        "PRAGMA temp_store = MEMORY;",
        "PRAGMA cache_size = 8096;",
        "PRAGMA busy_timeout = 5000;",
    });
}

using namespace LOICollection::data::detail;

SQLiteConnection::SQLiteConnection(std::string path, bool readOnly)
    : mDatabase(std::make_unique<SQLite::Database>(path.c_str(),
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

thread_local std::vector<std::pair<observer<ConnectionPool const>, observer<SQLiteConnection>>> ConnectionPool::sActiveTxns;

ConnectionPool::ConnectionPool(std::string path) : mPath(std::move(path)) {}

ll::Expected<std::shared_ptr<ConnectionPool>> ConnectionPool::create(
    std::string path, std::size_t size, bool readOnly
) {
    if (size == 0)
        return ll::makeStringError("connection pool size must be at least 1");

    std::shared_ptr<ConnectionPool> pool(new ConnectionPool(std::move(path)));
    pool->mSize = size;
    pool->mReadOnly = readOnly;

    return pool;
}

ll::Expected<std::shared_ptr<SQLiteConnection>> ConnectionPool::acquire(int timeout) {
    std::unique_lock lock(this->mMutex);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    while (true) {
        if (!this->mAvailable.empty()) {
            auto conn = this->mAvailable.front();
            this->mAvailable.pop();
            return conn;
        }

        if (this->mOpen < this->mSize) {
            this->mOpen++;

            lock.unlock();

            auto conn = SQLiteConnection::create(this->mPath, this->mReadOnly);
            if (!conn) {
                std::unique_lock relock(this->mMutex);
                this->mOpen--;
                return ll::makeStringError(conn.error().message());
            }

            {
                std::unique_lock relock(this->mMutex);
                this->mAll.push_back(*conn);
            }

            return conn;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return ll::makeErrorCodeError(BlockError::makeErrorCode(BlockError::BlockErrorCode::PoolTimeout));
        }

        this->mCond.wait_for(lock, deadline - now, [this] { return !this->mAvailable.empty(); });
    }
}

void ConnectionPool::release(std::shared_ptr<SQLiteConnection> conn) {
    if (!conn)
        return;

    {
        std::unique_lock lock(this->mMutex);
        if (this->mInTxn.contains(conn.get()))
            return;
    }

    conn->database().tryExec("ROLLBACK");
    
    {
        std::unique_lock lock(this->mMutex);
        this->mAvailable.push(std::move(conn));
    }

    this->mCond.notify_one();
}

void ConnectionPool::bindTransaction(observer<SQLiteConnection> conn) {
    std::unique_lock lock(this->mMutex);

    this->mInTxn.insert(conn);

    sActiveTxns.emplace_back(this, conn);
}

void ConnectionPool::unbindTransaction(observer<SQLiteConnection> conn) {
    std::unique_lock lock(this->mMutex);
    
    this->mInTxn.erase(conn);

    for (auto it = sActiveTxns.rbegin(); it != sActiveTxns.rend(); ++it) {
        if (it->first == this && it->second == conn) {
            sActiveTxns.erase(std::next(it).base());
            break;
        }
    }
}

SQLiteConnection* ConnectionPool::activeTransaction() const noexcept {
    for (auto& sActiveTxn : std::ranges::reverse_view(sActiveTxns)) {
        if (sActiveTxn.first == this)
            return sActiveTxn.second;
    }

    return nullptr;
}

bool ConnectionPool::tryAcquireWrite(int timeout) {
    std::unique_lock lock(this->mWriteMutex);
    if (!this->mWriteBusy) {
        this->mWriteBusy = true;
        return true;
    }

    if (timeout <= 0)
        return false;

    if (!this->mWriteCv.wait_for(lock, std::chrono::milliseconds(timeout), [this] { return !this->mWriteBusy; }))
        return false;

    this->mWriteBusy = true;

    return true;
}

void ConnectionPool::releaseWrite() {
    {
        std::unique_lock lock(this->mWriteMutex);
        this->mWriteBusy = false;
    }
    
    this->mWriteCv.notify_one();
}
