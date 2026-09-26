#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include <sqlite3.h>

#include <SQLiteCpp/SQLiteCpp.h>

#include "LOICollectionA/data/sqlite/block/BlockError.h"
#include "LOICollectionA/data/sqlite/connection/ConnectionPool.h"
#include "LOICollectionA/data/sqlite/connection/PreparedStatements.h"

#include "LOICollectionA/data/sqlite/block/BlockStore.h"

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

    ll::Unexpected sqlError(SQLiteConnection& conn) {
        return ll::makeStringError(std::string(conn.database().getErrorMsg()));
    }

    ll::Unexpected prepareError(std::string name) {
        return ll::makeStringError("prepare statement '" + name + "' failed");
    }

    struct IndexSpec {
        std::string_view name;
        std::string_view ddl;
        std::string_view columns;
        std::string_view where = {};
    };

    ll::Expected<void> readBlockRow(
        SQLite::Statement& stmt, bool withId, BlockRecord& out, SQLiteConnection& conn, bool withProps = true) {
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

        if (!withProps)
            return {};

        auto props = conn.statements().get("getPropsByBlock");
        if (!props)
            return prepareError("getPropsByBlock");
        props->reset();
        props->bind(1, static_cast<std::int64_t>(out.id));
        int prc = props->tryExecuteStep();
        while (prc == SQLITE_ROW) {
            BlockProp prop;
            prop.key = props->getColumn(0).getInt();
            prop.type = static_cast<PayloadType>(props->getColumn(1).getInt());
            prop.intValue = props->getColumn(2).getInt64();
            prop.realValue = props->getColumn(3).getDouble();
            prop.textValue = props->getColumn(4).isNull() ? "" : props->getColumn(4).getString();
            out.props.push_back(std::move(prop));
            prc = props->tryExecuteStep();
        }
        props->reset();
        if (prc != SQLITE_DONE)
            return sqlError(conn);
        return {};
    }

    constexpr std::size_t kPropChunk = 500;

    ll::Expected<void> fillPropsBatch(SQLiteConnection& conn, std::vector<BlockRecord>& rows) {
        if (rows.empty())
            return {};

        std::unordered_map<BlockId, BlockRecord*> byId;
        byId.reserve(rows.size());
        for (auto& r : rows)
            byId.emplace(r.id, &r);

        for (std::size_t off = 0; off < rows.size(); off += kPropChunk) {
            std::size_t cnt = std::min(kPropChunk, rows.size() - off);
            std::string sql = "SELECT block_id,key,type,ival,rval,tval FROM prop WHERE block_id IN (";
            for (std::size_t i = 0; i < cnt; ++i) {
                if (i)
                    sql.push_back(',');
                sql.push_back('?');
            }
            sql += ")";
            auto stmt = conn.statements().ensure("propsBatch" + std::to_string(cnt), sql);
            if (!stmt)
                return prepareError("propsBatch" + std::to_string(cnt));
            stmt->reset();
            for (std::size_t i = 0; i < cnt; ++i)
                stmt->bind(static_cast<int>(i + 1), static_cast<std::int64_t>(rows[off + i].id));

            int rc = 0;
            while ((rc = stmt->tryExecuteStep()) == SQLITE_ROW) {
                auto it = byId.find(stmt->getColumn(0).getInt64());
                if (it == byId.end())
                    continue;
                BlockProp prop;
                prop.key = stmt->getColumn(1).getInt();
                prop.type = static_cast<PayloadType>(stmt->getColumn(2).getInt());
                prop.intValue = stmt->getColumn(3).getInt64();
                prop.realValue = stmt->getColumn(4).getDouble();
                prop.textValue = stmt->getColumn(5).isNull() ? "" : stmt->getColumn(5).getString();
                it->second->props.push_back(std::move(prop));
            }
            stmt->reset();
            if (rc != SQLITE_DONE)
                return sqlError(conn);
        }
        return {};
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

        decltype(auto) statements() noexcept { return conn->statements(); }
        decltype(auto) database() noexcept { return conn->database(); }
    };

    ll::Expected<DbGuard> acquireConnection(std::shared_ptr<ConnectionPool> pool) {
        auto conn = pool->acquire();
        if (!conn)
            return ll::makeStringError(conn.error().message());
        return DbGuard{std::move(pool), std::move(*conn)};
    }

    std::string nameKey(BlockId parent, std::string_view name) {
        return std::to_string(parent) + '\x1f' + std::string(name);
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
    constexpr std::string_view kTables[] = {
        "CREATE TABLE IF NOT EXISTS dict("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL UNIQUE)",
        "CREATE TABLE IF NOT EXISTS block("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, parent INTEGER NOT NULL DEFAULT 0,"
        "name TEXT NOT NULL, kind INTEGER NOT NULL DEFAULT 0,"
        "state INTEGER NOT NULL DEFAULT 1, payload BLOB,"
        "created INTEGER NOT NULL, updated INTEGER NOT NULL)",
        "CREATE TABLE IF NOT EXISTS prop("
        "block_id INTEGER NOT NULL, key INTEGER NOT NULL, type INTEGER NOT NULL,"
        "ival INTEGER NOT NULL DEFAULT 0, rval REAL NOT NULL DEFAULT 0, tval TEXT,"
        "PRIMARY KEY(block_id,key))",
        "CREATE TABLE IF NOT EXISTS link("
        "src INTEGER NOT NULL, dst INTEGER NOT NULL, kind INTEGER NOT NULL DEFAULT 0,"
        "PRIMARY KEY(src,dst,kind))",
        "CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT)",
    };

    constexpr IndexSpec kIndexes[] = {
        { "idx_block_parent",
            "CREATE INDEX IF NOT EXISTS idx_block_parent ON block(parent)",
            "parent" },
        { "idx_block_parent_kind",
            "CREATE INDEX IF NOT EXISTS idx_block_parent_kind ON block(parent, kind)",
            "parent,kind" },
        { "idx_block_parent_name",
            "CREATE INDEX IF NOT EXISTS idx_block_parent_name ON block(parent, name)",
            "parent,name" },
        { "idx_block_parent_name_row",
            "CREATE UNIQUE INDEX IF NOT EXISTS idx_block_parent_name_row ON block(parent, name) "
            "WHERE kind=1000000000",
            "parent,name", "kind=1000000000" },
        { "idx_block_parent_live",
            "CREATE INDEX IF NOT EXISTS idx_block_parent_live ON block(parent) WHERE state < 8",
            "parent", "state < 8" },
        { "idx_block_parent_kind_live",
            "CREATE INDEX IF NOT EXISTS idx_block_parent_kind_live ON block(parent, kind) WHERE state < 8",
            "parent,kind", "state < 8" },
        { "idx_prop_key_ival",
            "CREATE INDEX IF NOT EXISTS idx_prop_key_ival ON prop(key, ival, block_id)",
            "key,ival,block_id" },
        { "idx_prop_key_tval",
            "CREATE INDEX IF NOT EXISTS idx_prop_key_tval ON prop(key, tval, block_id)",
            "key,tval,block_id" },
        { "idx_link_dst_kind",
            "CREATE INDEX IF NOT EXISTS idx_link_dst_kind ON link(dst, kind)",
            "dst,kind" },
    };

    for (auto const& sql : kTables) {
        int rc = conn.database().tryExec(sql.data());
        if (rc != SQLITE_OK)
            return sqlError(conn);
    }

    constexpr int kSchemaVersion = 1;
    int current = 0;
    {
        auto ver = conn.statements().ensure("userVersionGet", "PRAGMA user_version");
        if (!ver)
            return prepareError("userVersionGet");
        ver->reset();
        if (ver->tryExecuteStep() == SQLITE_ROW)
            current = ver->getColumn(0).getInt();
        ver->reset();
    }
    if (current == kSchemaVersion)
        return {};

    for (auto const& spec : kIndexes) {
        int rc = conn.database().tryExec(("DROP INDEX IF EXISTS " + std::string(spec.name)).c_str());
        if (rc != SQLITE_OK)
            return sqlError(conn);
    }
    for (auto const& spec : kIndexes) {
        int rc = conn.database().tryExec(spec.ddl.data());
        if (rc != SQLITE_OK)
            return sqlError(conn);
    }
    {
        std::string setVer = "PRAGMA user_version = " + std::to_string(kSchemaVersion);
        int rc = conn.database().tryExec(setVer.c_str());
        if (rc != SQLITE_OK)
            return sqlError(conn);
    }
    return {};
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

    BlockId id = 0;
    if (auto res = this->implCreateBlock(*guard, parent, kind, name, payload, id); !res)
        return ll::makeStringError(res.error().message());
    return id;
}

