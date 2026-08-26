#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/ir/ByteCode.h"

namespace LOICollection::frontend::ir {
    class BytecodeSerializer {
    public:
        struct Header {
            std::string sourceHash;
            std::vector<std::string> importHashes;
        };

        LOICOLLECTION_A_NDAPI static std::optional<std::string> serialize(
            const BytecodeChunk& chunk, const Header& header
        );

        LOICOLLECTION_A_NDAPI static std::optional<BytecodeChunk> deserialize(
            const std::string& blob, const Header& expected
        );

    private:
        BytecodeSerializer() = default;

        static constexpr char MAGIC[4] = { 'L', 'C', 'U', 'I' };
        static constexpr uint32_t FORMAT_VERSION = 2;
    };
}
