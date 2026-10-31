#include <algorithm>
#include <charconv>
#include <limits>
#include <string>
#include <utility>

#include "LOICollectionA/data/sqlite/block/BlockRepository.h"
#include "LOICollectionA/data/sqlite/block/WriteBatch.h"
#include "LOICollectionA/data/sqlite/block/TimeFold.h"

namespace LOICollection::data::detail {
    ll::Expected<void> eraseTimeFoldRows(WriteBatch& batch, std::span<const BlockId> ids) {
        for (BlockId id : ids) {
            BlockProp param;
            param.type = PayloadType::Int;
            param.intValue = id;

            std::span<const BlockProp> params(&param, 1);

            auto props = batch.execCells("timeFoldEraseProps", "DELETE FROM prop WHERE block_id=?", params);
            if (!props.has_value())
                return ll::forwardError(props.error());

            auto block = batch.execCells("timeFoldEraseBlock", "DELETE FROM block WHERE id=?", params);
            if (!block.has_value())
                return ll::forwardError(block.error());
        }

        return {};
    }

    ll::Expected<void> eraseTimeFoldRows(BlockRepository& repo, std::span<const BlockId> ids) {
        if (ids.empty())
            return {};

        auto batch = WriteBatch::begin(repo.store());
        if (!batch.has_value())
            return ll::makeStringError(batch.error().message());

        auto erased = eraseTimeFoldRows(**batch, ids);
        if (!erased.has_value())
            return ll::forwardError(erased.error());

        auto committed = (*batch)->commit();
        if (!committed.has_value())
            return ll::makeStringError(committed.error().message());

        repo.store().invalidateAll();

        return {};
    }
}

namespace LOICollection::data {
    TimeFold::TimeFold(
        BlockRepository& repo,
        std::string name,
        BlockId root,
        TimeBucket bucket,
        PropKey bucketKey,
        PropKey countKey,
        PropKey sumKey,
        PropKey minKey,
        PropKey maxKey
    ) : mRepo(repo),
        mName(std::move(name)),
        mRoot(root),
        mBucketNs(static_cast<std::int64_t>(bucket)),
        mBucketKey(bucketKey),
        mCountKey(countKey),
        mSumKey(sumKey),
        mMinKey(minKey),
        mMaxKey(maxKey) {
        mBucketStatement =
            "INSERT INTO prop(block_id,key,type,ival,rval,tval) VALUES(?,?,?,?,0.0,NULL)"
            " ON CONFLICT(block_id,key) DO NOTHING";
        mAddStatement =
            "INSERT INTO prop(block_id,key,type,ival,rval,tval) VALUES(?,?,?,?,0.0,NULL),(?,?,?,?,0.0,NULL)"
            " ON CONFLICT(block_id,key) DO UPDATE SET type=excluded.type,ival=prop.ival+excluded.ival";
        mMinStatement =
            "INSERT INTO prop(block_id,key,type,ival,rval,tval) VALUES(?,?,?,?,0.0,NULL)"
            " ON CONFLICT(block_id,key) DO UPDATE SET type=excluded.type,ival=MIN(prop.ival,excluded.ival)";
        mMaxStatement =
            "INSERT INTO prop(block_id,key,type,ival,rval,tval) VALUES(?,?,?,?,0.0,NULL)"
            " ON CONFLICT(block_id,key) DO UPDATE SET type=excluded.type,ival=MAX(prop.ival,excluded.ival)";

        std::string suffix = std::to_string(mBucketKey) + ":" + std::to_string(mCountKey) + ":"
                           + std::to_string(mSumKey) + ":" + std::to_string(mMinKey) + ":"
                           + std::to_string(mMaxKey);

        mBucketStatementKey = "timeFoldBucket:" + suffix;
        mAddStatementKey = "timeFoldAdd:" + suffix;
        mMinStatementKey = "timeFoldMin:" + suffix;
        mMaxStatementKey = "timeFoldMax:" + suffix;
    }

    ll::Expected<TimeFold> TimeFold::open(BlockRepository& repo, std::string_view name, TimeBucket bucket) {
        BlockId root = 0;

        auto existing = repo.store().load(0, name);
        if (existing.has_value()) {
            root = existing.value().id;
        } else {
            auto created = repo.store().createBlock(0, 0, name);
            if (!created.has_value())
                return ll::makeStringError(created.error().message());

            root = created.value();
        }

        auto bucketKey = repo.store().intern("tf.bucket");
        if (!bucketKey.has_value())
            return ll::makeStringError(bucketKey.error().message());

        auto countKey = repo.store().intern("tf.count");
        if (!countKey.has_value())
            return ll::makeStringError(countKey.error().message());

        auto sumKey = repo.store().intern("tf.sum");
        if (!sumKey.has_value())
            return ll::makeStringError(sumKey.error().message());

        auto minKey = repo.store().intern("tf.min");
        if (!minKey.has_value())
            return ll::makeStringError(minKey.error().message());

        auto maxKey = repo.store().intern("tf.max");
        if (!maxKey.has_value())
            return ll::makeStringError(maxKey.error().message());

        return TimeFold(
            repo,
            std::string(name),
            root,
            bucket,
            bucketKey.value(),
            countKey.value(),
            sumKey.value(),
            minKey.value(),
            maxKey.value()
        );
    }

