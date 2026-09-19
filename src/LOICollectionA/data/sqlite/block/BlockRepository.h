#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"
#include "LOICollectionA/data/sqlite/block/BlockStore.h"

class BlockRepository {
public:
    LOICOLLECTION_A_API ~BlockRepository();

    BlockRepository(BlockRepository const&) = delete;
    BlockRepository& operator=(BlockRepository const&) = delete;

    [[nodiscard]] LOICOLLECTION_A_NDAPI static ll::Expected<std::shared_ptr<BlockRepository>> open(
        std::string dbPath, size_t connections = 4);

    [[nodiscard]] LOICOLLECTION_A_NDAPI static ll::Expected<std::shared_ptr<BlockRepository>> fromStore(
        std::shared_ptr<BlockStore> store);

    [[nodiscard]] BlockStore& store() noexcept { return *mStore; }

    [[nodiscard]] ll::Expected<std::optional<std::string>> metaGet(std::string_view key);
    [[nodiscard]] ll::Expected<void> metaSet(std::string_view key, std::string_view value);
    [[nodiscard]] ll::Expected<void> metaDel(std::string_view key);

private:
    explicit BlockRepository(std::shared_ptr<BlockStore> store);

    std::shared_ptr<BlockStore> mStore = nullptr;
};