ll::Expected<void> BlockStore::implCreateBlock(
    SQLiteConnection& conn, BlockId parent, std::int32_t kind,
    std::string_view name, std::string_view payload, BlockId& id) {
    auto stmt = conn.statements().get("insertBlock");
    if (!stmt)
        return prepareError("insertBlock");
    stmt->reset();
    stmt->bind(1, static_cast<std::int64_t>(parent));
    stmt->bind(2, std::string(name));
    stmt->bind(3, kind);
    stmt->bind(4, static_cast<std::int64_t>(BlockLifecycle::Active));
    bindPayload(*stmt, 5, payload);
    const std::int64_t ts = nowMs();
    stmt->bind(6, ts);
    stmt->bind(7, ts);
    int rc = stmt->tryExecuteStep();
    if (rc != SQLITE_DONE)
        return sqlError(conn);
    id = conn.database().getLastInsertRowid();

    BlockRecord rec;
    rec.id = id;
    rec.parent = parent;
    rec.kind = kind;
    rec.name = std::string(name);
    rec.state = BlockLifecycle::Active;
    rec.created = ts;
    rec.updated = ts;
    if (!payload.empty()) {
        rec.payload.resize(payload.size());
        std::memcpy(rec.payload.data(), payload.data(), payload.size());
    }
    mBlockCache.put(id, std::make_shared<BlockRecord>(std::move(rec)));
    mNameCache.put(nameKey(parent, name), id);
    return {};
}

