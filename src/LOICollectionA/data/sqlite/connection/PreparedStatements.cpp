#include "LOICollectionA/data/sqlite/connection/PreparedStatements.h"

#include <array>

#include <SQLiteCpp/SQLiteCpp.h>

namespace {
    using StatementSpec = std::pair<std::string_view, std::string_view>;

    constexpr auto kCatalog = std::to_array<StatementSpec>({
        { "insertBlock",
            "INSERT INTO block(parent,name,kind,state,payload,created,updated) VALUES(?,?,?,?,?,?,?)" },
        { "getBlockById",
            "SELECT parent,name,kind,state,created,updated,payload FROM block WHERE id=?" },
        { "getBlockByName",
            "SELECT id,parent,name,kind,state,created,updated,payload FROM block WHERE parent=? AND name=? AND (state & 24)=0 LIMIT 1" },
        { "updateState",
            "UPDATE block SET state=?,updated=? WHERE id=?" },
        { "setPayload",
            "UPDATE block SET payload=?,updated=? WHERE id=?" },
        { "listChildren",
            "SELECT id FROM block WHERE parent=? AND (state & 24)=0 AND (?=-1 OR kind=?) ORDER BY id LIMIT ?" },
        { "getChildrenFull",
            "SELECT id,parent,name,kind,state,created,updated,payload FROM block "
            "WHERE parent=? AND (state & 24)=0 AND (?=-1 OR kind=?) ORDER BY id LIMIT ?" },
        { "getState", "SELECT state FROM block WHERE id=?" },
        { "countChildren",
            "SELECT COUNT(*) FROM block WHERE parent=? AND (state & 24)=0 AND (?=-1 OR kind=?)" },
        { "deletePropsByBlock", "DELETE FROM prop WHERE block_id=?" },
        { "getPropsByBlock", "SELECT key,type,ival,rval,tval FROM prop WHERE block_id=?" },
        { "insertProp",
            "INSERT INTO prop(block_id,key,type,ival,rval,tval) VALUES(?,?,?,?,?,?) "
            "ON CONFLICT(block_id,key) DO UPDATE SET type=excluded.type,ival=excluded.ival,rval=excluded.rval,tval=excluded.tval" },
        { "deleteProp", "DELETE FROM prop WHERE block_id=? AND key=?" },
        { "queryPropText",
            "SELECT DISTINCT b.id FROM block b JOIN prop p ON p.block_id=b.id "
            "WHERE p.key=? AND p.tval=? AND (b.state & 24)=0 ORDER BY b.id LIMIT ?" },
        { "queryPropInt",
            "SELECT DISTINCT b.id FROM block b JOIN prop p ON p.block_id=b.id "
            "WHERE p.key=? AND p.ival>=? AND p.ival<=? AND (b.state & 24)=0 ORDER BY b.id LIMIT ?" },
        { "queryPropIntUnder",
            "SELECT DISTINCT b.id FROM block b JOIN prop p ON p.block_id=b.id "
            "WHERE p.key=? AND p.ival>=? AND p.ival<=? AND b.parent=? AND (b.state & 24)=0 ORDER BY b.id LIMIT ?" },
        { "insertDict", "INSERT INTO dict(name) VALUES(?) ON CONFLICT(name) DO NOTHING" },
        { "getDictId", "SELECT id FROM dict WHERE name=?" },
        { "getDictName", "SELECT name FROM dict WHERE id=?" },
        { "insertLink", "INSERT OR IGNORE INTO link(src,dst,kind) VALUES(?,?,?)" },
        { "deleteLink", "DELETE FROM link WHERE src=? AND dst=? AND kind=?" },
        { "linksBySrc", "SELECT dst FROM link WHERE src=? AND (?=-1 OR kind=?) ORDER BY dst" },
        { "linksByDst", "SELECT src FROM link WHERE dst=? AND (?=-1 OR kind=?) ORDER BY src" },
        { "getMeta", "SELECT value FROM meta WHERE key=?" },
        { "setMeta", "INSERT INTO meta(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value" },
        { "delMeta", "DELETE FROM meta WHERE key=?" },
    });
}

PreparedStatements::PreparedStatements(SQLite::Database& db) : mDb(db) {}

SQLite::Statement& PreparedStatements::ensure(std::string_view name, std::string_view sql) {
    auto key = std::string(name);
    auto it = mStatements.find(key);
    if (it != mStatements.end())
        return *it->second;

    auto stmt = std::make_unique<SQLite::Statement>(mDb, std::string(sql));
    auto& ref = *stmt;
    mStatements.emplace(std::move(key), std::move(stmt));
    return ref;
}

SQLite::Statement& PreparedStatements::get(std::string_view name) {
    for (const auto& [n, sql] : kCatalog)
        if (n == name)
            return this->ensure(n, sql);
    return this->ensure(name, "");
}

void PreparedStatements::resetAll() {
    for (auto& [name, stmt] : mStatements)
        stmt->reset();
}