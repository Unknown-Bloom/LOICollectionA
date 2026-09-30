#pragma once

#include <memory>
#include <string>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"
#include "LOICollectionA/base/Ownership.h"

class SQLiteConnection;
class ConnectionPool;

namespace SQLite {
    class Transaction;
}

class StorageTransaction {
public:
    LOICOLLECTION_A_API explicit StorageTransaction(std::shared_ptr<SQLiteConnection> conn, observer<ConnectionPool> pool);

    LOICOLLECTION_A_API StorageTransaction(
        std::shared_ptr<SQLiteConnection> conn, observer<ConnectionPool> pool, std::string savepoint
    );

    StorageTransaction(StorageTransaction const&) = delete;
    StorageTransaction& operator=(StorageTransaction const&) = delete;

    StorageTransaction(StorageTransaction&& other) noexcept;
    StorageTransaction& operator=(StorageTransaction&&) = delete;

    LOICOLLECTION_A_API ~StorageTransaction();

    LOICOLLECTION_A_NDAPI ll::Expected<bool> commit();
    LOICOLLECTION_A_NDAPI ll::Expected<bool> rollback();

    LOICOLLECTION_A_NDAPI std::shared_ptr<SQLiteConnection> connection() const noexcept { return mConnection; }

private:
    void finish();

    std::shared_ptr<SQLiteConnection> mConnection;
    std::unique_ptr<SQLite::Transaction> mTransaction;

    observer<ConnectionPool> mPool = nullptr;

    bool mNested = false;
    std::string mSavepoint;
};