ll::Expected<BlockId> BlockStore::upsertRow(
    BlockId parent, std::string_view name, std::string_view payload) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());
    return this->implUpsertRow(*guard, parent, name, payload);
}

ll::Expected<BlockId> BlockStore::implUpsertRow(
    SQLiteConnection& conn, BlockId parent, std::string_view name, std::string_view payload) {
    auto stmt = conn.statements().get("upsertRow");
    if (!stmt)
        return prepareError("upsertRow");
    stmt->reset();
    stmt->bind(1, static_cast<std::int64_t>(parent));
    stmt->bind(2, std::string(name));
    stmt->bind(3, kTypedRowKind);
    stmt->bind(4, static_cast<std::int64_t>(BlockLifecycle::Active));
    bindPayload(*stmt, 5, payload);
    const std::int64_t ts = nowMs();
    stmt->bind(6, ts);
    stmt->bind(7, ts);
    int rc = stmt->tryExecuteStep();
    if (rc != SQLITE_ROW)
        return sqlError(conn);
    BlockId id = static_cast<BlockId>(stmt->getColumn(0).getInt64());
    stmt->reset();

    auto key = nameKey(parent, name);
    if (auto cached = mNameCache.get(key); cached.has_value())
        mBlockCache.erase(*cached.value());
    return id;
}

ll::Expected<BlockRecord> BlockStore::load(BlockId id) {
    if (auto cached = mBlockCache.get(id); cached.has_value())
        return **cached;

    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    auto record = this->readBlock(*guard, id);
    if (!record)
        return ll::makeStringError(record.error().message());
    auto shared = std::make_shared<BlockRecord>(std::move(*record));
    mBlockCache.put(id, shared);
    return *shared;
}

ll::Expected<BlockRecord> BlockStore::load(BlockId parent, std::string_view name) {
    std::string key = nameKey(parent, name);
    if (auto cached = mNameCache.get(key); cached.has_value()) {
        auto rec = this->load(*cached.value());
        if (rec.has_value()) {
            if (rec.value().state != BlockLifecycle::Deleted)
                return rec.value();
            mNameCache.erase(key);
        } else {
            mNameCache.erase(key);
        }
    }

    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    auto stmt = (*guard).statements().get("getBlockByName");
    if (!stmt)
        return prepareError("getBlockByName");
    stmt->reset();
    stmt->bind(1, static_cast<std::int64_t>(parent));
    stmt->bind(2, std::string(name));
    int rc = stmt->tryExecuteStep();
    if (rc == SQLITE_DONE)
        return ll::makeErrorCodeError(BlockError::makeErrorCode(BlockError::BlockErrorCode::NotFound));
    if (rc != SQLITE_ROW)
        return sqlError(*guard);

    BlockRecord record;
    if (auto r = readBlockRow(*stmt, true, record, *guard); !r)
        return ll::makeStringError(r.error().message());
    stmt->reset();
    mBlockCache.put(record.id, std::make_shared<BlockRecord>(record));
    if (record.state != BlockLifecycle::Deleted)
        mNameCache.put(key, record.id);
    return record;
}

ll::Expected<std::optional<BlockId>> BlockStore::idOf(BlockId parent, std::string_view name) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    auto stmt = (*guard).statements().get("getIdByName");
    if (!stmt)
        return prepareError("getIdByName");
    stmt->reset();
    stmt->bind(1, static_cast<std::int64_t>(parent));
    stmt->bind(2, std::string(name));
    int rc = stmt->tryExecuteStep();
    if (rc == SQLITE_DONE) {
        stmt->reset();
        return std::optional<BlockId>{};
    }
    if (rc != SQLITE_ROW)
        return sqlError(*guard);

    BlockId id = stmt->getColumn(0).getInt64();
    stmt->reset();
    mNameCache.put(nameKey(parent, name), id);
    return id;
}

