#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Ownership.h"
#include "LOICollectionA/data/sqlite/block/BlockStore.h"

class WriteBatch;

class EventLog {
public:
    struct PreparedEvent {
        std::int32_t kind = 0;      // event_name，interned
        std::int64_t timestamp = 0; // 时间戳（秒），下推查询用
        std::int64_t type = 0;      // event_type，interned（Normal/Operable）
        std::int64_t actor = 0;     // 相关玩家块 id（0 = 无）
        std::int64_t posX = 0;
        std::int64_t posY = 0;
        std::int64_t posZ = 0;
        std::int64_t dimension = 0;
        std::string payload;        // TLV 扩展字段（含 event_time 展示串）
    };

    LOICOLLECTION_A_API ~EventLog();

    EventLog(EventLog const&) = delete;
    EventLog& operator=(EventLog const&) = delete;

    [[nodiscard]] LOICOLLECTION_A_NDAPI static ll::Expected<std::unique_ptr<EventLog>> create(
        BlockStore& store, std::string_view rootName);

    [[nodiscard]] ll::Expected<BlockId> append(PreparedEvent const& event);
    [[nodiscard]] ll::Expected<std::vector<BlockId>> appendMany(
        std::span<const PreparedEvent> events);

    [[nodiscard]] ll::Expected<std::vector<BlockId>> byTimeRange(
        std::int64_t from, std::int64_t to, size_t limit = 0);
    [[nodiscard]] ll::Expected<std::vector<BlockId>> byName(
        std::int32_t kind, size_t limit = 0);
    [[nodiscard]] ll::Expected<std::vector<BlockId>> byType(
        std::int64_t type, size_t limit = 0);
    [[nodiscard]] ll::Expected<std::vector<BlockId>> byDimension(
        std::int64_t dimension, size_t limit = 0);
    [[nodiscard]] ll::Expected<std::vector<BlockId>> byActor(
        std::int64_t actor, size_t limit = 0);

    [[nodiscard]] ll::Expected<std::vector<std::byte>> readPayload(BlockId id);

    [[nodiscard]] ll::Expected<size_t> sealBefore(std::int64_t timestamp);
    [[nodiscard]] ll::Expected<size_t> archiveBefore(std::int64_t timestamp);

    [[nodiscard]] BlockId root() const noexcept { return mRoot; }

private:
    explicit EventLog(observer<BlockStore> store);

    [[nodiscard]] ll::Expected<void> writeProps(WriteBatch& batch, BlockId id, PreparedEvent const& event);

    observer<BlockStore> mStore = nullptr;
    BlockId mRoot = 0;
    PropKey mKeyTimestamp = 0;
    PropKey mKeyType = 0;
    PropKey mKeyActor = 0;
    PropKey mKeyDim = 0;
    PropKey mKeyPosX = 0;
    PropKey mKeyPosY = 0;
    PropKey mKeyPosZ = 0;
};