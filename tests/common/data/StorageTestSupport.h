#pragma once

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <ll/api/Expected.h>

#include "LOICollectionA/data/sqlite/block/BlockError.h"

namespace storage_test_support {
    inline void dropDb(std::filesystem::path const& base) {
        std::filesystem::remove(base);
        std::filesystem::remove(std::filesystem::path(base.string() + "-wal"));
        std::filesystem::remove(std::filesystem::path(base.string() + "-shm"));
    }

    inline std::filesystem::path tempDb(std::string_view suite, std::string_view tag) {
        auto base = std::filesystem::temp_directory_path()
            / ("loicollectiona-" + std::string(suite) + "-" + std::string(tag) + ".db");
        dropDb(base);
        return base;
    }

    inline std::string_view payloadView(std::vector<std::byte> const& data) {
        return std::string_view(reinterpret_cast<char const*>(data.data()), data.size());
    }

    inline std::string payloadText(std::vector<std::byte> const& data) {
        return std::string(reinterpret_cast<char const*>(data.data()), data.size());
    }

    inline void expectBlockError(ll::Error& err, BlockError::BlockErrorCode code) {
        if (!err.isA<ll::ErrorCodeError>()) {
            ADD_FAILURE() << "expected block error code " << static_cast<int>(code)
                          << " but got message: " << err.message();
            return;
        }

        EXPECT_EQ(err.as<ll::ErrorCodeError>().ec, BlockError::makeErrorCode(code));
    }
}
