#include "LOICollectionA/data/sqlite/block/BlockStore.h"

#include <chrono>

#include <cstring>

#include <limits>

#include <string>

#include <utility>

#include <SQLiteCpp/SQLiteCpp.h>

#include "LOICollectionA/data/sqlite/block/BlockError.h"
#include "LOICollectionA/data/sqlite/connection/ConnectionPool.h"
#include "LOICollectionA/data/sqlite/connection/PreparedStatements.h"

namespace {
    std::int64_t nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    void bindPayload(SQLite::Statement& stmt, int index, std::string_view payload) {
        if (payload.empty())
            stmt.bind(index);
        else
            stmt.bind(index, payload.data(), static_cast<int>(payload.size()));
    }

    std::int64_t liveLimit(size_t limit) {
        return limit == 0 ? std::numeric_limits<std::int64_t>::max()
                          : static_cast<std::int64_t>(limit);
    }

    void readBlockRow(SQLite::Statement& stmt, bool withId, BlockRecord& out, SQLiteConnection& conn) {
        size_t col = 0;
        if (withId) out.id = stmt.getColumn(static_cast<int>(col++)).getInt64();
        out.parent = stmt.getColumn(static_cast<int>(col++)).getInt64();
        out.name = stmt.getColumn(static_cast<int>(col++)).getString();
        out.kind = stmt.getColumn(static_cast<int>(col++)).getInt();
        out.state = static_cast<BlockLifecycle>(stmt.getColumn(static_cast<int>(col++)).getInt64());
        out.created = stmt.getColumn(static_cast<int>(col++)).getInt64();
        out.updated = stmt.getColumn(static_cast<int>(col++)).getInt64();
        out.payload.clear();
        if (auto payloadCol = stmt.getColumn(static_cast<int>(col++)); !payloadCol.isNull()) {
            int n = payloadCol.getBytes();
            out.payload.resize(n);
            std::memcpy(out.payload.data(), payloadCol.getBlob(), n);
        }

        auto& props = conn.statements().get("getPropsByBlock");
        props.reset();
        props.bind(1, static_cast<std::int64_t>(out.id));
        while (props.executeStep()) {
            BlockProp prop;
            prop.key = props.getColumn(0).getInt();
            prop.type = static_cast<PayloadType>(props.getColumn(1).getInt());
            prop.intValue = props.getColumn(2).getInt64();
            prop.realValue = props.getColumn(3).getDouble();
            prop.textValue = props.getColumn(4).isNull() ? "" : props.getColumn(4).getString();
            out.props.push_back(std::move(prop));
        }
    }

    struct DbGuard {
        std::shared_ptr<ConnectionPool> pool;
        std::shared_ptr<SQLiteConnection> conn;

        ~DbGuard() {
            if (pool && conn)
                pool->release(std::move(conn));
        }

        SQLiteConnection* operator->() noexcept { return conn.get(); }
        SQLiteConnection& operator*() noexcept { return *conn; }
        operator SQLiteConnection&() noexcept { return *conn; }
    };

    ll::Expected<DbGuard> acquireConnection(std::shared_ptr<ConnectionPool> pool) {
        auto conn = pool->acquire();
        if (!conn)
            return ll::makeStringError(conn.error().message());
        return DbGuard{std::move(pool), std::move(*conn)};
    }
}

BlockStore::BlockStore(std::shared_ptr<ConnectionPool> pool) : mPool(std::move(pool)) {}

BlockStore::~BlockStore() = default;

BlockRecord materialize(BlockView const& view) {
    BlockRecord record;
    record.id = view.id;
    record.parent = view.parent;
    record.kind = view.kind;
    record.name = view.name;
    record.state = view.state;
    record.created = view.created;
    record.updated = view.updated;
    if (!view.payload.empty()) {
        record.payload.resize(view.payload.size());
        std::memcpy(record.payload.data(), view.payload.data(), view.payload.size());
    }
    return record;
}

ll::Expected<std::shared_ptr<SQLiteConnection>> BlockStore::acquire(int timeout) {
    return this->mPool->acquire(timeout);
}

void BlockStore::release(std::shared_ptr<SQLiteConnection> conn) {
    this->mPool->release(std::move(conn));
}

