#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Cache.h"
#include "LOICollectionA/base/Macro.h"
#include "LOICollectionA/data/sqlite/block/Payload.h"

using LOICollection::PropKey;

namespace SQLite {
    class Statement;
}

class ConnectionPool;
class SQLiteConnection;

using BlockId = std::int64_t;

inline constexpr std::int32_t kTypedRowKind = 1000000000;

enum class BlockLifecycle : std::uint32_t {
    None = 0,
    Active = 1u << 0,
    Frozen = 1u << 1,
    Sealed = 1u << 2,
    Archived = 1u << 3,
    Deleted = 1u << 4
};

struct BlockProp {
    PropKey key = 0;
    PayloadType type = PayloadType::Null;
    std::int64_t intValue = 0;
    double realValue = 0.0;
    std::string textValue;
};

struct PropMatch {
    PropKey key = 0;
    std::string value;
};

struct BlockView {
    BlockId id = 0;
    BlockId parent = 0;
    std::int32_t kind = 0;
    std::string_view name;
    BlockLifecycle state = BlockLifecycle::None;
    std::int64_t created = 0;
    std::int64_t updated = 0;
    std::string_view payload;
};

struct BlockRecord {
    BlockId id = 0;
    BlockId parent = 0;
    std::int32_t kind = 0;
    std::string name;
    BlockLifecycle state = BlockLifecycle::None;
    std::int64_t created = 0;
    std::int64_t updated = 0;
    std::vector<std::byte> payload;
    std::vector<BlockProp> props;
};

LOICOLLECTION_A_NDAPI BlockRecord materialize(BlockView const& view);

class BlockStore {
public:
    LOICOLLECTION_A_NDAPI static ll::Expected<std::unique_ptr<BlockStore>> create(
        std::shared_ptr<ConnectionPool> pool);

    LOICOLLECTION_A_API ~BlockStore();

    LOICOLLECTION_A_NDAPI ll::Expected<std::optional<std::string>> metaGet(std::string_view key);
    LOICOLLECTION_A_NDAPI ll::Expected<void> metaSet(std::string_view key, std::string_view value);
    LOICOLLECTION_A_NDAPI ll::Expected<void> metaDel(std::string_view key);

    BlockStore(BlockStore const&) = delete;
    BlockStore& operator=(BlockStore const&) = delete;

    LOICOLLECTION_A_NDAPI ll::Expected<BlockId> createBlock(
        BlockId parent, std::int32_t kind, std::string_view name, std::string_view payload = {});

    LOICOLLECTION_A_NDAPI ll::Expected<BlockId> upsertRow(
        BlockId parent, std::string_view name, std::string_view payload = {});

    LOICOLLECTION_A_NDAPI ll::Expected<BlockRecord> load(BlockId id);
    LOICOLLECTION_A_NDAPI ll::Expected<BlockRecord> load(BlockId parent, std::string_view name);

    LOICOLLECTION_A_NDAPI ll::Expected<std::optional<BlockId>> idOf(BlockId parent, std::string_view name);

    LOICOLLECTION_A_NDAPI ll::Expected<void> withBlock(BlockId id, std::function<void(BlockView const&)> const& consumer);

    LOICOLLECTION_A_NDAPI ll::Expected<void> setPayload(BlockId id, std::string_view payload);
    LOICOLLECTION_A_NDAPI ll::Expected<void> remove(BlockId id);
    LOICOLLECTION_A_NDAPI ll::Expected<void> control(BlockId id, BlockLifecycle to);
    LOICOLLECTION_A_NDAPI ll::Expected<BlockLifecycle> stateOf(BlockId id);

    LOICOLLECTION_A_NDAPI ll::Expected<std::vector<BlockId>> children(BlockId parent, std::int32_t kind = -1, size_t limit = 0);
    LOICOLLECTION_A_NDAPI ll::Expected<std::vector<BlockRecord>> records(BlockId parent, std::int32_t kind = -1, size_t limit = 0);
    LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::pair<BlockId, std::string>>> childNames(BlockId parent);
    LOICOLLECTION_A_NDAPI ll::Expected<std::vector<BlockRecord>> rowsByIds(
        std::span<const BlockId> ids, bool withProps = false);

    LOICOLLECTION_A_NDAPI ll::Expected<void> setProp(BlockId id, PropKey key, std::int64_t value);
    LOICOLLECTION_A_NDAPI ll::Expected<void> setProp(BlockId id, PropKey key, double value);
    LOICOLLECTION_A_NDAPI ll::Expected<void> setProp(BlockId id, PropKey key, std::string_view value);
    LOICOLLECTION_A_NDAPI ll::Expected<void> setProp(
        BlockId id, PropKey key, PayloadType type, std::int64_t ival, double rval, std::string_view tval);

