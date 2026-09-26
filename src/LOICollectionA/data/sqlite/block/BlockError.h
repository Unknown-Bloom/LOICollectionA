#pragma once

#include <string>

#include <system_error>

namespace BlockError {
    enum class BlockErrorCode : int {
        PoolTimeout = 1,
        CreateFailed = 2,
        LoadFailed = 3,
        NotFound = 4,
        NoConnection = 5,
        CommitFailed = 6,
        RollbackFailed = 7,
        DuplicateName = 8,
        InvalidState = 9,
        SchemaMismatch = 10
    };

    struct BlockErrorCategory : std::error_category {
        [[nodiscard]] const char* name() const noexcept override {
            return "BlockError";
        }

        [[nodiscard]] std::string message(int ev) const override {
            switch (static_cast<BlockErrorCode>(ev)) {
                case BlockErrorCode::PoolTimeout: return "connection pool timeout";
                case BlockErrorCode::CreateFailed: return "block create failed";
                case BlockErrorCode::LoadFailed: return "block load failed";
                case BlockErrorCode::NotFound: return "block not found";
                case BlockErrorCode::NoConnection: return "no database connection";
                case BlockErrorCode::CommitFailed: return "transaction commit failed";
                case BlockErrorCode::RollbackFailed: return "transaction rollback failed";
                case BlockErrorCode::DuplicateName: return "duplicate block name under parent";
                case BlockErrorCode::InvalidState: return "invalid block state";
                case BlockErrorCode::SchemaMismatch: return "table schema version or columns mismatch";
            }

            return "Unknown";
        }
    };

    inline std::error_code makeErrorCode(BlockErrorCode e) {
        static BlockErrorCategory cat;
        return std::error_code{ static_cast<int>(e), cat };
    }
}