ll::Expected<void> BlockStore::ensureSchema(SQLiteConnection& conn) {
    constexpr std::string_view kSchema[] = {
        "CREATE TABLE IF NOT EXISTS dict("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL UNIQUE)",
        "CREATE TABLE IF NOT EXISTS block("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, parent INTEGER NOT NULL DEFAULT 0,"
        "name TEXT NOT NULL, kind INTEGER NOT NULL DEFAULT 0,"
        "state INTEGER NOT NULL DEFAULT 1, payload BLOB,"
        "created INTEGER NOT NULL, updated INTEGER NOT NULL)",
        "CREATE INDEX IF NOT EXISTS idx_block_parent ON block(parent)",
        "CREATE INDEX IF NOT EXISTS idx_block_parent_name ON block(parent, name)",
        "CREATE TABLE IF NOT EXISTS prop("
        "block_id INTEGER NOT NULL, key INTEGER NOT NULL, type INTEGER NOT NULL,"
        "ival INTEGER NOT NULL DEFAULT 0, rval REAL NOT NULL DEFAULT 0, tval TEXT,"
        "PRIMARY KEY(block_id,key))",
        "CREATE INDEX IF NOT EXISTS idx_prop_key_ival ON prop(key, ival)",
        "CREATE INDEX IF NOT EXISTS idx_prop_key_tval ON prop(key, tval)",
        "CREATE TABLE IF NOT EXISTS link("
        "src INTEGER NOT NULL, dst INTEGER NOT NULL, kind INTEGER NOT NULL DEFAULT 0,"
        "PRIMARY KEY(src,dst,kind))",
        "CREATE INDEX IF NOT EXISTS idx_link_dst_kind ON link(dst, kind)",
        "CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT)",
    };

    try {
        for (auto const& sql : kSchema)
            conn.database().exec(sql.data());
        return {};
    } catch (SQLite::Exception const& e) {
        return ll::makeStringError(e.what());
    }
}

ll::Expected<std::unique_ptr<BlockStore>> BlockStore::create(std::shared_ptr<ConnectionPool> pool) {
    auto store = std::unique_ptr<BlockStore>(new BlockStore(std::move(pool)));
    auto guard = acquireConnection(store->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());
    if (auto err = store->ensureSchema(*guard); !err)
        return ll::makeStringError(err.error().message());
    return store;
}

ll::Expected<BlockId> BlockStore::createBlock(
    BlockId parent, std::int32_t kind, std::string_view name, std::string_view payload) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());
    if (auto err = this->ensureSchema(*guard); !err)
        return ll::makeStringError(err.error().message());

    try {
        BlockId id = 0;
        auto res = this->implCreateBlock(*guard, parent, kind, name, payload, id);
        if (!res)
            return ll::makeStringError(res.error().message());
        return id;
    } catch (SQLite::Exception const& e) {
        return ll::makeStringError(e.what());
    }
}

ll::Expected<void> BlockStore::implCreateBlock(
    SQLiteConnection& conn, BlockId parent, std::int32_t kind,
    std::string_view name, std::string_view payload, BlockId& id) {
    auto& stmt = conn.statements().get("insertBlock");
    stmt.reset();
    stmt.bind(1, static_cast<std::int64_t>(parent));
    stmt.bind(2, std::string(name));
    stmt.bind(3, kind);
    stmt.bind(4, static_cast<std::int64_t>(BlockLifecycle::Active));
    bindPayload(stmt, 5, payload);
    const std::int64_t ts = nowMs();
    stmt.bind(6, ts);
    stmt.bind(7, ts);
    stmt.executeStep();
    id = conn.database().getLastInsertRowid();
    return {};
}

ll::Expected<BlockRecord> BlockStore::load(BlockId id) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    try {
        auto record = this->readBlock(*guard, id);
        if (!record)
            return ll::makeStringError(record.error().message());
        return std::move(*record);
    } catch (SQLite::Exception const& e) {
        return ll::makeStringError(e.what());
    }
}

ll::Expected<BlockRecord> BlockStore::load(BlockId parent, std::string_view name) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    try {
        auto& stmt = (**guard).statements().get("getBlockByName");
        stmt.reset();
        stmt.bind(1, static_cast<std::int64_t>(parent));
        stmt.bind(2, std::string(name));
        if (!stmt.executeStep())
            return ll::makeErrorCodeError(BlockError::makeErrorCode(BlockError::BlockErrorCode::NotFound));

        BlockRecord record;
        readBlockRow(stmt, true, record, *guard);
        stmt.reset();
        return record;
    } catch (SQLite::Exception const& e) {
        return ll::makeStringError(e.what());
    }
}

