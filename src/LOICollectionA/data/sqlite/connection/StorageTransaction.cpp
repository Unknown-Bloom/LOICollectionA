#include "LOICollectionA/data/sqlite/connection/StorageTransaction.h"

#include <utility>

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
        // transaction (the pool also guards this on reuse).
        if (this->mConnection)
            this->mConnection->database().tryExec("ROLLBACK");
        this->mTransaction.reset();
        this->finish();
        return ll::makeErrorCodeError(BlockError::makeErrorCode(BlockError::BlockErrorCode::CommitFailed));
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
        // transaction is still active on it.
        if (this->mConnection)
            this->mConnection->database().tryExec("ROLLBACK");
        this->mTransaction.reset();
        this->finish();
        return ll::makeErrorCodeError(
            BlockError::makeErrorCode(BlockError::BlockErrorCode::RollbackFailed));
    }
    this->finish();
    return true;
}