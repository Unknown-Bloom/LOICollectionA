#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Ownership.h"
#include "LOICollectionA/data/sqlite/block/BlockStore.h"
#include "LOICollectionA/data/sqlite/block/Payload.h"

class StorageTransaction;

class WriteBatch {
public:
    LOICOLLECTION_A_API ~WriteBatch();

    WriteBatch(WriteBatch const&) = delete;
    WriteBatch& operator=(WriteBatch const&) = delete;
    WriteBatch(WriteBatch&&) = delete;
    WriteBatch& operator=(WriteBatch&&) = delete;

    [[nodiscard]] LOICOLLECTION_A_NDAPI static ll::Expected<std::unique_ptr<WriteBatch>> begin(
        BlockStore& store);

    [[nodiscard]] ll::Expected<BlockId> append(
        BlockId parent, std::int32_t kind, std::string_view name, std::string_view payload = {});

    [[nodiscard]] ll::Expected<std::vector<BlockId>> appendMany(
        BlockId parent,
        std::int32_t kind,
        std::span<const std::pair<std::string_view, std::string_view>> namePayloads);

    [[nodiscard]] ll::Expected<void> setPayload(BlockId id, std::string_view payload);

    [[nodiscard]] ll::Expected<void> control(BlockId id, BlockLifecycle to);

    [[nodiscard]] ll::Expected<void> setProp(BlockId id, PropKey key, std::int64_t value);
    [[nodiscard]] ll::Expected<void> setProp(BlockId id, PropKey key, double value);
    [[nodiscard]] ll::Expected<void> setProp(BlockId id, PropKey key, std::string_view value);

    [[nodiscard]] ll::Expected<std::int32_t> intern(std::string_view name);

    [[nodiscard]] ll::Expected<void> link(BlockId src, BlockId dst, std::int32_t kind);

    [[nodiscard]] ll::Expected<bool> commit();
    [[nodiscard]] ll::Expected<bool> rollback();

private:
    explicit WriteBatch(BlockStore& store, std::unique_ptr<StorageTransaction> txn);

    observer<BlockStore> mStore = nullptr;
    std::unique_ptr<StorageTransaction> mTxn;
    bool mFinished = false;
};
