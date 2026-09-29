#include "LOICollectionA/data/sqlite/connection/StorageTransaction.h"

#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include <SQLiteCpp/SQLiteCpp.h>

#include "LOICollectionA/data/sqlite/block/BlockError.h"
#include "LOICollectionA/data/sqlite/connection/ConnectionPool.h"

namespace {
// The fallback catch (...) in commit()/rollback() used to swallow the real
// exception and report only the bare "transaction ... failed" string, hiding the
// underlying SQLite error (e.g. SQLITE_BUSY/LOCKED) that actually broke the call.
// This recovers the real exception's message when one is available.
std::string transactionFailedMessage(std::string_view base) {
    if (auto ep = std::current_exception()) {
        try {
            std::rethrow_exception(ep);
        } catch (std::exception const& e) {
            return std::string(base) + ": " + e.what();
        } catch (...) {
        }
    }
    return std::string(base);
}
} // namespace

StorageTransaction::StorageTransaction(std::shared_ptr<SQLiteConnection> conn, observer<ConnectionPool> pool)
    : mConnection(std::move(conn)), mPool(pool) {
    mTransaction = std::make_unique<SQLite::Transaction>(this->mConnection->database());
    if (this->mPool)
        this->mPool->bindTransaction(this->mConnection.get());
}

StorageTransaction::StorageTransaction(
    std::shared_ptr<SQLiteConnection> conn, observer<ConnectionPool> pool, std::string savepoint)
    : mConnection(std::move(conn)), mPool(pool), mNested(true), mSavepoint(std::move(savepoint)) {
    this->mConnection->database().exec("SAVEPOINT " + this->mSavepoint);
}

StorageTransaction::StorageTransaction(StorageTransaction&& other) noexcept
    : mConnection(std::move(other.mConnection)),
      mTransaction(std::move(other.mTransaction)),
      mPool(other.mPool),
      mNested(other.mNested),
      mSavepoint(std::move(other.mSavepoint)) {
    other.mConnection.reset();
    other.mPool = nullptr;
    other.mNested = false;
    other.mSavepoint.clear();
}

StorageTransaction::~StorageTransaction() {
    if (this->mConnection)
        [[maybe_unused]] auto _ = this->rollback();
}

void StorageTransaction::finish() {
    if (this->mNested) {
        // The enclosing transaction owns the connection and the write lock.
        this->mConnection.reset();
        this->mPool = nullptr;
        return;
    }
    if (auto conn = std::move(this->mConnection)) {
        if (this->mPool) {
            this->mPool->unbindTransaction(conn.get());
            this->mPool->release(std::move(conn));
            this->mPool->releaseWrite();
        }
        this->mConnection.reset();
        this->mPool = nullptr;
    }
}

ll::Expected<bool> StorageTransaction::commit() {
    if (this->mNested) {
        if (!this->mConnection)
            return false;
        if (this->mPool && this->mPool->activeTransaction() != this->mConnection.get()) {
            // The enclosing transaction already ended and released this savepoint.
            this->mSavepoint.clear();
            this->finish();
            return true;
        }
        if (!this->mSavepoint.empty()) {
            auto& db = this->mConnection->database();
            try {
                db.exec("RELEASE SAVEPOINT " + this->mSavepoint);
            } catch (SQLite::Exception const& e) {
                this->mSavepoint.clear();
                this->finish();
                return ll::makeStringError(
                    "savepoint release failed: " + std::string(e.what()) + " (code "
                    + std::to_string(e.getErrorCode()) + ")");
            } catch (...) {
                this->mSavepoint.clear();
                this->finish();
                return ll::makeStringError(transactionFailedMessage("savepoint release failed"));
            }
            this->mSavepoint.clear();
        }
        this->finish();
        return true;
    }

    if (!this->mTransaction) {
        if (this->mConnection)
            this->finish();
        return false;
    }

    try {
        this->mTransaction->commit();
        this->mTransaction.reset();
    } catch (SQLite::Exception const& e) {
        this->mTransaction.reset();
        auto msg = std::string("transaction commit failed: ")
                   + e.what() + " (code " + std::to_string(e.getErrorCode()) + ")";
        this->finish();
        return ll::makeStringError(std::move(msg));
    } catch (...) {
        this->mTransaction.reset();
        this->finish();
        return ll::makeStringError(transactionFailedMessage("transaction commit failed"));
    }
    this->finish();
    return true;
}

ll::Expected<bool> StorageTransaction::rollback() {
    if (this->mNested) {
        if (!this->mConnection)
            return false;
        if (this->mPool && this->mPool->activeTransaction() != this->mConnection.get()) {
            // The enclosing transaction already ended; there is nothing left to undo.
            this->mSavepoint.clear();
            this->finish();
            return true;
        }
        if (!this->mSavepoint.empty()) {
            auto& db = this->mConnection->database();
            try {
                db.exec("ROLLBACK TO SAVEPOINT " + this->mSavepoint);
                db.exec("RELEASE SAVEPOINT " + this->mSavepoint);
            } catch (SQLite::Exception const& e) {
                this->mSavepoint.clear();
                this->finish();
                return ll::makeStringError(
                    "savepoint rollback failed: " + std::string(e.what()) + " (code "
                    + std::to_string(e.getErrorCode()) + ")");
            } catch (...) {
                this->mSavepoint.clear();
                this->finish();
                return ll::makeStringError(transactionFailedMessage("savepoint rollback failed"));
            }
            this->mSavepoint.clear();
        }
        this->finish();
        return true;
    }

    if (!this->mTransaction) {
        if (this->mConnection)
            this->finish();
        return false;
    }

    try {
        this->mTransaction->rollback();
        this->mTransaction.reset();
    } catch (SQLite::Exception const& e) {
        this->mTransaction.reset();
        auto msg = std::string("transaction rollback failed: ")
                   + e.what() + " (code " + std::to_string(e.getErrorCode()) + ")";
        this->finish();
        return ll::makeStringError(std::move(msg));
    } catch (...) {
        this->mTransaction.reset();
        this->finish();
        return ll::makeStringError(transactionFailedMessage("transaction rollback failed"));
    }
    this->finish();
    return true;
}