ll::Expected<void> BlockStore::withBlock(
    BlockId id, std::function<void(BlockView const&)> const& consumer) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    auto stmt = (*guard).statements().get("getBlockById");
    if (!stmt)
        return prepareError("getBlockById");
    stmt->reset();
    stmt->bind(1, id);
    int rc = stmt->tryExecuteStep();
    if (rc == SQLITE_DONE)
        return ll::makeErrorCodeError(BlockError::makeErrorCode(BlockError::BlockErrorCode::NotFound));
    if (rc != SQLITE_ROW)
        return sqlError(*guard);

    size_t col = 0;
    BlockView view;
    view.id = id;
    view.parent = stmt->getColumn(static_cast<int>(col++)).getInt64();
    view.name = stmt->getColumn(static_cast<int>(col++)).getText();
    view.kind = stmt->getColumn(static_cast<int>(col++)).getInt();
    view.state = static_cast<BlockLifecycle>(stmt->getColumn(static_cast<int>(col++)).getInt64());
    view.created = stmt->getColumn(static_cast<int>(col++)).getInt64();
    view.updated = stmt->getColumn(static_cast<int>(col++)).getInt64();
    std::string payload;
    if (auto payloadCol = stmt->getColumn(static_cast<int>(col++)); !payloadCol.isNull()) {
        int n = payloadCol.getBytes();
        payload.resize(n);
        std::memcpy(payload.data(), payloadCol.getBlob(), n);
    }
    view.payload = payload;

    consumer(view);
    stmt->reset();
    return {};
}

ll::Expected<BlockRecord> BlockStore::readBlock(SQLiteConnection& conn, BlockId id) {
    auto stmt = conn.statements().get("getBlockById");
    if (!stmt)
        return prepareError("getBlockById");
    stmt->reset();
    stmt->bind(1, id);
    int rc = stmt->tryExecuteStep();
    if (rc == SQLITE_DONE)
        return ll::makeErrorCodeError(BlockError::makeErrorCode(BlockError::BlockErrorCode::NotFound));
    if (rc != SQLITE_ROW)
        return sqlError(conn);

    BlockRecord record;
    record.id = id;
    if (auto r = readBlockRow(*stmt, false, record, conn); !r)
        return ll::makeStringError(r.error().message());
    stmt->reset();
    return record;
}

ll::Expected<void> BlockStore::setPayload(BlockId id, std::string_view payload) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    if (auto res = this->implSetPayload(*guard, id, payload); !res)
        return ll::makeStringError(res.error().message());
    return {};
}

ll::Expected<void> BlockStore::implSetPayload(SQLiteConnection& conn, BlockId id, std::string_view payload) {
    auto stmt = conn.statements().get("setPayload");
    if (!stmt)
        return prepareError("setPayload");
    stmt->reset();
    bindPayload(*stmt, 1, payload);
    stmt->bind(2, nowMs());
    stmt->bind(3, id);
    int rc = stmt->tryExecuteStep();
    if (rc != SQLITE_DONE)
        return sqlError(conn);
    if (stmt->getChanges() == 0)
        return ll::makeErrorCodeError(BlockError::makeErrorCode(BlockError::BlockErrorCode::NotFound));
    mBlockCache.erase(id);
    return {};
}

ll::Expected<void> BlockStore::remove(BlockId id) {
    return this->control(id, BlockLifecycle::Deleted);
}

ll::Expected<void> BlockStore::control(BlockId id, BlockLifecycle to) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    if (auto res = this->implControl(*guard, id, to); !res)
        return ll::makeStringError(res.error().message());
    return {};
}

ll::Expected<void> BlockStore::implControl(SQLiteConnection& conn, BlockId id, BlockLifecycle to) {
    auto stmt = conn.statements().get("updateState");
    if (!stmt)
        return prepareError("updateState");
    stmt->reset();
    stmt->bind(1, static_cast<std::int64_t>(to));
    stmt->bind(2, nowMs());
    stmt->bind(3, id);
    int rc = stmt->tryExecuteStep();
    if (rc != SQLITE_DONE)
        return sqlError(conn);
    if (stmt->getChanges() == 0)
        return ll::makeErrorCodeError(BlockError::makeErrorCode(BlockError::BlockErrorCode::NotFound));
    mBlockCache.erase(id);
    return {};
}

ll::Expected<BlockLifecycle> BlockStore::stateOf(BlockId id) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    auto stmt = (*guard).statements().get("getState");
    if (!stmt)
        return prepareError("getState");
    stmt->reset();
    stmt->bind(1, id);
    int rc = stmt->tryExecuteStep();
    if (rc == SQLITE_DONE)
        return ll::makeErrorCodeError(BlockError::makeErrorCode(BlockError::BlockErrorCode::NotFound));
    if (rc != SQLITE_ROW)
        return sqlError(*guard);
    auto state = static_cast<BlockLifecycle>(stmt->getColumn(0).getInt64());
    stmt->reset();
    return state;
}

