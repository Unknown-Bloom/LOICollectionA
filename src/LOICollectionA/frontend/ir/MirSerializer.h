#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "LOICollectionA/base/Macro.h"

#include "LOICollectionA/frontend/ir/Mir.h"

namespace LOICollection::frontend::ir {
    class MirSerializer {
    public:
        struct Header {
            std::string scriptId;
            std::string abiFingerprint;
            std::optional<std::string> sourceHash;
            std::vector<std::string> importHashes;
        };

        LOICOLLECTION_A_NDAPI static std::optional<std::string> serialize(
            const MirChunk& chunk, const Header& header, std::string* bodyChecksum = nullptr
        );

        LOICOLLECTION_A_NDAPI static std::optional<MirChunk> deserialize(
            const std::string& blob, const Header& expected, std::string* bodyChecksum = nullptr
        );

        LOICOLLECTION_A_NDAPI static std::optional<Header> peekHeader(const std::string& blob);

        LOICOLLECTION_A_NDAPI static std::optional<std::string> serializeDebugInfo(
            const MirChunk& chunk, const std::string& bodyChecksum
        );

        LOICOLLECTION_A_NDAPI static bool attachDebugInfo(
            MirChunk& chunk, const std::string& debugBlob, const std::string& bodyChecksum
        );

    private:
        MirSerializer() = default;

        static constexpr char MAGIC[4] = { 'L', 'C', 'U', 'P' };
        static constexpr char DEBUG_MAGIC[4] = { 'L', 'C', 'U', 'D' };
        static constexpr uint32_t FORMAT_VERSION = 1;
    };
}
