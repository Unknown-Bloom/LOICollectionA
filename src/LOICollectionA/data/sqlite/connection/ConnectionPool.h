#pragma once

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_set>
#include <vector>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"
#include "LOICollectionA/base/Ownership.h"

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

    LOICOLLECTION_A_API void bindTransaction(observer<SQLiteConnection> conn);
    LOICOLLECTION_A_API void unbindTransaction(observer<SQLiteConnection> conn);

    LOICOLLECTION_A_NDAPI observer<SQLiteConnection> activeTransaction() const noexcept;

    LOICOLLECTION_A_NDAPI bool tryAcquireWrite(int timeout);
    LOICOLLECTION_A_API void releaseWrite();

private:
    explicit ConnectionPool(std::string path);

    std::mutex mWriteMutex;
    std::condition_variable mWriteCv;
    bool mWriteBusy = false;
    std::mutex mMutex;
    std::condition_variable mCond;
    std::string mPath;
    bool mReadOnly = false;
    std::size_t mSize = 0;
    std::size_t mOpen = 0;
    std::vector<std::shared_ptr<SQLiteConnection>> mAll;
    std::queue<std::shared_ptr<SQLiteConnection>> mAvailable;
    std::unordered_set<SQLiteConnection*> mInTxn;

    // Per-thread stack of (pool, connection) pairs. SQLite connections are
    // bound to one thread and a transaction must run entirely on the
    // connection it began on, so a thread may hold an open transaction on more
    // than one pool (e.g. market.db and settings.db) at the same time. A flat
    // single slot would let one pool's transaction clobber another's affinity;
    // the stack keeps each pool's binding independent and restores it when an
    // inner pool's transaction ends.
    static thread_local std::vector<std::pair<observer<ConnectionPool const>, observer<SQLiteConnection>>> sActiveTxns;
};
