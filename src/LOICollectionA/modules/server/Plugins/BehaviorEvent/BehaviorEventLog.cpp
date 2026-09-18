#include "LOICollectionA/include/server/Plugins/BehaviorEvent/BehaviorEventLog.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>

#include "LOICollectionA/data/sqlite/block/Payload.h"
#include "LOICollectionA/data/sqlite/block/WriteBatch.h"

namespace {
    void keepCommon(std::vector<BlockId>& target, std::vector<BlockId> const& other) {
        std::unordered_set<BlockId> keep(other.begin(), other.end());
        std::erase_if(target, [&keep](BlockId id) -> bool { return !keep.contains(id); });
    }
}

ll::Expected<std::unique_ptr<BehaviorEventLog>> BehaviorEventLog::create(BlockStore& store, std::string_view rootName) {
    auto log = std::unique_ptr<BehaviorEventLog>(new BehaviorEventLog(&store));

    auto root = store.load(0, rootName);
    if (root && root.value().id != 0) {
        log->mRoot = root.value().id;
    } else {
        auto created = store.createBlock(0, 0, rootName);
        if (!created)
            return ll::makeStringError(created.error().message());
        log->mRoot = *created;
    }

    auto intern = [&log](std::string_view name) -> ll::Expected<PropKey> { return log->intern(name); };
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

BehaviorEventLog::BehaviorEventLog(observer<BlockStore> store) : mStore(store) {}

BehaviorEventLog::~BehaviorEventLog() = default;

ll::Expected<std::int32_t> BehaviorEventLog::intern(std::string_view name) {
    std::string key(name);

    {
        std::lock_guard<std::mutex> guard(mDictMutex);
        if (auto it = mDictIds.find(key); it != mDictIds.end())
            return it->second;
    }

    auto id = mStore->intern(name);
    if (!id)
        return ll::makeStringError(id.error().message());

    {
        std::lock_guard<std::mutex> guard(mDictMutex);
        mDictIds.emplace(key, *id);
        mDictNames.emplace(*id, key);
    }

    return *id;
}

ll::Expected<std::string> BehaviorEventLog::unintern(std::int32_t id) {
    {
        std::lock_guard<std::mutex> guard(mDictMutex);
        if (auto it = mDictNames.find(id); it != mDictNames.end())
            return it->second;
    }

    auto name = mStore->unintern(id);
    if (!name)
        return ll::makeStringError(name.error().message());

    {
        std::lock_guard<std::mutex> guard(mDictMutex);
        mDictIds.emplace(*name, id);
        mDictNames.emplace(id, *name);
    }

    return *name;
}

ll::Expected<BehaviorEventLog::Encoded> BehaviorEventLog::encode(PreparedEvent const& event) {
    Encoded encoded;

    auto kind = this->intern(event.name);
    if (!kind)
        return ll::makeStringError(kind.error().message());
    encoded.kind = *kind;

    if (!event.type.empty()) {
        auto type = this->intern(event.type);
        if (!type)
            return ll::makeStringError(type.error().message());
        encoded.type = *type;
    }

    PayloadWriter writer;
    for (auto const& [key, value] : event.fields) {
        auto field = this->intern(key);
        if (!field)
            return ll::makeStringError(field.error().message());
        writer.write(*field, std::string_view(value));
    }

    auto data = writer.data();
    encoded.payload.assign(reinterpret_cast<char const*>(data.data()), data.size());

    return encoded;
}

ll::Expected<BlockId> BehaviorEventLog::writeEvent(
    WriteBatch& batch, PreparedEvent const& event, Encoded const& encoded) {
    auto id = batch.append(mRoot, encoded.kind, "", encoded.payload);
    if (!id)
        return ll::makeStringError(id.error().message());

    if (event.timestamp) {
        if (auto r = batch.setProp(*id, mKeyTimestamp, event.timestamp); !r)
            return ll::makeStringError(r.error().message());
    }
    if (encoded.type) {
        if (auto r = batch.setProp(*id, mKeyType, encoded.type); !r)
            return ll::makeStringError(r.error().message());
    }
    if (event.actor) {
        if (auto r = batch.setProp(*id, mKeyActor, event.actor); !r)
            return ll::makeStringError(r.error().message());
    }

    if (auto r = batch.setProp(*id, mKeyDim, event.dimension); !r)
        return ll::makeStringError(r.error().message());
    if (auto r = batch.setProp(*id, mKeyPosX, event.posX); !r)
        return ll::makeStringError(r.error().message());
    if (auto r = batch.setProp(*id, mKeyPosY, event.posY); !r)
        return ll::makeStringError(r.error().message());
    if (auto r = batch.setProp(*id, mKeyPosZ, event.posZ); !r)
        return ll::makeStringError(r.error().message());

    return *id;
}

ll::Expected<BehaviorEventLog::Row> BehaviorEventLog::decode(BlockRecord const& record) {
    Row row;

    if (auto name = this->unintern(record.kind); name)
        row.emplace("event_name", *name);

    std::int64_t type = 0;
    std::int64_t posX = 0;
    std::int64_t posY = 0;
    std::int64_t posZ = 0;
    std::int64_t dimension = 0;

    for (auto const& prop : record.props) {
        if (prop.key == mKeyType)
            type = prop.intValue;
        else if (prop.key == mKeyPosX)
            posX = prop.intValue;
        else if (prop.key == mKeyPosY)
            posY = prop.intValue;
        else if (prop.key == mKeyPosZ)
            posZ = prop.intValue;
        else if (prop.key == mKeyDim)
            dimension = prop.intValue;
    }

    if (auto value = this->unintern(static_cast<std::int32_t>(type)); value)
        row.emplace("event_type", *value);

    row.emplace("position_x", std::to_string(posX));
    row.emplace("position_y", std::to_string(posY));
    row.emplace("position_z", std::to_string(posZ));
    row.emplace("position_dimension", std::to_string(dimension));

    PayloadReader reader(std::string_view(
        reinterpret_cast<char const*>(record.payload.empty() ? nullptr : record.payload.data()),
        record.payload.size()));
    reader.forEach([&](PayloadField const& field) -> void {
        if (field.type != PayloadType::Text)
            return;

        auto key = this->unintern(field.key);
        if (!key)
            return;

        row.insert_or_assign(*key, reader.materializeText(field));
    });

    return row;
}

ll::Expected<BlockId> BehaviorEventLog::append(PreparedEvent const& event) {
    auto encoded = this->encode(event);
    if (!encoded)
        return ll::makeStringError(encoded.error().message());

    auto batch = WriteBatch::begin(*mStore);
    if (!batch)
        return ll::makeStringError(batch.error().message());

    auto id = this->writeEvent(**batch, event, *encoded);
    if (!id)
        return ll::makeStringError(id.error().message());

    auto ok = (*batch)->commit();
    if (!ok)
        return ll::makeStringError(ok.error().message());

    return *id;
}

ll::Expected<std::vector<BlockId>> BehaviorEventLog::appendMany(std::span<const PreparedEvent> events) {
    std::vector<Encoded> encoded;
    encoded.reserve(events.size());

    for (auto const& event : events) {
        auto prepared = this->encode(event);
        if (!prepared)
            return ll::makeStringError(prepared.error().message());

        encoded.emplace_back(std::move(*prepared));
    }

    auto batch = WriteBatch::begin(*mStore);
    if (!batch)
        return ll::makeStringError(batch.error().message());

    std::vector<BlockId> ids;
    ids.reserve(events.size());

    for (size_t index = 0; index < events.size(); ++index) {
        auto id = this->writeEvent(**batch, events[index], encoded[index]);
        if (!id)
            return ll::makeStringError(id.error().message());

        ids.emplace_back(*id);
    }

    auto ok = (*batch)->commit();
    if (!ok)
        return ll::makeStringError(ok.error().message());

    return ids;
}

ll::Expected<BehaviorEventLog::Row> BehaviorEventLog::read(BlockId id) {
    auto record = mStore->load(id);
    if (!record)
        return Row{};

    if (record->state == BlockLifecycle::Deleted || record->state == BlockLifecycle::Archived)
        return Row{};

    return this->decode(*record);
}

ll::Expected<BehaviorEventLog::Rows> BehaviorEventLog::read(std::span<const BlockId> ids) {
    Rows rows;
    rows.reserve(ids.size());

    for (auto id : ids) {
        auto row = this->read(id);
        if (!row)
            return ll::makeStringError(row.error().message());

        if (!row->empty())
            rows.emplace(std::to_string(id), std::move(*row));
    }

    return rows;
}

ll::Expected<std::vector<BlockId>> BehaviorEventLog::all(size_t limit) {
    return mStore->children(mRoot, -1, limit);
}

ll::Expected<size_t> BehaviorEventLog::count() {
    auto ids = mStore->children(mRoot, -1, 0);
    if (!ids)
        return ll::makeStringError(ids.error().message());

    return ids->size();
}

ll::Expected<std::vector<BlockId>> BehaviorEventLog::byTimeRange(std::int64_t from, std::int64_t to, size_t limit) {
    return mStore->queryInt(mKeyTimestamp, from, to, limit);
}

ll::Expected<std::vector<BlockId>> BehaviorEventLog::byName(std::string_view name, size_t limit) {
    auto kind = this->intern(name);
    if (!kind)
        return ll::makeStringError(kind.error().message());

    return mStore->children(mRoot, *kind, limit);
}

ll::Expected<std::vector<BlockId>> BehaviorEventLog::byType(std::string_view type, size_t limit) {
    auto value = this->intern(type);
    if (!value)
        return ll::makeStringError(value.error().message());

    return mStore->queryInt(mKeyType, *value, *value, limit);
}

ll::Expected<std::vector<BlockId>> BehaviorEventLog::byActor(std::int64_t actor, size_t limit) {
    return mStore->queryInt(mKeyActor, actor, actor, limit);
}

ll::Expected<std::vector<BlockId>> BehaviorEventLog::byDimension(std::int64_t dimension, size_t limit) {
    return mStore->queryInt(mKeyDim, dimension, dimension, limit);
}

ll::Expected<std::vector<BlockId>> BehaviorEventLog::byPosition(
    std::int64_t x, std::int64_t y, std::int64_t z, size_t limit) {
    auto xs = mStore->queryInt(mKeyPosX, x, x, limit);
    if (!xs)
        return ll::makeStringError(xs.error().message());

    if (xs->empty())
        return xs;

    auto ys = mStore->queryInt(mKeyPosY, y, y, limit);
    if (!ys)
        return ll::makeStringError(ys.error().message());

    keepCommon(*xs, *ys);
    if (xs->empty())
        return xs;

    auto zs = mStore->queryInt(mKeyPosZ, z, z, limit);
    if (!zs)
        return ll::makeStringError(zs.error().message());

    keepCommon(*xs, *zs);
    if (limit != 0 && xs->size() > limit)
        xs->resize(limit);

    return xs;
}

ll::Expected<void> BehaviorEventLog::erase(std::span<const BlockId> ids) {
    if (ids.empty())
        return {};

    auto batch = WriteBatch::begin(*mStore);
    if (!batch)
        return ll::makeStringError(batch.error().message());

    for (auto id : ids) {
        if (auto r = (*batch)->control(id, BlockLifecycle::Deleted); !r)
            return ll::makeStringError(r.error().message());
    }

    auto ok = (*batch)->commit();
    if (!ok)
        return ll::makeStringError(ok.error().message());

    return {};
}

ll::Expected<size_t> BehaviorEventLog::archiveBefore(std::int64_t timestamp) {
    auto ids = mStore->queryInt(mKeyTimestamp, std::numeric_limits<std::int64_t>::min(), timestamp);
    if (!ids)
        return ll::makeStringError(ids.error().message());

    if (ids->empty())
        return size_t{0};

    auto batch = WriteBatch::begin(*mStore);
    if (!batch)
        return ll::makeStringError(batch.error().message());

    for (auto id : *ids) {
        if (auto r = (*batch)->control(id, BlockLifecycle::Archived); !r)
            return ll::makeStringError(r.error().message());
    }

    auto ok = (*batch)->commit();
    if (!ok)
        return ll::makeStringError(ok.error().message());

    return ids->size();
}
