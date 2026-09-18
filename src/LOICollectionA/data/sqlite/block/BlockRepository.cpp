#include "LOICollectionA/data/sqlite/block/BlockRepository.h"

#include <cctype>
#include <memory>
#include <optional>
#include <ranges>
#include <utility>

#include "LOICollectionA/data/sqlite/block/BlockError.h"
#include "LOICollectionA/data/sqlite/block/Payload.h"
#include "LOICollectionA/data/sqlite/connection/ConnectionPool.h"

namespace {
    constexpr std::int32_t kRowKind = 0;
}

BlockRepository::BlockRepository(std::shared_ptr<BlockStore> store) : mStore(std::move(store)) {}

BlockRepository::~BlockRepository() = default;

ll::Expected<std::shared_ptr<BlockRepository>> BlockRepository::open(
    std::string dbPath, size_t connections) {
    auto pool = std::make_shared<ConnectionPool>(std::move(dbPath), connections);
    auto store = BlockStore::create(pool);
    if (!store)
        return ll::makeStringError(store.error().message());

    auto repo = std::shared_ptr<BlockRepository>(new BlockRepository(std::move(*store)));
    repo->mPool = std::move(pool);

    return repo;
}

ll::Expected<std::shared_ptr<BlockRepository>> BlockRepository::fromStore(
    std::shared_ptr<BlockStore> store) {
    if (!store)
        return ll::makeStringError("null block store");
    return std::shared_ptr<BlockRepository>(new BlockRepository(std::move(store)));
}

ll::Expected<BlockId> BlockRepository::ensureRoot(std::string_view table) {
    auto root = mStore->load(0, table);
    if (root)
        return root.value().id;

    auto created = mStore->createBlock(0, 0, table);
    if (!created)
        return ll::makeStringError(created.error().message());
    return *created;
}

ll::Expected<std::string> BlockRepository::encodeRow(
    observer<BlockStore> store, std::unordered_map<std::string, std::string> const& values) {
    PayloadWriter writer;
    for (auto const& [column, value] : values) {
        auto key = store->intern(column);
        if (!key)
            return ll::makeStringError(key.error().message());
        writer.write(key.value(), std::string_view(value));
    }
    auto data = writer.data();
    return std::string(reinterpret_cast<char const*>(data.data()), data.size());
}

ll::Expected<std::string> BlockRepository::encodeRow(
    WriteBatch& tx, std::unordered_map<std::string, std::string> const& values) {
    PayloadWriter writer;
    for (auto const& [column, value] : values) {
        auto key = tx.intern(column);
        if (!key)
            return ll::makeStringError(key.error().message());
        writer.write(key.value(), std::string_view(value));
    }
    auto data = writer.data();
    return std::string(reinterpret_cast<char const*>(data.data()), data.size());
}

ll::Expected<std::unordered_map<std::string, std::string>> BlockRepository::decodeRow(
    observer<BlockStore> store, std::vector<std::byte> const& payload) {
    std::unordered_map<std::string, std::string> row;
    PayloadReader reader(std::string_view(
        reinterpret_cast<char const*>(payload.empty() ? nullptr : payload.data()),
        payload.size()));
    reader.forEach([&](PayloadField const& field) {
        if (field.type != PayloadType::Text)
            return;
        auto column = store->unintern(field.key);
        if (column)
            row[column.value()] = reader.materializeText(field);
    });
    return row;
}

ll::Expected<std::unordered_map<std::string, std::string>> BlockRepository::readRow(
    std::string_view table, std::string_view key) {
    auto root = this->ensureRoot(table);
    if (!root)
        return ll::makeStringError(root.error().message());

    auto record = mStore->load(root.value(), key);
    if (!record || record.value().state == BlockLifecycle::Deleted)
        return std::unordered_map<std::string, std::string>{};

    return decodeRow(mStore.get(), record.value().payload);
}

