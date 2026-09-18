#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"
#include "LOICollectionA/data/sqlite/block/Payload.h"

using LOICollection::PropKey;

class ConnectionPool;
class SQLiteConnection;

using BlockId = std::int64_t;

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

[[nodiscard]] BlockRecord materialize(BlockView const& view);

class BlockStore {
public:
    [[nodiscard]] LOICOLLECTION_A_NDAPI static ll::Expected<std::unique_ptr<BlockStore>> create(
        std::shared_ptr<ConnectionPool> pool);

    LOICOLLECTION_A_API ~BlockStore();

    BlockStore(BlockStore const&) = delete;
    BlockStore& operator=(BlockStore const&) = delete;

    [[nodiscard]] ll::Expected<BlockId> createBlock(
        BlockId parent, std::int32_t kind, std::string_view name, std::string_view payload = {});

    [[nodiscard]] ll::Expected<BlockRecord> load(BlockId id);
    [[nodiscard]] ll::Expected<BlockRecord> load(BlockId parent, std::string_view name);

    [[nodiscard]] ll::Expected<void> withBlock(BlockId id, std::function<void(BlockView const&)> const& consumer);

    [[nodiscard]] ll::Expected<void> setPayload(BlockId id, std::string_view payload);
    [[nodiscard]] ll::Expected<void> remove(BlockId id);
    [[nodiscard]] ll::Expected<void> control(BlockId id, BlockLifecycle to);
    [[nodiscard]] ll::Expected<BlockLifecycle> stateOf(BlockId id);

    [[nodiscard]] ll::Expected<std::vector<BlockId>> children(BlockId parent, std::int32_t kind = -1, size_t limit = 0);
    [[nodiscard]] ll::Expected<std::vector<BlockRecord>> records(BlockId parent, std::int32_t kind = -1, size_t limit = 0);

    [[nodiscard]] ll::Expected<void> setProp(BlockId id, PropKey key, std::int64_t value);
    [[nodiscard]] ll::Expected<void> setProp(BlockId id, PropKey key, double value);
    [[nodiscard]] ll::Expected<void> setProp(BlockId id, PropKey key, std::string_view value);
    [[nodiscard]] ll::Expected<void> setProp(
        BlockId id, PropKey key, PayloadType type, std::int64_t ival, double rval, std::string_view tval);

    [[nodiscard]] ll::Expected<std::vector<BlockId>> queryInt(PropKey key, std::int64_t min, std::int64_t max, size_t limit = 0);
    [[nodiscard]] ll::Expected<std::vector<BlockId>> queryInt(
        BlockId parent, PropKey key, std::int64_t min, std::int64_t max, size_t limit = 0);
    [[nodiscard]] ll::Expected<std::vector<BlockId>> queryText(PropKey key, std::string_view value, size_t limit = 0);

    [[nodiscard]] ll::Expected<void> link(BlockId src, BlockId dst, std::int32_t kind);
    [[nodiscard]] ll::Expected<void> unlink(BlockId src, BlockId dst, std::int32_t kind);
    [[nodiscard]] ll::Expected<std::vector<BlockId>> links(BlockId src, std::int32_t kind = -1);
    [[nodiscard]] ll::Expected<std::vector<BlockId>> backlinks(BlockId dst, std::int32_t kind = -1);

    [[nodiscard]] ll::Expected<std::int32_t> intern(std::string_view name);
    [[nodiscard]] ll::Expected<std::string> unintern(std::int32_t id);

    friend class WriteBatch;

private:
    explicit BlockStore(std::shared_ptr<ConnectionPool> pool);

    [[nodiscard]] ll::Expected<std::shared_ptr<SQLiteConnection>> acquire(int timeout = 5000);
    void release(std::shared_ptr<SQLiteConnection> conn);

    [[nodiscard]] ll::Expected<void> ensureSchema(SQLiteConnection& conn);

    [[nodiscard]] ll::Expected<void> implCreateBlock(
        SQLiteConnection& conn, BlockId parent, std::int32_t kind, std::string_view name,
        std::string_view payload, BlockId& id);
    [[nodiscard]] ll::Expected<void> implSetPayload(SQLiteConnection& conn, BlockId id, std::string_view payload);
    [[nodiscard]] ll::Expected<void> implControl(SQLiteConnection& conn, BlockId id, BlockLifecycle to);
    [[nodiscard]] ll::Expected<void> implSetProp(SQLiteConnection& conn, BlockId id, PropKey key, PayloadType type, std::int64_t ival, double rval, std::string_view tval);
    [[nodiscard]] ll::Expected<void> implLink(SQLiteConnection& conn, BlockId src, BlockId dst, std::int32_t kind);
    [[nodiscard]] ll::Expected<void> implUnlink(SQLiteConnection& conn, BlockId src, BlockId dst, std::int32_t kind);

    [[nodiscard]] ll::Expected<BlockRecord> readBlock(SQLiteConnection& conn, BlockId id);
    [[nodiscard]] ll::Expected<std::int32_t> resolveKey(SQLiteConnection& conn, std::string_view name);

    std::shared_ptr<ConnectionPool> mPool;
};