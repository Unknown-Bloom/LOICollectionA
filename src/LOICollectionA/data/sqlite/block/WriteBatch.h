#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"
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

    LOICOLLECTION_A_NDAPI static ll::Expected<std::unique_ptr<WriteBatch>> begin(
        BlockStore& store);

    LOICOLLECTION_A_NDAPI ll::Expected<BlockId> append(
        BlockId parent, std::int32_t kind, std::string_view name, std::string_view payload = {});

    LOICOLLECTION_A_NDAPI ll::Expected<std::vector<BlockId>> appendMany(
        BlockId parent,
        std::int32_t kind,
        std::span<const std::pair<std::string_view, std::string_view>> namePayloads);

    LOICOLLECTION_A_NDAPI ll::Expected<BlockId> upsertRow(
        BlockId parent, std::string_view name, std::string_view payload = {});

    LOICOLLECTION_A_NDAPI ll::Expected<void> setPayload(BlockId id, std::string_view payload);

    LOICOLLECTION_A_NDAPI ll::Expected<void> control(BlockId id, BlockLifecycle to);

    LOICOLLECTION_A_NDAPI ll::Expected<void> setProp(BlockId id, PropKey key, std::int64_t value);
    LOICOLLECTION_A_NDAPI ll::Expected<void> setProp(BlockId id, PropKey key, double value);
    LOICOLLECTION_A_NDAPI ll::Expected<void> setProp(BlockId id, PropKey key, std::string_view value);
    LOICOLLECTION_A_NDAPI ll::Expected<void> setProp(
        BlockId id, PropKey key, PayloadType type,
        std::int64_t ival, double rval, std::string_view tval);

    LOICOLLECTION_A_NDAPI ll::Expected<void> exec(std::string_view sql);

    LOICOLLECTION_A_NDAPI ll::Expected<void> execCells(
        std::string_view key, std::string_view sql, std::span<const BlockProp> params);

    LOICOLLECTION_A_NDAPI ll::Expected<std::int32_t> intern(std::string_view name);

    LOICOLLECTION_A_NDAPI ll::Expected<void> link(BlockId src, BlockId dst, std::int32_t kind);

    LOICOLLECTION_A_NDAPI ll::Expected<bool> commit();
    LOICOLLECTION_A_NDAPI ll::Expected<bool> rollback();

private:
    LOICOLLECTION_A_API explicit WriteBatch(BlockStore& store, std::unique_ptr<StorageTransaction> txn);

    observer<BlockStore> mStore = nullptr;
    std::unique_ptr<StorageTransaction> mTxn;
    bool mFinished = false;
};
