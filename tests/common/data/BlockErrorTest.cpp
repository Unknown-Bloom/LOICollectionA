#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <vector>

#include "LOICollectionA/data/sqlite/block/BlockError.h"

namespace block_error_test_support {
    std::vector<std::pair<BlockError::BlockErrorCode, std::string_view>> knownCodes() {
        return {
            {BlockError::BlockErrorCode::PoolTimeout, "connection pool timeout"},
            {BlockError::BlockErrorCode::CreateFailed, "block create failed"},
            {BlockError::BlockErrorCode::LoadFailed, "block load failed"},
            {BlockError::BlockErrorCode::NotFound, "block not found"},
            {BlockError::BlockErrorCode::NoConnection, "no database connection"},
            {BlockError::BlockErrorCode::CommitFailed, "transaction commit failed"},
            {BlockError::BlockErrorCode::RollbackFailed, "transaction rollback failed"},
            {BlockError::BlockErrorCode::DuplicateName, "duplicate block name under parent"},
            {BlockError::BlockErrorCode::InvalidState, "invalid block state"},
            {BlockError::BlockErrorCode::SchemaMismatch, "table schema version or columns mismatch"},
        };
    }
}

TEST(BlockErrorTest, MessagesMatchKnownTextForEachCode) {
    for (auto const& [code, text] : block_error_test_support::knownCodes())
        EXPECT_EQ(BlockError::makeErrorCode(code).message(), text);
}

TEST(BlockErrorTest, KnownMessagesArePairwiseDistinct) {
    std::unordered_set<std::string> messages;

    for (auto const& [code, text] : block_error_test_support::knownCodes())
        messages.insert(std::string(text));

    EXPECT_EQ(messages.size(), block_error_test_support::knownCodes().size());
}

TEST(BlockErrorTest, UnknownCodeFallsBackToGenericMessage) {
    auto code = BlockError::makeErrorCode(static_cast<BlockError::BlockErrorCode>(99));
    EXPECT_EQ(code.message(), "Unknown");
}

TEST(BlockErrorTest, CategoryIsNamedAfterBlockDomain) {
    auto code = BlockError::makeErrorCode(BlockError::BlockErrorCode::NotFound);
    EXPECT_STREQ(code.category().name(), "BlockError");
}

TEST(BlockErrorTest, SameCodesCompareEqualAndDifferentCodesDiffer) {
    auto left = BlockError::makeErrorCode(BlockError::BlockErrorCode::NotFound);
    auto right = BlockError::makeErrorCode(BlockError::BlockErrorCode::NotFound);
    auto other = BlockError::makeErrorCode(BlockError::BlockErrorCode::InvalidState);

    EXPECT_TRUE(left == right);
    EXPECT_TRUE(left != other);
}

TEST(BlockErrorTest, ErrorCodeValueCarriesEnumValue) {
    for (auto const& [code, text] : block_error_test_support::knownCodes())
        EXPECT_EQ(BlockError::makeErrorCode(code).value(), static_cast<int>(code));
}

TEST(BlockErrorTest, CategoryInstanceIsStableAcrossCalls) {
    auto left = BlockError::makeErrorCode(BlockError::BlockErrorCode::PoolTimeout);
    auto right = BlockError::makeErrorCode(BlockError::BlockErrorCode::SchemaMismatch);

    EXPECT_EQ(&left.category(), &right.category());
}

TEST(BlockErrorTest, ErrorCodeMessageDelegatesToCategory) {
    for (auto const& [code, text] : block_error_test_support::knownCodes()) {
        auto ec = BlockError::makeErrorCode(code);
        EXPECT_EQ(ec.message(), ec.category().message(static_cast<int>(code)));
    }
}