ll::Expected<std::vector<BlockId>> BlockStore::children(BlockId parent, std::int32_t kind, size_t limit) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    auto stmt = (*guard).statements().get(kind < 0 ? "listChildren" : "listChildrenKind");
    if (!stmt)
        return prepareError(std::string(kind < 0 ? "listChildren" : "listChildrenKind"));
    stmt->reset();
    stmt->bind(1, static_cast<std::int64_t>(parent));
    if (kind < 0) {
        stmt->bind(2, liveLimit(limit));
    } else {
        stmt->bind(2, kind);
        stmt->bind(3, liveLimit(limit));
    }

    std::vector<BlockId> ids;
    int rc = 0;
    while ((rc = stmt->tryExecuteStep()) == SQLITE_ROW)
        ids.push_back(stmt->getColumn(0).getInt64());
    if (rc != SQLITE_DONE)
        return sqlError(*guard);
    return ids;
}

ll::Expected<std::vector<std::pair<BlockId, std::string>>> BlockStore::childNames(BlockId parent) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    auto stmt = (*guard).statements().get("listChildNames");
    if (!stmt)
        return prepareError("listChildNames");
    stmt->reset();
    stmt->bind(1, static_cast<std::int64_t>(parent));

    std::vector<std::pair<BlockId, std::string>> out;
    int rc = 0;
    while ((rc = stmt->tryExecuteStep()) == SQLITE_ROW)
        out.emplace_back(static_cast<BlockId>(stmt->getColumn(0).getInt64()), stmt->getColumn(1).getString());
    if (rc != SQLITE_DONE)
        return sqlError(*guard);
    return out;
}

ll::Expected<std::vector<BlockRecord>> BlockStore::records(BlockId parent, std::int32_t kind, size_t limit) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    auto stmt = (*guard).statements().get(kind < 0 ? "getChildrenFull" : "getChildrenFullKind");
    if (!stmt)
        return prepareError(std::string(kind < 0 ? "getChildrenFull" : "getChildrenFullKind"));
    stmt->reset();
    stmt->bind(1, static_cast<std::int64_t>(parent));
    if (kind < 0) {
        stmt->bind(2, liveLimit(limit));
    } else {
        stmt->bind(2, kind);
        stmt->bind(3, liveLimit(limit));
    }

    std::vector<BlockRecord> out;
    int rc = 0;
    while ((rc = stmt->tryExecuteStep()) == SQLITE_ROW) {
        BlockRecord record;
        if (auto r = readBlockRow(*stmt, true, record, *guard, false); !r)
            return ll::makeStringError(r.error().message());
        out.push_back(std::move(record));
    }
    stmt->reset();
    if (rc != SQLITE_DONE)
        return sqlError(*guard);

    if (auto r = fillPropsBatch(*guard, out); !r)
        return ll::makeStringError(r.error().message());
    return out;
}

ll::Expected<std::vector<BlockRecord>> BlockStore::rowsByIds(std::span<const BlockId> ids, bool withProps) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    std::vector<BlockRecord> out;
    if (ids.empty())
        return out;
    out.reserve(ids.size());

    for (std::size_t off = 0; off < ids.size(); off += kPropChunk) {
        std::size_t cnt = std::min(kPropChunk, ids.size() - off);
        std::string sql = "SELECT id,parent,name,kind,state,created,updated,payload FROM block WHERE id IN (";
        for (std::size_t i = 0; i < cnt; ++i) {
            if (i)
                sql.push_back(',');
            sql.push_back('?');
        }
        sql += ")";
        auto stmt = (*guard).statements().ensure("rowsByIds" + std::to_string(cnt), sql);
        if (!stmt)
            return prepareError("rowsByIds" + std::to_string(cnt));
        stmt->reset();
        for (std::size_t i = 0; i < cnt; ++i)
            stmt->bind(static_cast<int>(i + 1), static_cast<std::int64_t>(ids[off + i]));

        int rc = 0;
        while ((rc = stmt->tryExecuteStep()) == SQLITE_ROW) {
            BlockRecord record;
            if (auto r = readBlockRow(*stmt, true, record, *guard, false); !r)
                return ll::makeStringError(r.error().message());
            out.push_back(std::move(record));
        }
        stmt->reset();
        if (rc != SQLITE_DONE)
            return sqlError(*guard);
    }

    if (withProps) {
        if (auto r = fillPropsBatch(*guard, out); !r)
            return ll::makeStringError(r.error().message());
    }
    return out;
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

    if (auto res = this->implSetProp(*guard, id, key, type, ival, rval, tval); !res)
        return ll::makeStringError(res.error().message());
    return {};
}