    std::string TimeFold::rowKey(std::string_view group, std::int64_t bucketStart) {
        std::string key;
        key.reserve(group.size() + 32);
        key += std::to_string(group.size());
        key.push_back(':');
        key.append(group);
        key.push_back(':');
        key += std::to_string(bucketStart);
        return key;
    }

    std::string_view TimeFold::groupOf(std::string_view rowKey) {
        auto separator = rowKey.find(':');
        if (separator == std::string_view::npos)
            return rowKey;

        std::size_t length = 0;
        auto [ptr, ec] = std::from_chars(rowKey.data(), rowKey.data() + separator, length);
        if (ec != std::errc() || ptr != rowKey.data() + separator || separator + 1 + length > rowKey.size())
            return rowKey;

        return rowKey.substr(separator + 1, length);
    }

    std::int64_t TimeFold::bucketStart(std::int64_t atNs) const noexcept {
        std::int64_t start = (atNs / mBucketNs) * mBucketNs;
        if (atNs < 0 && atNs % mBucketNs != 0)
            start -= mBucketNs;

        return start;
    }

    std::string_view TimeFold::name() const noexcept {
        return mName;
    }

    TimeFoldValue TimeFold::valueOf(std::span<const BlockProp> props) const noexcept {
        TimeFoldValue value;
        for (auto const& prop : props) {
            if (prop.key == mCountKey)
                value.count = prop.intValue;
            else if (prop.key == mSumKey)
                value.sum = prop.intValue;
            else if (prop.key == mMinKey)
                value.min = prop.intValue;
            else if (prop.key == mMaxKey)
                value.max = prop.intValue;
        }

        return value;
    }

    ll::Expected<void> TimeFold::add(
        WriteBatch& batch, std::string_view group, std::int64_t atNs, std::int64_t value
    ) {
        std::int64_t bucket = bucketStart(atNs);

        auto row = batch.upsertRow(mRoot, rowKey(group, bucket));
        if (!row.has_value())
            return ll::makeStringError(row.error().message());

        auto type = static_cast<std::int64_t>(PayloadType::Int);

        BlockProp bucketParams[] = {
            { 0, PayloadType::Int, row.value(), 0.0, {} },
            { 0, PayloadType::Int, mBucketKey, 0.0, {} },
            { 0, PayloadType::Int, type, 0.0, {} },
            { 0, PayloadType::Int, bucket, 0.0, {} },
        };

        auto written = batch.execCells(mBucketStatementKey, mBucketStatement, bucketParams);
        if (!written.has_value())
            return ll::forwardError(written.error());

        BlockProp addParams[] = {
            { 0, PayloadType::Int, row.value(), 0.0, {} },
            { 0, PayloadType::Int, mCountKey, 0.0, {} },
            { 0, PayloadType::Int, type, 0.0, {} },
            { 0, PayloadType::Int, 1, 0.0, {} },
            { 0, PayloadType::Int, row.value(), 0.0, {} },
            { 0, PayloadType::Int, mSumKey, 0.0, {} },
            { 0, PayloadType::Int, type, 0.0, {} },
            { 0, PayloadType::Int, value, 0.0, {} },
        };

        written = batch.execCells(mAddStatementKey, mAddStatement, addParams);
        if (!written.has_value())
            return ll::forwardError(written.error());

        BlockProp minParams[] = {
            { 0, PayloadType::Int, row.value(), 0.0, {} },
            { 0, PayloadType::Int, mMinKey, 0.0, {} },
            { 0, PayloadType::Int, type, 0.0, {} },
            { 0, PayloadType::Int, value, 0.0, {} },
        };

        written = batch.execCells(mMinStatementKey, mMinStatement, minParams);
        if (!written.has_value())
            return ll::forwardError(written.error());

        BlockProp maxParams[] = {
            { 0, PayloadType::Int, row.value(), 0.0, {} },
            { 0, PayloadType::Int, mMaxKey, 0.0, {} },
            { 0, PayloadType::Int, type, 0.0, {} },
            { 0, PayloadType::Int, value, 0.0, {} },
        };

        written = batch.execCells(mMaxStatementKey, mMaxStatement, maxParams);
        if (!written.has_value())
            return ll::forwardError(written.error());

        return {};
    }

