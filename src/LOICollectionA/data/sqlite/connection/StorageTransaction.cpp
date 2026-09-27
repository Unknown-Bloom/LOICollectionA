#include "LOICollectionA/data/sqlite/connection/StorageTransaction.h"

#include <utility>

#include <sqlite3.h>

#include <SQLiteCpp/SQLiteCpp.h>

#include "LOICollectionA/data/sqlite/block/BlockError.h"
#include "LOICollectionA/data/sqlite/connection/ConnectionPool.h"

StorageTransaction::StorageTransaction(std::shared_ptr<SQLiteConnection> conn, observer<ConnectionPool> pool)
    : mConnection(std::move(conn)), mPool(pool) {
    mTransaction = std::make_unique<SQLite::Transaction>(this->mConnection->database());
}

StorageTransaction::StorageTransaction(StorageTransaction&& other) noexcept
    : mConnection(std::move(other.mConnection)),
      mTransaction(std::move(other.mTransaction)),
      mPool(other.mPool) {
    other.mConnection.reset();
    other.mPool = nullptr;
}

StorageTransaction::~StorageTransaction() {
    if (this->mConnection)
        [[maybe_unused]] auto _ = this->rollback();
}

void StorageTransaction::finish() {
    if (auto conn = std::move(this->mConnection)) {
        this->mConnection.reset();
        this->mPool->release(std::move(conn));
        this->mPool = nullptr;
    }
}

ll::Expected<bool> StorageTransaction::commit() {
    if (!this->mTransaction)
        return false;

    try {
        this->mTransaction->commit();
        this->mTransaction.reset();
    } catch (...) {
        // A failed COMMIT leaves the transaction open; the SQLite::Transaction
        // destructor retries the rollback but swallows its error. Retry through
        // busy_timeout so the connection is never released with an active
        // transaction, and surface the underlying SQLite error for diagnosis.
        std::string detail = "commit failed: connection already released";
        if (this->mConnection) {
            auto& db = this->mConnection->database();
            db.tryExec("ROLLBACK");
            detail = "commit failed (autocommit=";
            detail += sqlite3_get_autocommit(db.getHandle()) ? "1" : "0";
            detail += "): ";
            detail += db.getErrorMsg();
        }
        this->mTransaction.reset();
        this->finish();
        return ll::makeStringError(std::move(detail));
    }
    this->finish();
    return true;
}

ll::Expected<bool> StorageTransaction::rollback() {
    if (!this->mTransaction)
        return false;

    try {
        this->mTransaction->rollback();
        this->mTransaction.reset();
    } catch (...) {
        // Same guard as commit(): never release the connection while a
        // transaction is still active on it, and report the SQLite error.
        std::string detail = "rollback failed: connection already released";
        if (this->mConnection) {
            auto& db = this->mConnection->database();
            db.tryExec("ROLLBACK");
            detail = "rollback failed (autocommit=";
            detail += sqlite3_get_autocommit(db.getHandle()) ? "1" : "0";
            detail += "): ";
            detail += db.getErrorMsg();
        }
        this->mTransaction.reset();
        this->finish();
        return ll::makeStringError(std::move(detail));
    }
    this->finish();
    return true;
}