ll::Expected<void> BlockStore::implSetProp(
    SQLiteConnection& conn, BlockId id, PropKey key, PayloadType type,
    std::int64_t ival, double rval, std::string_view tval) {
    auto stmt = conn.statements().get("insertProp");
    if (!stmt)
        return prepareError("insertProp");
    stmt->reset();
    stmt->bind(1, id);
    stmt->bind(2, key);
    stmt->bind(3, static_cast<int>(type));
    stmt->bind(4, ival);
    stmt->bind(5, rval);
    if (tval.empty())
        stmt->bind(6);
    else
        stmt->bind(6, std::string(tval));
    int rc = stmt->tryExecuteStep();
    if (rc != SQLITE_DONE)
        return sqlError(conn);
    if (stmt->getChanges() == 0)
        return ll::makeErrorCodeError(BlockError::makeErrorCode(BlockError::BlockErrorCode::NotFound));
    mBlockCache.erase(id);
    return {};
}

ll::Expected<std::vector<BlockId>> BlockStore::queryInt(PropKey key, std::int64_t min, std::int64_t max, size_t limit) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    auto stmt = (*guard).statements().get("queryPropInt");
    if (!stmt)
        return prepareError("queryPropInt");
    stmt->reset();
    stmt->bind(1, key);
    stmt->bind(2, min);
    stmt->bind(3, max);
    stmt->bind(4, liveLimit(limit));

    std::vector<BlockId> ids;
    int rc = 0;
    while ((rc = stmt->tryExecuteStep()) == SQLITE_ROW)
        ids.push_back(stmt->getColumn(0).getInt64());
    if (rc != SQLITE_DONE)
        return sqlError(*guard);
    return ids;
}

ll::Expected<std::vector<BlockId>> BlockStore::queryInt(
    BlockId parent, PropKey key, std::int64_t min, std::int64_t max, size_t limit) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    auto stmt = (*guard).statements().get("queryPropIntUnder");
    if (!stmt)
        return prepareError("queryPropIntUnder");
    stmt->reset();
    stmt->bind(1, key);
    stmt->bind(2, min);
    stmt->bind(3, max);
    stmt->bind(4, static_cast<std::int64_t>(parent));
    stmt->bind(5, liveLimit(limit));

    std::vector<BlockId> ids;
    int rc = 0;
    while ((rc = stmt->tryExecuteStep()) == SQLITE_ROW)
        ids.push_back(stmt->getColumn(0).getInt64());
    if (rc != SQLITE_DONE)
        return sqlError(*guard);
    return ids;
}

ll::Expected<std::vector<std::pair<BlockId, std::string>>> BlockStore::queryTextNames(
    BlockId parent, PropKey key, std::string_view value, size_t limit) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    auto stmt = (*guard).statements().get("queryPropTextUnderNames");
    if (!stmt)
        return prepareError("queryPropTextUnderNames");
    stmt->reset();
    stmt->bind(1, key);
    stmt->bind(2, std::string(value));
    stmt->bind(3, static_cast<std::int64_t>(parent));
    stmt->bind(4, liveLimit(limit));

    std::vector<std::pair<BlockId, std::string>> out;
    int rc = 0;
    while ((rc = stmt->tryExecuteStep()) == SQLITE_ROW)
        out.emplace_back(
            static_cast<BlockId>(stmt->getColumn(0).getInt64()), stmt->getColumn(1).getString());
    if (rc != SQLITE_DONE)
        return sqlError(*guard);
    return out;
}

ll::Expected<std::vector<std::pair<BlockId, std::string>>> BlockStore::queryTextNamesAll(
    BlockId parent, std::vector<PropMatch> const& conds, size_t limit) {
    if (conds.empty())
        return std::vector<std::pair<BlockId, std::string>>{};
    if (conds.size() == 1)
        return this->queryTextNames(parent, conds.front().key, conds.front().value, limit);

    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    std::size_t const n = conds.size();
    std::string sql = "SELECT b.id, b.name FROM prop p0";
    for (std::size_t i = 1; i < n; ++i) {
        sql += " CROSS JOIN prop p";
        sql += std::to_string(i);
        sql += " ON p";
        sql += std::to_string(i);
        sql += ".block_id=p0.block_id";
    }
    sql += " CROSS JOIN block b ON b.id=p0.block_id";
    sql += " WHERE p0.key=? AND p0.tval=?";
    for (std::size_t i = 1; i < n; ++i) {
        sql += " AND p";
        sql += std::to_string(i);
        sql += ".key=? AND p";
        sql += std::to_string(i);
        sql += ".tval=?";
    }
    sql += " AND b.parent=? AND b.state < 8";
    sql += " ORDER BY b.id LIMIT ?";

    auto stmt = (*guard).statements().ensure("queryTextNamesAll" + std::to_string(n), sql);
    if (!stmt)
        return prepareError("queryTextNamesAll" + std::to_string(n));
    stmt->reset();
    int idx = 1;
    for (auto const& c : conds) {
        stmt->bind(idx++, static_cast<int>(c.key));
        stmt->bind(idx++, c.value);
    }
    stmt->bind(idx++, static_cast<std::int64_t>(parent));
    stmt->bind(idx++, liveLimit(limit));

    std::vector<std::pair<BlockId, std::string>> out;
    int rc = 0;
    while ((rc = stmt->tryExecuteStep()) == SQLITE_ROW)
        out.emplace_back(
            static_cast<BlockId>(stmt->getColumn(0).getInt64()), stmt->getColumn(1).getString());
    if (rc != SQLITE_DONE)
        return sqlError(*guard);
    return out;
}

