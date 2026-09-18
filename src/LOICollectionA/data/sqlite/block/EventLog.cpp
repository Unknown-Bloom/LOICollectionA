#include "LOICollectionA/data/sqlite/block/EventLog.h"

#include <limits>
#include <memory>
#include <utility>

#include "LOICollectionA/data/sqlite/block/WriteBatch.h"

ll::Expected<std::unique_ptr<EventLog>> EventLog::create(BlockStore& store, std::string_view rootName) {
    auto log = std::unique_ptr<EventLog>(new EventLog(&store));

    auto root = store.load(0, rootName);
    if (root && root.value().id != 0) {
        log->mRoot = root.value().id;
    } else {
        auto created = store.createBlock(0, 0, rootName);
        if (!created)
            return ll::makeStringError(created.error().message());
        log->mRoot = *created;
    }

    auto intern = [&](std::string_view name) -> ll::Expected<PropKey> { return store.intern(name); };
    auto ts = intern("event_timestamp");
    auto ty = intern("event_type");
    auto ac = intern("event_actor");
    auto dm = intern("event_dimension");
    auto px = intern("event_pos_x");
    auto py = intern("event_pos_y");
    auto pz = intern("event_pos_z");

    if (!ts || !ty || !ac || !dm || !px || !py || !pz)
        return ll::makeStringError("intern event props failed");

    log->mKeyTimestamp = *ts;
    log->mKeyType = *ty;
    log->mKeyActor = *ac;
    log->mKeyDim = *dm;
    log->mKeyPosX = *px;
    log->mKeyPosY = *py;
    log->mKeyPosZ = *pz;

    return log;
}

EventLog::EventLog(observer<BlockStore> store) : mStore(store) {}

EventLog::~EventLog() = default;

ll::Expected<void> EventLog::writeProps(WriteBatch& batch, BlockId id, PreparedEvent const& event) {
    if (event.timestamp) {
        if (auto r = batch.setProp(id, mKeyTimestamp, event.timestamp); !r)
            return r;
    }
    if (event.type) {
        if (auto r = batch.setProp(id, mKeyType, event.type); !r)
            return r;
    }
    if (event.actor) {
        if (auto r = batch.setProp(id, mKeyActor, event.actor); !r)
            return r;
    }
    if (auto r = batch.setProp(id, mKeyDim, event.dimension); !r)
        return r;
    if (auto r = batch.setProp(id, mKeyPosX, event.posX); !r)
        return r;
    if (auto r = batch.setProp(id, mKeyPosY, event.posY); !r)
        return r;
    if (auto r = batch.setProp(id, mKeyPosZ, event.posZ); !r)
        return r;
    return {};
}

ll::Expected<BlockId> EventLog::append(PreparedEvent const& event) {
    auto batch = WriteBatch::begin(*mStore);
    if (!batch)
        return ll::makeStringError(batch.error().message());

    auto id = (*batch)->append(mRoot, event.kind, "", event.payload);
    if (!id)
        return ll::makeStringError(id.error().message());
    if (auto r = this->writeProps(**batch, *id, event); !r)
        return ll::makeStringError(r.error().message());

    auto ok = (*batch)->commit();
    if (!ok)
        return ll::makeStringError(ok.error().message());
    return *id;
}

ll::Expected<std::vector<BlockId>> EventLog::appendMany(std::span<const PreparedEvent> events) {
    auto batch = WriteBatch::begin(*mStore);
    if (!batch)
        return ll::makeStringError(batch.error().message());

    std::vector<BlockId> ids;
    ids.reserve(events.size());
    for (auto& e : events) {
        auto id = (*batch)->append(mRoot, e.kind, "", e.payload);
        if (!id)
            return ll::makeStringError(id.error().message());
        if (auto r = this->writeProps(**batch, *id, e); !r)
            return ll::makeStringError(r.error().message());
        ids.emplace_back(*id);
    }

    auto ok = (*batch)->commit();
    if (!ok)
        return ll::makeStringError(ok.error().message());
    return ids;
}

ll::Expected<std::vector<BlockId>> EventLog::byTimeRange(std::int64_t from, std::int64_t to, size_t limit) {
    return mStore->queryInt(mKeyTimestamp, from, to, limit);
}

ll::Expected<std::vector<BlockId>> EventLog::byName(std::int32_t kind, size_t limit) {
    return mStore->children(mRoot, kind, limit);
}

ll::Expected<std::vector<BlockId>> EventLog::byType(std::int64_t type, size_t limit) {
    return mStore->queryInt(mKeyType, type, type, limit);
}

ll::Expected<std::vector<BlockId>> EventLog::byDimension(std::int64_t dimension, size_t limit) {
    return mStore->queryInt(mKeyDim, dimension, dimension, limit);
}

ll::Expected<std::vector<BlockId>> EventLog::byActor(std::int64_t actor, size_t limit) {
    return mStore->queryInt(mKeyActor, actor, actor, limit);
}

ll::Expected<std::vector<std::byte>> EventLog::readPayload(BlockId id) {
    auto rec = mStore->load(id);
    if (!rec)
        return ll::makeStringError(rec.error().message());
    return rec.value().payload;
}

ll::Expected<size_t> EventLog::sealBefore(std::int64_t timestamp) {
    auto ids = mStore->queryInt(mKeyTimestamp, std::numeric_limits<std::int64_t>::min(), timestamp);
    if (!ids)
        return ll::makeStringError(ids.error().message());

    size_t n = 0;
    for (auto id : *ids) {
        if (auto r = mStore->control(id, BlockState::Sealed); !r)
            return ll::makeStringError(r.error().message());
        ++n;
    }
    return n;
}

ll::Expected<size_t> EventLog::archiveBefore(std::int64_t timestamp) {
    auto ids = mStore->queryInt(mKeyTimestamp, std::numeric_limits<std::int64_t>::min(), timestamp);
    if (!ids)
        return ll::makeStringError(ids.error().message());

    size_t n = 0;
    for (auto id : *ids) {
        if (auto r = mStore->control(id, BlockState::Archived); !r)
            return ll::makeStringError(r.error().message());
        ++n;
    }
    return n;
}