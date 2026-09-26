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
        this->mTransaction.reset();
        this->finish();
        return ll::makeErrorCodeError(
            BlockError::makeErrorCode(BlockError::BlockErrorCode::RollbackFailed));
    }
    this->finish();
    return true;
}