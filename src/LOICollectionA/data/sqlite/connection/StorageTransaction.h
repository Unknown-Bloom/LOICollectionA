#pragma once

#include <memory>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Ownership.h"

class SQLiteConnection;
class ConnectionPool;

namespace SQLite {
    class Transaction;
}

class StorageTransaction {
public:
    explicit StorageTransaction(std::shared_ptr<SQLiteConnection> conn, observer<ConnectionPool> pool);

    StorageTransaction(StorageTransaction const&) = delete;
    StorageTransaction& operator=(StorageTransaction const&) = delete;

    StorageTransaction(StorageTransaction&& other) noexcept;
    StorageTransaction& operator=(StorageTransaction&&) = delete;

    ~StorageTransaction();

    [[nodiscard]] ll::Expected<bool> commit();
    [[nodiscard]] ll::Expected<bool> rollback();

    [[nodiscard]] std::shared_ptr<SQLiteConnection> connection() const noexcept { return mConnection; }

private:
    void finish();

    std::shared_ptr<SQLiteConnection> mConnection;
    std::unique_ptr<SQLite::Transaction> mTransaction;
    observer<ConnectionPool> mPool = nullptr;
};