#include "LOICollectionA/data/sqlite/block/WriteBatch.h"

#include <string>
#include <utility>

#include <sqlite3.h>

#include <SQLiteCpp/SQLiteCpp.h>

#include "LOICollectionA/data/sqlite/connection/ConnectionPool.h"
#include "LOICollectionA/data/sqlite/connection/PreparedStatements.h"
#include "LOICollectionA/data/sqlite/connection/StorageTransaction.h"

ll::Expected<std::unique_ptr<WriteBatch>> WriteBatch::begin(BlockStore& store) {
    auto conn = store.acquire();
    if (!conn)
        return ll::makeStringError(conn.error().message());

    auto txn = std::make_unique<StorageTransaction>(std::move(*conn), store.mPool.get());
    return std::unique_ptr<WriteBatch>(new WriteBatch(store, std::move(txn)));
}

WriteBatch::WriteBatch(BlockStore& store, std::unique_ptr<StorageTransaction> txn)
    : mStore(&store), mTxn(std::move(txn)) {}

WriteBatch::~WriteBatch() = default;

ll::Expected<BlockId> WriteBatch::append(
    BlockId parent, std::int32_t kind, std::string_view name, std::string_view payload) {
    if (mFinished)
        return ll::makeStringError("write batch already finished");

    BlockId id = 0;
    auto res = mStore->implCreateBlock(*mTxn->connection(), parent, kind, name, payload, id);
    if (!res)
        return ll::makeStringError(res.error().message());
    return id;
}

ll::Expected<std::vector<BlockId>> WriteBatch::appendMany(
    BlockId parent,
    std::int32_t kind,
    std::span<const std::pair<std::string_view, std::string_view>> namePayloads) {
    std::vector<BlockId> ids;
    ids.reserve(namePayloads.size());

    for (auto& [name, payload] : namePayloads) {
        auto id = this->append(parent, kind, name, payload);
        if (!id)
            return ll::makeStringError(id.error().message());
        ids.emplace_back(*id);
    }
    return ids;
}

ll::Expected<BlockId> WriteBatch::upsertRow(
    BlockId parent, std::string_view name, std::string_view payload) {
    if (mFinished)
        return ll::makeStringError("write batch already finished");
    return mStore->implUpsertRow(*mTxn->connection(), parent, name, payload);
}

ll::Expected<void> WriteBatch::setPayload(BlockId id, std::string_view payload) {
    if (mFinished)
        return ll::makeStringError("write batch already finished");
    return mStore->implSetPayload(*mTxn->connection(), id, payload);
}

ll::Expected<void> WriteBatch::control(BlockId id, BlockLifecycle to) {
    if (mFinished)
        return ll::makeStringError("write batch already finished");
    return mStore->implControl(*mTxn->connection(), id, to);
}

ll::Expected<void> WriteBatch::setProp(BlockId id, PropKey key, std::int64_t value) {
    if (mFinished)
        return ll::makeStringError("write batch already finished");
    return mStore->implSetProp(*mTxn->connection(), id, key, PayloadType::Int, value, 0.0, {});
}

ll::Expected<void> WriteBatch::setProp(BlockId id, PropKey key, double value) {
    if (mFinished)
        return ll::makeStringError("write batch already finished");
    return mStore->implSetProp(*mTxn->connection(), id, key, PayloadType::Double, 0, value, {});
}

ll::Expected<void> WriteBatch::setProp(BlockId id, PropKey key, std::string_view value) {
    if (mFinished)
        return ll::makeStringError("write batch already finished");
    return mStore->implSetProp(*mTxn->connection(), id, key, PayloadType::Text, 0, 0.0, value);
}

ll::Expected<void> WriteBatch::setProp(
    BlockId id, PropKey key, PayloadType type,
    std::int64_t ival, double rval, std::string_view tval) {
    if (mFinished)
        return ll::makeStringError("write batch already finished");
    return mStore->implSetProp(*mTxn->connection(), id, key, type, ival, rval, tval);
}

ll::Expected<void> WriteBatch::exec(std::string_view sql) {
    if (mFinished)
        return ll::makeStringError("write batch already finished");

    auto& db = mTxn->connection()->database();
    int rc = db.tryExec(std::string(sql));
    if (rc != SQLITE_OK)
        return ll::makeStringError(std::string(db.getErrorMsg()));
    return {};
}

ll::Expected<void> WriteBatch::execCells(
    std::string_view key, std::string_view sql, std::span<const BlockProp> params) {
    if (mFinished)
        return ll::makeStringError("write batch already finished");

    auto& conn = *mTxn->connection();
    auto& stmt = conn.statements().ensure(key, sql);
    stmt.reset();
    int idx = 1;
    for (auto const& p : params) {
        if (p.type == PayloadType::Int)
            stmt.bind(idx++, static_cast<std::int64_t>(p.intValue));
        else if (p.type == PayloadType::Double)
            stmt.bind(idx++, static_cast<double>(p.realValue));
        else
            stmt.bind(idx++, p.textValue);
    }
    int rc = stmt.tryExecuteStep();
    stmt.reset();
    if (rc != SQLITE_DONE)
        return ll::makeStringError(std::string(conn.database().getErrorMsg()));
    return {};
}

ll::Expected<std::int32_t> WriteBatch::intern(std::string_view name) {
    if (mFinished)
        return ll::makeStringError("write batch already finished");
    return mStore->resolveKey(*mTxn->connection(), name);
}

ll::Expected<void> WriteBatch::link(BlockId src, BlockId dst, std::int32_t kind) {
    if (mFinished)
        return ll::makeStringError("write batch already finished");
    return mStore->implLink(*mTxn->connection(), src, dst, kind);
}

ll::Expected<bool> WriteBatch::commit() {
    if (mFinished)
        return ll::makeStringError("write batch already finished");

    auto ok = mTxn->commit();
    if (ok)
        mFinished = true;
    return ok;
}

ll::Expected<bool> WriteBatch::rollback() {
    if (mFinished)
        return ll::makeStringError("write batch already finished");

    auto ok = mTxn->rollback();
    if (ok)
        mFinished = true;
    return ok;
}
