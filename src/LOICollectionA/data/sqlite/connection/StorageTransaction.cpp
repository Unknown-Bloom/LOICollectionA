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
        if (this->mPool) {
            this->mPool->unbindTransaction(conn.get());
            this->mPool->release(std::move(conn));
        }
        this->mConnection.reset();
        this->mPool = nullptr;
    }
}

ll::Expected<bool> StorageTransaction::commit() {
    if (!this->mTransaction)
        return false;

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
    if (!this->mTransaction)
        return false;

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