    ll::Expected<void> TimeFold::put(
        WriteBatch& batch, std::string_view group, std::int64_t atNs, TimeFoldValue const& value
    ) {
        std::int64_t bucket = bucketStart(atNs);

        auto row = batch.upsertRow(mRoot, rowKey(group, bucket));
        if (!row.has_value())
            return ll::makeStringError(row.error().message());

        auto written = batch.setProp(row.value(), mBucketKey, bucket);
        if (!written.has_value())
            return ll::forwardError(written.error());

        written = batch.setProp(row.value(), mCountKey, value.count);
        if (!written.has_value())
            return ll::forwardError(written.error());

        written = batch.setProp(row.value(), mSumKey, value.sum);
        if (!written.has_value())
            return ll::forwardError(written.error());

        written = batch.setProp(row.value(), mMinKey, value.min);
        if (!written.has_value())
            return ll::forwardError(written.error());

        written = batch.setProp(row.value(), mMaxKey, value.max);
        if (!written.has_value())
            return ll::forwardError(written.error());

        return {};
    }

    ll::Expected<TimeFoldValue> TimeFold::get(std::string_view group, std::int64_t atNs) {
        std::int64_t bucket = bucketStart(atNs);

        auto row = mRepo.store().idOf(mRoot, rowKey(group, bucket));
        if (!row.has_value())
            return ll::makeStringError(row.error().message());

        if (!row.value().has_value())
            return TimeFoldValue{};

        BlockId id = *row.value();
        auto rows = mRepo.store().rowsByIds(std::span<const BlockId>(&id, 1), true);
        if (!rows.has_value())
            return ll::makeStringError(rows.error().message());

        if (rows.value().empty())
            return TimeFoldValue{};

        return valueOf(rows.value().front().props);
    }

    ll::Expected<std::vector<std::pair<std::string, TimeFoldValue>>> TimeFold::groups(std::int64_t atNs) {
        std::int64_t bucket = bucketStart(atNs);

        auto ids = mRepo.store().queryInt(mRoot, mBucketKey, bucket, bucket);
        if (!ids.has_value())
            return ll::makeStringError(ids.error().message());

        std::vector<std::pair<std::string, TimeFoldValue>> out;
        if (ids.value().empty())
            return out;

        auto rows = mRepo.store().rowsByIds(ids.value(), true);
        if (!rows.has_value())
            return ll::makeStringError(rows.error().message());

        out.reserve(rows.value().size());
        for (auto const& row : rows.value())
            out.emplace_back(std::string(groupOf(row.name)), valueOf(row.props));

        std::sort(out.begin(), out.end(), [](auto const& left, auto const& right) {
            return left.first < right.first;
        });

        return out;
    }

    ll::Expected<void> TimeFold::replace(
        std::int64_t atNs, std::span<const std::pair<std::string, TimeFoldValue>> values
    ) {
        std::int64_t bucket = bucketStart(atNs);

        auto stale = mRepo.store().queryInt(mRoot, mBucketKey, bucket, bucket);
        if (!stale.has_value())
            return ll::makeStringError(stale.error().message());

        auto batch = WriteBatch::begin(mRepo.store());
        if (!batch.has_value())
            return ll::makeStringError(batch.error().message());

        if (!stale.value().empty()) {
            auto erased = detail::eraseTimeFoldRows(**batch, stale.value());
            if (!erased.has_value())
                return ll::forwardError(erased.error());
        }

        for (auto const& [group, value] : values) {
            auto written = put(**batch, group, bucket, value);
            if (!written.has_value())
                return ll::forwardError(written.error());
        }

        auto committed = (*batch)->commit();
        if (!committed.has_value())
            return ll::makeStringError(committed.error().message());

        this->mRepo.store().invalidateAll();

        return {};
    }

    ll::Expected<void> TimeFold::trim(std::int64_t beforeNs) {
        if (beforeNs == std::numeric_limits<std::int64_t>::min())
            return {};

        auto ids = mRepo.store().queryInt(
            mRoot, mBucketKey, std::numeric_limits<std::int64_t>::min(), beforeNs - 1
        );
        if (!ids.has_value())
            return ll::makeStringError(ids.error().message());

        return detail::eraseTimeFoldRows(mRepo, ids.value());
    }

    ll::Expected<void> TimeFold::clear() {
        auto ids = mRepo.store().children(mRoot, kTypedRowKind);
        if (!ids.has_value())
            return ll::makeStringError(ids.error().message());

        return detail::eraseTimeFoldRows(mRepo, ids.value());
    }

    ll::Expected<void> clearTimeFold(BlockRepository& repo, std::string_view name) {
        auto root = repo.store().idOf(0, name);
        if (!root.has_value())
            return ll::makeStringError(root.error().message());

        if (!root.value().has_value())
            return {};

        auto ids = repo.store().children(*root.value(), kTypedRowKind);
        if (!ids.has_value())
            return ll::makeStringError(ids.error().message());

        return detail::eraseTimeFoldRows(repo, ids.value());
    }
}