ll::Expected<void> BlockRepository::exec(std::string_view sql) {
    auto trim = [](std::string_view s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
            s.remove_prefix(1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
            s.remove_suffix(1);
        return s;
    };
    auto ltrim = [](std::string_view s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
            s.remove_prefix(1);
        return s;
    };

    auto stmt = trim(sql);
    if (!(stmt.starts_with("DELETE") || stmt.starts_with("delete")))
        return {};

    auto from = stmt.find("FROM");
    if (from == std::string_view::npos)
        return {};

    auto where = stmt.find("WHERE");
    auto afterFrom = stmt.substr(from + 4);
    auto headEnd = (where == std::string_view::npos) ? afterFrom.size() : (where - (from + 4));
    auto table = trim(afterFrom.substr(0, headEnd));
    while (!table.empty() && table.back() == ';')
        table.remove_suffix(1);
    table = trim(table);

    auto root = mStore->load(0, table);
    if (!root)
        return {};
    auto records = mStore->records(root.value().id, -1);
    if (!records)
        return ll::makeStringError(records.error().message());

    if (where == std::string_view::npos) {
        for (auto const& record : records.value()) {
            if (record.state == BlockLifecycle::Deleted)
                continue;
            if (auto r = mStore->control(record.id, BlockLifecycle::Deleted); !r)
                return ll::makeStringError(r.error().message());
        }
        return {};
    }

    auto whereClause = trim(stmt.substr(where + 5));
    auto lt = whereClause.find('<');
    if (lt == std::string_view::npos)
        return {};
    auto colName = trim(whereClause.substr(0, lt));
    auto rhs = trim(whereClause.substr(lt + 1));
    if (!rhs.empty() && rhs.back() == ';')
        rhs.remove_suffix(1);
    rhs = trim(rhs);

    long long threshold = 0;
    {
        try {
            threshold = std::stoll(std::string(rhs));
        } catch (...) {
            return {};
        }
    }

    for (auto const& record : records.value()) {
        if (record.state == BlockLifecycle::Deleted)
            continue;
        auto row = decodeRow(mStore.get(), record.payload);
        if (!row)
            return ll::makeStringError(row.error().message());
        auto it = row.value().find(std::string(colName));
        if (it == row.value().end())
            continue;
        long long val = 0;
        try {
            val = std::stoll(it->second);
        } catch (...) {
            continue;
        }
        if (val < threshold) {
            if (auto r = mStore->control(record.id, BlockLifecycle::Deleted); !r)
                return ll::makeStringError(r.error().message());
        }
    }
    return {};
}

ll::Expected<void> BlockRepository::create(std::string_view table, CreateCallback callback) {
    auto root = this->ensureRoot(table);
    if (!root)
        return ll::makeStringError(root.error().message());

    if (callback) {
        callback([this](std::string_view column) {
            (void)mStore->intern(column);
        });
    }
    return {};
}

ll::Expected<void> BlockRepository::remove(std::string_view table) {
    auto root = this->ensureRoot(table);
    if (!root)
        return ll::makeStringError(root.error().message());

    auto children = mStore->children(root.value(), -1);
    if (!children)
        return ll::makeStringError(children.error().message());

    for (auto id : children.value()) {
        if (auto r = mStore->control(id, BlockLifecycle::Deleted); !r)
            return ll::makeStringError(r.error().message());
    }
    return mStore->control(root.value(), BlockLifecycle::Deleted);
}

ll::Expected<void> BlockRepository::set(
    std::string_view table, std::string_view key, std::string_view column, std::string_view value) {
    auto row = this->readRow(table, key);
    if (!row)
        return ll::makeStringError(row.error().message());

    row.value()[std::string(column)] = std::string(value);
    return this->set(table, key, std::move(row.value()));
}

ll::Expected<void> BlockRepository::set(
    std::string_view table, std::string_view key, std::unordered_map<std::string, std::string> values) {
    auto root = this->ensureRoot(table);
    if (!root)
        return ll::makeStringError(root.error().message());

    auto payload = encodeRow(mStore.get(), values);
    if (!payload)
        return ll::makeStringError(payload.error().message());

    auto existing = mStore->load(root.value(), key);
    if (existing && existing.value().id != 0)
        return mStore->setPayload(existing.value().id, payload.value());

    auto id = mStore->createBlock(root.value(), kRowKind, key, payload.value());
    if (!id)
        return ll::makeStringError(id.error().message());
    return {};
}

ll::Expected<void> BlockRepository::del(std::string_view table, std::string_view key) {
    auto root = this->ensureRoot(table);
    if (!root)
        return ll::makeStringError(root.error().message());

    auto record = mStore->load(root.value(), key);
    if (!record || record.value().id == 0)
        return {};
    return mStore->control(record.value().id, BlockLifecycle::Deleted);
}

ll::Expected<void> BlockRepository::del(std::string_view table, std::vector<std::string> keys) {
    for (auto const& key : keys) {
        if (auto r = this->del(table, key); !r)
            return r;
    }
    return {};
}

ll::Expected<BlockRepository::WriteTransaction>
BlockRepository::WriteTransaction::create(BlockRepository& repo) {
    auto batch = WriteBatch::begin(*repo.mStore);
    if (!batch)
        return ll::makeStringError(batch.error().message());
    return WriteTransaction(std::move(*batch));
}

ll::Expected<bool> BlockRepository::WriteTransaction::commit() {
    return mBatch ? mBatch->commit() : ll::makeStringError("null transaction");
}

ll::Expected<bool> BlockRepository::WriteTransaction::rollback() {
    return mBatch ? mBatch->rollback() : ll::makeStringError("null transaction");
}

ll::Expected<void> BlockRepository::set(
    WriteBatch& tx, std::string_view table, std::string_view key,
    std::unordered_map<std::string, std::string> values) {
    auto payload = encodeRow(tx, values);
    if (!payload)
        return ll::makeStringError(payload.error().message());

    auto rootRec = mStore->load(0, table);
    BlockId root = 0;
    if (rootRec && rootRec.value().id != 0) {
        root = rootRec.value().id;
    } else {
        auto created = tx.append(0, kRowKind, table);
        if (!created)
            return ll::makeStringError(created.error().message());
        root = created.value();
    }

    auto existing = mStore->load(root, key);
    if (existing && existing.value().id != 0 && existing.value().state != BlockLifecycle::Deleted)
        return tx.setPayload(existing.value().id, payload.value());

    auto id = tx.append(root, kRowKind, key, payload.value());
    if (!id)
        return ll::makeStringError(id.error().message());
    return {};
}

ll::Expected<void> BlockRepository::set(
    WriteBatch& tx, std::string_view table, std::string_view key,
    std::string_view column, std::string_view value) {
    auto row = this->get(table, key);
    if (!row)
        return ll::makeStringError(row.error().message());
    row.value()[std::string(column)] = std::string(value);
    return this->set(tx, table, key, std::move(row.value()));
}

ll::Expected<void> BlockRepository::del(WriteBatch& tx, std::string_view table, std::string_view key) {
    auto rootRec = mStore->load(0, table);
    if (!rootRec || rootRec.value().id == 0)
        return {};
    auto record = mStore->load(rootRec.value().id, key);
    if (!record || record.value().id == 0)
        return {};
    return tx.control(record.value().id, BlockLifecycle::Deleted);
}

ll::Expected<void> BlockRepository::del(WriteBatch& tx, std::string_view table, std::vector<std::string> keys) {
    for (auto const& key : keys) {
        if (auto r = this->del(tx, table, key); !r)
            return r;
    }
    return {};
}

ll::Expected<std::unordered_map<std::string, std::string>> BlockRepository::get(
    WriteBatch& tx, std::string_view table, std::string_view key) {
    return this->get(table, key);
}

ll::Expected<std::string> BlockRepository::get(
    WriteBatch& tx, std::string_view table, std::string_view key,
    std::string_view column, std::string_view defaultValue) {
    return this->get(table, key, column, defaultValue);
}

ll::Expected<bool> BlockRepository::has(WriteBatch& tx, std::string_view table, std::string_view key) {
    return this->has(table, key);
}

ll::Expected<std::vector<std::string>> BlockRepository::list(WriteBatch& tx, std::string_view table) {
    return this->list(table);
}

ll::Expected<bool> BlockRepository::has(std::string_view table, std::string_view key) {
    auto row = this->readRow(table, key);
    if (!row)
        return ll::makeStringError(row.error().message());
    return !row.value().empty();
}

ll::Expected<bool> BlockRepository::has(std::string_view table) {
    auto root = this->ensureRoot(table);
    if (!root)
        return ll::makeStringError(root.error().message());

    auto record = mStore->load(root.value());
    if (!record)
        return ll::makeStringError(record.error().message());
    return record.value().state != BlockLifecycle::Deleted;
}

ll::Expected<std::unordered_map<std::string, std::string>> BlockRepository::get(
    std::string_view table, std::string_view key) {
    return this->readRow(table, key);
}

ll::Expected<std::unordered_map<std::string, std::unordered_map<std::string, std::string>>> BlockRepository::get(
    std::string_view table, std::vector<std::string> keys) {
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> result;
    for (auto const& key : keys) {
        auto row = this->readRow(table, key);
        if (!row)
            return ll::makeStringError(row.error().message());
        if (!row.value().empty())
            result[key] = std::move(row.value());
    }
    return result;
}

ll::Expected<std::string> BlockRepository::get(
    std::string_view table, std::string_view key, std::string_view column, std::string_view defaultValue) {
    auto row = this->readRow(table, key);
    if (!row)
        return ll::makeStringError(row.error().message());

    auto it = row.value().find(std::string(column));
    return it == row.value().end() ? std::string(defaultValue) : it->second;
}

ll::Expected<std::string> BlockRepository::find(
    std::string_view table, std::vector<std::pair<std::string, std::string>> conditions,
    std::string_view defaultValue, FindCondition match) {
    auto keys = this->find(table, std::move(conditions), match);
    if (!keys)
        return ll::makeStringError(keys.error().message());
    return keys.value().empty() ? std::string(defaultValue) : std::move(keys.value().front());
}

ll::Expected<std::vector<std::string>> BlockRepository::find(
    std::string_view table, std::vector<std::pair<std::string, std::string>> conditions, FindCondition match) {
    auto root = this->ensureRoot(table);
    if (!root)
        return ll::makeStringError(root.error().message());

    auto records = mStore->records(root.value(), -1);
    if (!records)
        return ll::makeStringError(records.error().message());

    std::vector<std::string> keys;
    for (auto const& record : records.value()) {
        if (record.state == BlockLifecycle::Deleted)
            continue;

        auto row = decodeRow(mStore.get(), record.payload);
        if (!row)
            return ll::makeStringError(row.error().message());

        std::int64_t matched = 0;
        for (auto const& [column, value] : conditions) {
            auto it = row.value().find(column);
            if (it != row.value().end() && it->second == value)
                ++matched;
        }

        if (match == FindCondition::AND ? matched == static_cast<std::int64_t>(conditions.size())
                                        : matched > 0)
            keys.emplace_back(record.name);
    }
    return keys;
}

ll::Expected<std::vector<std::string>> BlockRepository::find(
    std::string_view table, std::string_view column,
    std::vector<std::pair<std::string, std::string>> conditions, FindCondition match) {
    auto keys = this->find(table, std::move(conditions), match);
    if (!keys)
        return ll::makeStringError(keys.error().message());

    std::vector<std::string> result;
    result.reserve(keys.value().size());
    for (auto const& key : keys.value()) {
        auto value = this->get(table, key, column);
        if (!value)
            return ll::makeStringError(value.error().message());
        result.emplace_back(value.value());
    }
    return result;
}

ll::Expected<std::vector<std::string>> BlockRepository::list(std::string_view table) {
    auto root = this->ensureRoot(table);
    if (!root)
        return ll::makeStringError(root.error().message());

    auto children = mStore->children(root.value(), -1);
    if (!children)
        return ll::makeStringError(children.error().message());

    std::vector<std::string> keys;
    keys.reserve(children.value().size());
    for (auto id : children.value()) {
        auto record = mStore->load(id);
        if (!record)
            return ll::makeStringError(record.error().message());
        if (record.value().state != BlockLifecycle::Deleted)
            keys.emplace_back(record.value().name);
    }
    return keys;
}

ll::Expected<std::vector<std::string>> BlockRepository::list() {
    auto tables = mStore->children(0, -1);
    if (!tables)
        return ll::makeStringError(tables.error().message());

    std::vector<std::string> names;
    names.reserve(tables.value().size());
    for (auto id : tables.value()) {
        auto record = mStore->load(id);
        if (!record)
            return ll::makeStringError(record.error().message());
        if (record.value().state != BlockLifecycle::Deleted)
            names.emplace_back(record.value().name);
    }
    return names;
}

ll::Expected<std::vector<std::string>> BlockRepository::columns(std::string_view table) {
    auto root = this->ensureRoot(table);
    if (!root)
        return ll::makeStringError(root.error().message());

    auto records = mStore->records(root.value(), -1);
    if (!records)
        return ll::makeStringError(records.error().message());

    std::vector<std::string> columns;
    std::unordered_map<std::string, bool> seen;
    for (auto const& record : records.value()) {
        if (record.state == BlockLifecycle::Deleted)
            continue;
        auto row = decodeRow(mStore.get(), record.payload);
        if (!row)
            return ll::makeStringError(row.error().message());
        for (auto const& [column, _] : row.value()) {
            if (!seen[column]) {
                seen[column] = true;
                columns.emplace_back(column);
            }
        }
    }
    return columns;
}