    LOICOLLECTION_A_NDAPI ll::Expected<std::vector<BlockId>> queryInt(PropKey key, std::int64_t min, std::int64_t max, size_t limit = 0);
    LOICOLLECTION_A_NDAPI ll::Expected<std::vector<BlockId>> queryInt(
        BlockId parent, PropKey key, std::int64_t min, std::int64_t max, size_t limit = 0);
    LOICOLLECTION_A_NDAPI ll::Expected<std::vector<BlockId>> queryText(PropKey key, std::string_view value, size_t limit = 0);
    LOICOLLECTION_A_NDAPI ll::Expected<std::vector<BlockId>> queryText(
        BlockId parent, PropKey key, std::string_view value, size_t limit = 0);
    LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::pair<BlockId, std::string>>> queryTextNames(
        BlockId parent, PropKey key, std::string_view value, size_t limit = 0);
    LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::pair<BlockId, std::string>>> queryTextNamesAll(
        BlockId parent, std::vector<PropMatch> const& conds, size_t limit = 0);

    LOICOLLECTION_A_NDAPI ll::Expected<void> exec(std::string_view sql);

    LOICOLLECTION_A_NDAPI ll::Expected<void> withQuery(
        std::string_view key,
        std::string_view sql,
        std::span<const BlockProp> params,
        std::function<void(SQLite::Statement&)> const& consumer);

    LOICOLLECTION_A_NDAPI ll::Expected<void> link(BlockId src, BlockId dst, std::int32_t kind);
    LOICOLLECTION_A_NDAPI ll::Expected<void> unlink(BlockId src, BlockId dst, std::int32_t kind);
    LOICOLLECTION_A_NDAPI ll::Expected<std::vector<BlockId>> links(BlockId src, std::int32_t kind = -1);
    LOICOLLECTION_A_NDAPI ll::Expected<std::vector<BlockId>> backlinks(BlockId dst, std::int32_t kind = -1);

    LOICOLLECTION_A_NDAPI ll::Expected<std::int32_t> intern(std::string_view name);
    LOICOLLECTION_A_NDAPI ll::Expected<std::string> unintern(std::int32_t id);

    friend class WriteBatch;

private:
    LOICOLLECTION_A_API explicit BlockStore(std::shared_ptr<ConnectionPool> pool);

    LOICOLLECTION_A_NDAPI ll::Expected<std::shared_ptr<SQLiteConnection>> acquire(int timeout = 5000);
    LOICOLLECTION_A_API void release(std::shared_ptr<SQLiteConnection> conn);

    LOICOLLECTION_A_NDAPI ll::Expected<void> ensureSchema(SQLiteConnection& conn);

    LOICOLLECTION_A_NDAPI ll::Expected<void> implCreateBlock(
        SQLiteConnection& conn, BlockId parent, std::int32_t kind, std::string_view name,
        std::string_view payload, BlockId& id);
    LOICOLLECTION_A_NDAPI ll::Expected<BlockId> implUpsertRow(
        SQLiteConnection& conn, BlockId parent, std::string_view name, std::string_view payload);
    LOICOLLECTION_A_NDAPI ll::Expected<void> implSetPayload(SQLiteConnection& conn, BlockId id, std::string_view payload);
    LOICOLLECTION_A_NDAPI ll::Expected<void> implControl(SQLiteConnection& conn, BlockId id, BlockLifecycle to);
    LOICOLLECTION_A_NDAPI ll::Expected<void> implSetProp(SQLiteConnection& conn, BlockId id, PropKey key, PayloadType type, std::int64_t ival, double rval, std::string_view tval);
    LOICOLLECTION_A_NDAPI ll::Expected<void> implLink(SQLiteConnection& conn, BlockId src, BlockId dst, std::int32_t kind);
    LOICOLLECTION_A_NDAPI ll::Expected<void> implUnlink(SQLiteConnection& conn, BlockId src, BlockId dst, std::int32_t kind);

    LOICOLLECTION_A_NDAPI ll::Expected<BlockRecord> readBlock(SQLiteConnection& conn, BlockId id);
    LOICOLLECTION_A_NDAPI ll::Expected<std::int32_t> resolveKey(SQLiteConnection& conn, std::string_view name);

    std::shared_ptr<ConnectionPool> mPool;
    LRUCache<BlockId, BlockRecord> mBlockCache{2048};
    LRUKCache<std::string, BlockId> mNameCache{2048, 512, 2};
};