ll::Expected<std::vector<BlockId>> BlockStore::queryText(PropKey key, std::string_view value, size_t limit) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    auto stmt = (*guard).statements().get("queryPropText");
    if (!stmt)
        return prepareError("queryPropText");
    stmt->reset();
    stmt->bind(1, key);
    stmt->bind(2, std::string(value));
    stmt->bind(3, liveLimit(limit));

    std::vector<BlockId> ids;
    int rc = 0;
    while ((rc = stmt->tryExecuteStep()) == SQLITE_ROW)
        ids.push_back(stmt->getColumn(0).getInt64());
    if (rc != SQLITE_DONE)
        return sqlError(*guard);
    return ids;
}

ll::Expected<std::vector<BlockId>> BlockStore::queryText(
    BlockId parent, PropKey key, std::string_view value, size_t limit) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    auto stmt = (*guard).statements().get("queryPropTextUnder");
    if (!stmt)
        return prepareError("queryPropTextUnder");
    stmt->reset();
    stmt->bind(1, key);
    stmt->bind(2, std::string(value));
    stmt->bind(3, parent);
    stmt->bind(4, liveLimit(limit));

    std::vector<BlockId> ids;
    int rc = 0;
    while ((rc = stmt->tryExecuteStep()) == SQLITE_ROW)
        ids.push_back(stmt->getColumn(0).getInt64());
    if (rc != SQLITE_DONE)
        return sqlError(*guard);
    return ids;
}

ll::Expected<void> BlockStore::exec(std::string_view sql) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());
    int rc = (*guard).database().tryExec(std::string(sql).c_str());
    if (rc != SQLITE_OK)
        return sqlError(*guard);
    return {};
}

ll::Expected<void> BlockStore::withQuery(
    std::string_view key, std::string_view sql, std::span<const BlockProp> params,
    std::function<void(SQLite::Statement&)> const& consumer) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    auto stmt = (*guard).statements().ensure(key, sql);
    if (!stmt)
        return prepareError(std::string(key));
    stmt->reset();
    int idx = 1;
    for (auto const& p : params) {
        if (p.type == PayloadType::Int)
            stmt->bind(idx++, static_cast<std::int64_t>(p.intValue));
        else if (p.type == PayloadType::Double)
            stmt->bind(idx++, static_cast<double>(p.realValue));
        else
            stmt->bind(idx++, p.textValue);
    }
    int rc = 0;
    while ((rc = stmt->tryExecuteStep()) == SQLITE_ROW)
        consumer(*stmt);
    stmt->reset();
    if (rc != SQLITE_DONE)
        return sqlError(*guard);
    return {};
}

ll::Expected<void> BlockStore::link(BlockId src, BlockId dst, std::int32_t kind) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    if (auto res = this->implLink(*guard, src, dst, kind); !res)
        return ll::makeStringError(res.error().message());
    return {};
}

ll::Expected<void> BlockStore::implLink(SQLiteConnection& conn, BlockId src, BlockId dst, std::int32_t kind) {
    auto stmt = conn.statements().get("insertLink");
    if (!stmt)
        return prepareError("insertLink");
    stmt->reset();
    stmt->bind(1, src);
    stmt->bind(2, dst);
    stmt->bind(3, kind);
    int rc = stmt->tryExecuteStep();
    if (rc != SQLITE_DONE)
        return sqlError(conn);
    return {};
}

ll::Expected<void> BlockStore::unlink(BlockId src, BlockId dst, std::int32_t kind) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    if (auto res = this->implUnlink(*guard, src, dst, kind); !res)
        return ll::makeStringError(res.error().message());
    return {};
}

ll::Expected<void> BlockStore::implUnlink(SQLiteConnection& conn, BlockId src, BlockId dst, std::int32_t kind) {
    auto stmt = conn.statements().get("deleteLink");
    if (!stmt)
        return prepareError("deleteLink");
    stmt->reset();
    stmt->bind(1, src);
    stmt->bind(2, dst);
    stmt->bind(3, kind);
    int rc = stmt->tryExecuteStep();
    if (rc != SQLITE_DONE)
        return sqlError(conn);
    return {};
}