ll::Expected<void> BlockStore::withBlock(
    BlockId id, std::function<void(BlockView const&)> const& consumer) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    try {
        auto& stmt = (**guard).statements().get("getBlockById");
        stmt.reset();
        stmt.bind(1, id);
        if (!stmt.executeStep())
            return ll::makeErrorCodeError(BlockError::makeErrorCode(BlockError::BlockErrorCode::NotFound));

        size_t col = 0;
        BlockView view;
        view.id = id;
        view.parent = stmt.getColumn(static_cast<int>(col++)).getInt64();
        view.name = stmt.getColumn(static_cast<int>(col++)).getText();
        view.kind = stmt.getColumn(static_cast<int>(col++)).getInt();
        view.state = static_cast<BlockLifecycle>(stmt.getColumn(static_cast<int>(col++)).getInt64());
        view.created = stmt.getColumn(static_cast<int>(col++)).getInt64();
        view.updated = stmt.getColumn(static_cast<int>(col++)).getInt64();
        std::string payload;
        if (auto payloadCol = stmt.getColumn(static_cast<int>(col++)); !payloadCol.isNull()) {
            int n = payloadCol.getBytes();
            payload.resize(n);
            std::memcpy(payload.data(), payloadCol.getBlob(), n);
        }
        view.payload = payload;

        consumer(view);
        stmt.reset();
        return {};
    } catch (SQLite::Exception const& e) {
        return ll::makeStringError(e.what());
    }
}

ll::Expected<BlockRecord> BlockStore::readBlock(SQLiteConnection& conn, BlockId id) {
    auto& stmt = conn.statements().get("getBlockById");
    stmt.reset();
    stmt.bind(1, id);
    if (!stmt.executeStep())
        return ll::makeErrorCodeError(BlockError::makeErrorCode(BlockError::BlockErrorCode::NotFound));

    BlockRecord record;
    record.id = id;
    readBlockRow(stmt, false, record, conn);
    stmt.reset();
    return record;
}

ll::Expected<void> BlockStore::setPayload(BlockId id, std::string_view payload) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    try {
        auto res = this->implSetPayload(*guard, id, payload);
        if (!res)
            return ll::makeStringError(res.error().message());
        return {};
    } catch (SQLite::Exception const& e) {
        return ll::makeStringError(e.what());
    }
}

ll::Expected<void> BlockStore::implSetPayload(SQLiteConnection& conn, BlockId id, std::string_view payload) {
    auto& stmt = conn.statements().get("setPayload");
    stmt.reset();
    bindPayload(stmt, 1, payload);
    stmt.bind(2, nowMs());
    stmt.bind(3, id);
    stmt.executeStep();
    if (stmt.getChanges() == 0)
        return ll::makeErrorCodeError(BlockError::makeErrorCode(BlockError::BlockErrorCode::NotFound));
    return {};
}

ll::Expected<void> BlockStore::remove(BlockId id) {
    return this->control(id, BlockLifecycle::Deleted);
}

ll::Expected<void> BlockStore::control(BlockId id, BlockLifecycle to) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    try {
        auto res = this->implControl(*guard, id, to);
        if (!res)
            return ll::makeStringError(res.error().message());
        return {};
    } catch (SQLite::Exception const& e) {
        return ll::makeStringError(e.what());
    }
}

ll::Expected<void> BlockStore::implControl(SQLiteConnection& conn, BlockId id, BlockLifecycle to) {
    auto& stmt = conn.statements().get("updateState");
    stmt.reset();
    stmt.bind(1, static_cast<std::int64_t>(to));
    stmt.bind(2, nowMs());
    stmt.bind(3, id);
    stmt.executeStep();
    if (stmt.getChanges() == 0)
        return ll::makeErrorCodeError(BlockError::makeErrorCode(BlockError::BlockErrorCode::NotFound));
    return {};
}

ll::Expected<BlockLifecycle> BlockStore::stateOf(BlockId id) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    try {
        auto& stmt = (**guard).statements().get("getState");
        stmt.reset();
        stmt.bind(1, id);
        if (!stmt.executeStep())
            return ll::makeErrorCodeError(BlockError::makeErrorCode(BlockError::BlockErrorCode::NotFound));
        auto state = static_cast<BlockLifecycle>(stmt.getColumn(0).getInt64());
        stmt.reset();
        return state;
    } catch (SQLite::Exception const& e) {
        return ll::makeStringError(e.what());
    }
}

