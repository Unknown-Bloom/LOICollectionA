#include "LOICollectionA/data/sqlite/block/BlockRepository.h"

#include "LOICollectionA/data/sqlite/connection/ConnectionPool.h"

BlockRepository::BlockRepository(std::shared_ptr<BlockStore> store) : mStore(std::move(store)) {}

BlockRepository::~BlockRepository() = default;

ll::Expected<std::shared_ptr<BlockRepository>> BlockRepository::open(
    std::string dbPath, size_t connections) {
    auto pool = std::make_shared<ConnectionPool>(std::move(dbPath), connections);
    auto store = BlockStore::create(pool);
    if (!store)
        return ll::makeStringError(store.error().message());
    return std::shared_ptr<BlockRepository>(new BlockRepository(std::move(*store)));
}

ll::Expected<std::shared_ptr<BlockRepository>> BlockRepository::fromStore(
    std::shared_ptr<BlockStore> store) {
    if (!store)
        return ll::makeStringError("null block store");
    return std::shared_ptr<BlockRepository>(new BlockRepository(std::move(store)));
}

ll::Expected<std::optional<std::string>> BlockRepository::metaGet(std::string_view key) {
    return mStore->metaGet(key);
}

ll::Expected<void> BlockRepository::metaSet(std::string_view key, std::string_view value) {
    return mStore->metaSet(key, value);
}

ll::Expected<void> BlockRepository::metaDel(std::string_view key) {
    return mStore->metaDel(key);
}

ll::Expected<void> BlockRepository::exec(std::string_view sql) {
    return mStore->exec(sql);
}
