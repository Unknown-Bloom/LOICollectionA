#pragma once

#include <span>
#include <string>
#include <vector>
#include <utility>
#include <cstdint>
#include <string_view>

#include <ll/api/Expected.h>

#include "LOICollectionA/base/Macro.h"
#include "LOICollectionA/data/sqlite/block/BlockStore.h"

class BlockRepository;
class WriteBatch;

namespace LOICollection::data {
    enum class TimeBucket : std::int64_t {
        Hour = 3'600'000'000'000LL,
        Day = 86'400'000'000'000LL
    };

    struct TimeFoldValue {
        std::int64_t count = 0;
        std::int64_t sum = 0;
        std::int64_t min = 0;
        std::int64_t max = 0;
    };

    class TimeFold {
    public:
        TimeFold(TimeFold const&) = default;
        TimeFold(TimeFold&&) = default;
        TimeFold& operator=(TimeFold const&) = delete;
        TimeFold& operator=(TimeFold&&) = delete;

        LOICOLLECTION_A_NDAPI static ll::Expected<TimeFold> open(
            BlockRepository& repo, std::string_view name, TimeBucket bucket
        );

        LOICOLLECTION_A_NDAPI ll::Expected<void> add(
            WriteBatch& batch, std::string_view group, std::int64_t atNs, std::int64_t value
        );

        LOICOLLECTION_A_NDAPI ll::Expected<void> put(
            WriteBatch& batch, std::string_view group, std::int64_t atNs, TimeFoldValue const& value
        );

        LOICOLLECTION_A_NDAPI ll::Expected<TimeFoldValue> get(std::string_view group, std::int64_t atNs);

        LOICOLLECTION_A_NDAPI ll::Expected<std::vector<std::pair<std::string, TimeFoldValue>>> groups(std::int64_t atNs);

        LOICOLLECTION_A_NDAPI ll::Expected<void> replace(
            std::int64_t atNs, std::span<const std::pair<std::string, TimeFoldValue>> values
        );

        LOICOLLECTION_A_NDAPI ll::Expected<void> trim(std::int64_t beforeNs);

        LOICOLLECTION_A_NDAPI ll::Expected<void> clear();

        LOICOLLECTION_A_NDAPI std::int64_t bucketStart(std::int64_t atNs) const noexcept;

        LOICOLLECTION_A_NDAPI std::string_view name() const noexcept;

    private:
        TimeFold(
            BlockRepository& repo,
            std::string name,
            BlockId root,
            TimeBucket bucket,
            PropKey bucketKey,
            PropKey countKey,
            PropKey sumKey,
            PropKey minKey,
            PropKey maxKey
        );

        static std::string rowKey(std::string_view group, std::int64_t bucketStart);
        static std::string_view groupOf(std::string_view rowKey);

        TimeFoldValue valueOf(std::span<const BlockProp> props) const noexcept;

        BlockRepository& mRepo;
        std::string mName;
        BlockId mRoot = 0;
        std::int64_t mBucketNs = 0;

        PropKey mBucketKey = 0;
        PropKey mCountKey = 0;
        PropKey mSumKey = 0;
        PropKey mMinKey = 0;
        PropKey mMaxKey = 0;

        std::string mBucketStatement;
        std::string mAddStatement;
        std::string mMinStatement;
        std::string mMaxStatement;

        std::string mBucketStatementKey;
        std::string mAddStatementKey;
        std::string mMinStatementKey;
        std::string mMaxStatementKey;
    };

    LOICOLLECTION_A_NDAPI ll::Expected<void> clearTimeFold(BlockRepository& repo, std::string_view name);
}