ll::Expected<std::vector<BlockId>> BlockStore::children(BlockId parent, std::int32_t kind, size_t limit) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    try {
        auto& stmt = (**guard).statements().get("listChildren");
        stmt.reset();
        stmt.bind(1, static_cast<std::int64_t>(parent));
        stmt.bind(2, kind);
        stmt.bind(3, kind);
        stmt.bind(4, liveLimit(limit));

        std::vector<BlockId> ids;
        while (stmt.executeStep())
            ids.push_back(stmt.getColumn(0).getInt64());
        return ids;
    } catch (SQLite::Exception const& e) {
        return ll::makeStringError(e.what());
    }
}

ll::Expected<std::vector<BlockRecord>> BlockStore::records(BlockId parent, std::int32_t kind, size_t limit) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    try {
        auto& stmt = (**guard).statements().get("getChildrenFull");
        stmt.reset();
        stmt.bind(1, static_cast<std::int64_t>(parent));
        stmt.bind(2, kind);
        stmt.bind(3, kind);
        stmt.bind(4, liveLimit(limit));

        std::vector<BlockRecord> out;
        while (stmt.executeStep()) {
            BlockRecord record;
            readBlockRow(stmt, true, record, *guard);
            out.push_back(std::move(record));
        }
        return out;
    } catch (SQLite::Exception const& e) {
        return ll::makeStringError(e.what());
    }
}

ll::Expected<void> BlockStore::setProp(BlockId id, PropKey key, std::int64_t value) {
    return this->setProp(id, key, PayloadType::Int, value, 0.0, {});
}

ll::Expected<void> BlockStore::setProp(BlockId id, PropKey key, double value) {
    return this->setProp(id, key, PayloadType::Double, 0, value, {});
}

ll::Expected<void> BlockStore::setProp(BlockId id, PropKey key, std::string_view value) {
    return this->setProp(id, key, PayloadType::Text, 0, 0.0, std::string(value));
}

ll::Expected<void> BlockStore::setProp(
    BlockId id, PropKey key, PayloadType type, std::int64_t ival, double rval, std::string_view tval) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    try {
        auto res = this->implSetProp(*guard, id, key, type, ival, rval, tval);
        if (!res)
            return ll::makeStringError(res.error().message());
        return {};
    } catch (SQLite::Exception const& e) {
        return ll::makeStringError(e.what());
    }
}

ll::Expected<void> BlockStore::implSetProp(
    SQLiteConnection& conn, BlockId id, PropKey key, PayloadType type,
    std::int64_t ival, double rval, std::string_view tval) {
    auto& stmt = conn.statements().get("insertProp");
    stmt.reset();
    stmt.bind(1, id);
    stmt.bind(2, key);
    stmt.bind(3, static_cast<int>(type));
    stmt.bind(4, ival);
    stmt.bind(5, rval);
    if (tval.empty())
        stmt.bind(6);
    else
        stmt.bind(6, std::string(tval));
    stmt.executeStep();
    if (stmt.getChanges() == 0)
        return ll::makeErrorCodeError(BlockError::makeErrorCode(BlockError::BlockErrorCode::NotFound));
    return {};
}

ll::Expected<std::vector<BlockId>> BlockStore::queryInt(PropKey key, std::int64_t min, std::int64_t max, size_t limit) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    try {
        auto& stmt = (**guard).statements().get("queryPropInt");
        stmt.reset();
        stmt.bind(1, key);
        stmt.bind(2, min);
        stmt.bind(3, max);
        stmt.bind(4, liveLimit(limit));

        std::vector<BlockId> ids;
        while (stmt.executeStep())
            ids.push_back(stmt.getColumn(0).getInt64());
        return ids;
    } catch (SQLite::Exception const& e) {
        return ll::makeStringError(e.what());
    }
}

ll::Expected<std::vector<BlockId>> BlockStore::queryInt(
    BlockId parent, PropKey key, std::int64_t min, std::int64_t max, size_t limit) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    try {
        auto& stmt = (**guard).statements().get("queryPropIntUnder");
        stmt.reset();
        stmt.bind(1, key);
        stmt.bind(2, min);
        stmt.bind(3, max);
        stmt.bind(4, static_cast<std::int64_t>(parent));
        stmt.bind(5, liveLimit(limit));

        std::vector<BlockId> ids;
        while (stmt.executeStep())
            ids.push_back(stmt.getColumn(0).getInt64());
        return ids;
    } catch (SQLite::Exception const& e) {
        return ll::makeStringError(e.what());
    }
}

