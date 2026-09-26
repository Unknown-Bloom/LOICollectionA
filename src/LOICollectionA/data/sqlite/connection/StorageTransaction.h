#pragma once

#include <memory>

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

    StorageTransaction(StorageTransaction const&) = delete;
    StorageTransaction& operator=(StorageTransaction const&) = delete;

    LOICOLLECTION_A_API StorageTransaction(StorageTransaction&& other) noexcept;
    StorageTransaction& operator=(StorageTransaction&&) = delete;

    LOICOLLECTION_A_API ~StorageTransaction();

    LOICOLLECTION_A_NDAPI ll::Expected<bool> commit();
    LOICOLLECTION_A_NDAPI ll::Expected<bool> rollback();

    [[nodiscard]] std::shared_ptr<SQLiteConnection> connection() const noexcept { return mConnection; }

private:
    LOICOLLECTION_A_API void finish();

    std::shared_ptr<SQLiteConnection> mConnection;
    std::unique_ptr<SQLite::Transaction> mTransaction;
    observer<ConnectionPool> mPool = nullptr;
};