ll::Expected<std::vector<BlockId>> BlockStore::links(BlockId src, std::int32_t kind) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    auto stmt = (*guard).statements().get("linksBySrc");
    if (!stmt)
        return prepareError("linksBySrc");
    stmt->reset();
    stmt->bind(1, src);
    stmt->bind(2, kind);
    stmt->bind(3, kind);

    std::vector<BlockId> ids;
    int rc = 0;
    while ((rc = stmt->tryExecuteStep()) == SQLITE_ROW)
        ids.push_back(stmt->getColumn(0).getInt64());
    if (rc != SQLITE_DONE)
        return sqlError(*guard);
    return ids;
}

ll::Expected<std::vector<BlockId>> BlockStore::backlinks(BlockId dst, std::int32_t kind) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    auto stmt = (*guard).statements().get("linksByDst");
    if (!stmt)
        return prepareError("linksByDst");
    stmt->reset();
    stmt->bind(1, dst);
    stmt->bind(2, kind);
    stmt->bind(3, kind);

    std::vector<BlockId> ids;
    int rc = 0;
    while ((rc = stmt->tryExecuteStep()) == SQLITE_ROW)
        ids.push_back(stmt->getColumn(0).getInt64());
    if (rc != SQLITE_DONE)
        return sqlError(*guard);
    return ids;
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

    auto stmt = (*guard).statements().get("getDictName");
    if (!stmt)
        return prepareError("getDictName");
    stmt->reset();
    stmt->bind(1, id);
    int rc = stmt->tryExecuteStep();
    if (rc == SQLITE_DONE)
        return ll::makeErrorCodeError(BlockError::makeErrorCode(BlockError::BlockErrorCode::NotFound));
    if (rc != SQLITE_ROW)
        return sqlError(*guard);
    auto name = stmt->getColumn(0).getString();
    stmt->reset();
    return name;
}

ll::Expected<std::int32_t> BlockStore::resolveKey(SQLiteConnection& conn, std::string_view name) {
    auto lookup = conn.statements().get("getDictId");
    if (!lookup)
        return prepareError("getDictId");
    lookup->reset();
    lookup->bind(1, std::string(name));
    int rc = lookup->tryExecuteStep();
    if (rc == SQLITE_ROW) {
        auto id = lookup->getColumn(0).getInt();
        lookup->reset();
        return id;
    }
    if (rc != SQLITE_DONE)
        return sqlError(conn);

    auto insert = conn.statements().get("insertDict");
    if (!insert)
        return prepareError("insertDict");
    insert->reset();
    insert->bind(1, std::string(name));
    int irc = insert->tryExecuteStep();
    if (irc != SQLITE_DONE)
        return sqlError(conn);

    lookup->reset();
    lookup->bind(1, std::string(name));
    int rrc = lookup->tryExecuteStep();
    if (rrc == SQLITE_ROW) {
        auto id = lookup->getColumn(0).getInt();
        lookup->reset();
        return id;
    }
    if (rrc != SQLITE_DONE)
        return sqlError(conn);
    return ll::makeErrorCodeError(BlockError::makeErrorCode(BlockError::BlockErrorCode::CreateFailed));
}

ll::Expected<std::optional<std::string>> BlockStore::metaGet(std::string_view key) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    auto stmt = (*guard).statements().get("getMeta");
    if (!stmt)
        return prepareError("getMeta");
    stmt->reset();
    stmt->bind(1, std::string(key));
    int rc = stmt->tryExecuteStep();
    if (rc == SQLITE_DONE)
        return std::nullopt;
    if (rc != SQLITE_ROW)
        return sqlError(*guard);
    auto value = stmt->getColumn(0).getString();
    stmt->reset();
    return std::optional<std::string>(std::string(value));
}

ll::Expected<void> BlockStore::metaSet(std::string_view key, std::string_view value) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    auto stmt = (*guard).statements().get("setMeta");
    if (!stmt)
        return prepareError("setMeta");
    stmt->reset();
    stmt->bind(1, std::string(key));
    stmt->bind(2, std::string(value));
    int rc = stmt->tryExecuteStep();
    if (rc != SQLITE_DONE)
        return sqlError(*guard);
    return {};
}

ll::Expected<void> BlockStore::metaDel(std::string_view key) {
    auto guard = acquireConnection(this->mPool);
    if (!guard)
        return ll::makeStringError(guard.error().message());

    auto stmt = (*guard).statements().get("delMeta");
    if (!stmt)
        return prepareError("delMeta");
    stmt->reset();
    stmt->bind(1, std::string(key));
    int rc = stmt->tryExecuteStep();
    if (rc != SQLITE_DONE)
        return sqlError(*guard);
    return {};
}