ll::Expected<std::vector<BlockId>> BlockStore::queryText(PropKey key, std::string_view value, size_t limit) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    try {
        auto& stmt = (**guard).statements().get("queryPropText");
        stmt.reset();
        stmt.bind(1, key);
        stmt.bind(2, std::string(value));
        stmt.bind(3, liveLimit(limit));

        std::vector<BlockId> ids;
        while (stmt.executeStep())
            ids.push_back(stmt.getColumn(0).getInt64());
        return ids;
    } catch (SQLite::Exception const& e) {
        return ll::makeStringError(e.what());
    }
}

ll::Expected<void> BlockStore::link(BlockId src, BlockId dst, std::int32_t kind) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    try {
        auto res = this->implLink(*guard, src, dst, kind);
        if (!res)
            return ll::makeStringError(res.error().message());
        return {};
    } catch (SQLite::Exception const& e) {
        return ll::makeStringError(e.what());
    }
}

ll::Expected<void> BlockStore::implLink(SQLiteConnection& conn, BlockId src, BlockId dst, std::int32_t kind) {
    auto& stmt = conn.statements().get("insertLink");
    stmt.reset();
    stmt.bind(1, src);
    stmt.bind(2, dst);
    stmt.bind(3, kind);
    stmt.executeStep();
    return {};
}

ll::Expected<void> BlockStore::unlink(BlockId src, BlockId dst, std::int32_t kind) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    try {
        auto res = this->implUnlink(*guard, src, dst, kind);
        if (!res)
            return ll::makeStringError(res.error().message());
        return {};
    } catch (SQLite::Exception const& e) {
        return ll::makeStringError(e.what());
    }
}

ll::Expected<void> BlockStore::implUnlink(SQLiteConnection& conn, BlockId src, BlockId dst, std::int32_t kind) {
    auto& stmt = conn.statements().get("deleteLink");
    stmt.reset();
    stmt.bind(1, src);
    stmt.bind(2, dst);
    stmt.bind(3, kind);
    stmt.executeStep();
    return {};
}

ll::Expected<std::vector<BlockId>> BlockStore::links(BlockId src, std::int32_t kind) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    try {
        auto& stmt = (**guard).statements().get("linksBySrc");
        stmt.reset();
        stmt.bind(1, src);
        stmt.bind(2, kind);
        stmt.bind(3, kind);

        std::vector<BlockId> ids;
        while (stmt.executeStep())
            ids.push_back(stmt.getColumn(0).getInt64());
        return ids;
    } catch (SQLite::Exception const& e) {
        return ll::makeStringError(e.what());
    }
}

ll::Expected<std::vector<BlockId>> BlockStore::backlinks(BlockId dst, std::int32_t kind) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    try {
        auto& stmt = (**guard).statements().get("linksByDst");
        stmt.reset();
        stmt.bind(1, dst);
        stmt.bind(2, kind);
        stmt.bind(3, kind);

        std::vector<BlockId> ids;
        while (stmt.executeStep())
            ids.push_back(stmt.getColumn(0).getInt64());
        return ids;
    } catch (SQLite::Exception const& e) {
        return ll::makeStringError(e.what());
    }
}

ll::Expected<std::int32_t> BlockStore::intern(std::string_view name) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());
    return this->resolveKey(*guard, name);
}

ll::Expected<std::string> BlockStore::unintern(std::int32_t id) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    try {
        auto& stmt = (**guard).statements().get("getDictName");
        stmt.reset();
        stmt.bind(1, id);
        if (!stmt.executeStep())
            return ll::makeErrorCodeError(BlockError::makeErrorCode(BlockError::BlockErrorCode::NotFound));
        auto name = stmt.getColumn(0).getString();
        stmt.reset();
        return name;
    } catch (SQLite::Exception const& e) {
        return ll::makeStringError(e.what());
    }
}

ll::Expected<std::int32_t> BlockStore::resolveKey(SQLiteConnection& conn, std::string_view name) {
    auto& lookup = conn.statements().get("getDictId");
    lookup.reset();
    lookup.bind(1, std::string(name));
    if (lookup.executeStep()) {
        auto id = lookup.getColumn(0).getInt();
        lookup.reset();
        return id;
    }

    auto& insert = conn.statements().get("insertDict");
    insert.reset();
    insert.bind(1, std::string(name));
    insert.executeStep();

    lookup.reset();
    lookup.bind(1, std::string(name));
    if (lookup.executeStep()) {
        auto id = lookup.getColumn(0).getInt();
        lookup.reset();
        return id;
    }
    return ll::makeErrorCodeError(BlockError::makeErrorCode(BlockError::BlockErrorCode::CreateFailed));
}
