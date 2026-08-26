#pragma once

/* Bytecode disk cache (§7.3).
 *
 * File layout:
 *   magic "LCUI" | format version (u32) | source SHA-256 (32 bytes)
 *   | import count (u32) | import SHA-256 * count | serialized BytecodeChunk
 *
 * The cache is invalidated as a whole when the source file or any file of
 * the import graph changes (content hash mismatch), or when the format
 * version differs — no cross-version migration. Native class bindings are
 * stored by name and re-resolved at load time, so deserialization never
 * depends on pointers. */

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

        /* Serializes the chunk with the given provenance header. Returns
         * nullopt when the chunk carries non-serializable constants (e.g.
         * folded runtime objects): the caller then simply skips caching. */
        LOICOLLECTION_A_NDAPI static std::optional<std::string> serialize(
            const BytecodeChunk& chunk, const Header& header
        );

        /* Reads a cache blob and validates it against the expected
         * provenance. Returns the chunk on a full match, nullopt on any
         * mismatch or corruption. */
        LOICOLLECTION_A_NDAPI static std::optional<BytecodeChunk> deserialize(
            const std::string& blob, const Header& expected
        );

    private:
        BytecodeSerializer() = default;

        static constexpr char MAGIC[4] = { 'L', 'C', 'U', 'I' };
        static constexpr uint32_t FORMAT_VERSION = 1;
    };
}
