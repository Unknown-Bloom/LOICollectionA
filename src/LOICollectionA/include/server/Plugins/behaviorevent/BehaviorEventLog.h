#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Ownership.h"
#include "LOICollectionA/data/sqlite/block/BlockStore.h"

class WriteBatch;

class BehaviorEventLog {
public:
    using Row = std::unordered_map<std::string, std::string>;
    using Rows = std::unordered_map<std::string, Row>;

    struct PreparedEvent {
        std::string name;
        std::string type;
        std::int64_t timestamp = 0;
        std::int64_t actor = 0;
        std::int64_t posX = 0;
        std::int64_t posY = 0;
        std::int64_t posZ = 0;
        std::int64_t dimension = 0;
        std::vector<std::pair<std::string, std::string>> fields;
    };

    LOICOLLECTION_A_API ~BehaviorEventLog();

    BehaviorEventLog(BehaviorEventLog const&) = delete;
    BehaviorEventLog& operator=(BehaviorEventLog const&) = delete;

    [[nodiscard]] LOICOLLECTION_A_NDAPI static ll::Expected<std::unique_ptr<BehaviorEventLog>> create(
        BlockStore& store, std::string_view rootName);

    [[nodiscard]] ll::Expected<BlockId> append(PreparedEvent const& event);
    [[nodiscard]] ll::Expected<std::vector<BlockId>> appendMany(std::span<const PreparedEvent> events);

    [[nodiscard]] ll::Expected<Row> read(BlockId id);
    [[nodiscard]] ll::Expected<Rows> read(std::span<const BlockId> ids);

    [[nodiscard]] ll::Expected<std::vector<BlockId>> all(size_t limit = 0);
    [[nodiscard]] ll::Expected<size_t> count();
    [[nodiscard]] ll::Expected<std::vector<BlockId>> byTimeRange(
        std::int64_t from, std::int64_t to, size_t limit = 0);
    [[nodiscard]] ll::Expected<std::vector<BlockId>> byName(std::string_view name, size_t limit = 0);
    [[nodiscard]] ll::Expected<std::vector<BlockId>> byType(std::string_view type, size_t limit = 0);
    [[nodiscard]] ll::Expected<std::vector<BlockId>> byActor(std::int64_t actor, size_t limit = 0);
    [[nodiscard]] ll::Expected<std::vector<BlockId>> byDimension(std::int64_t dimension, size_t limit = 0);
    [[nodiscard]] ll::Expected<std::vector<BlockId>> byPosition(
        std::int64_t x, std::int64_t y, std::int64_t z, size_t limit = 0);

    [[nodiscard]] ll::Expected<void> erase(std::span<const BlockId> ids);
    [[nodiscard]] ll::Expected<size_t> archiveBefore(std::int64_t timestamp);

    [[nodiscard]] BlockId root() const noexcept { return mRoot; }

private:
    struct Encoded {
        std::int32_t kind = 0;
        std::int64_t type = 0;
        std::string payload;
    };

    explicit BehaviorEventLog(observer<BlockStore> store);

    [[nodiscard]] ll::Expected<std::int32_t> intern(std::string_view name);
    [[nodiscard]] ll::Expected<std::string> unintern(std::int32_t id);

    [[nodiscard]] ll::Expected<Encoded> encode(PreparedEvent const& event);
    [[nodiscard]] ll::Expected<BlockId> writeEvent(
        WriteBatch& batch, PreparedEvent const& event, Encoded const& encoded);
    [[nodiscard]] ll::Expected<Row> decode(BlockRecord const& record);

    observer<BlockStore> mStore = nullptr;
    BlockId mRoot = 0;
    std::mutex mDictMutex;
    std::unordered_map<std::string, std::int32_t> mDictIds;
    std::unordered_map<std::int32_t, std::string> mDictNames;
    PropKey mKeyTimestamp = 0;
    PropKey mKeyType = 0;
    PropKey mKeyActor = 0;
    PropKey mKeyDim = 0;
    PropKey mKeyPosX = 0;
    PropKey mKeyPosY = 0;
    PropKey mKeyPosZ = 0;
};
