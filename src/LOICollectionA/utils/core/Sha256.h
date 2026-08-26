#pragma once

/* Minimal SHA-256 (FIPS 180-4) used for bytecode cache invalidation (§7.3).
 * Self-contained on purpose: the cache layer must not grow SDK dependencies. */

#include <cstddef>
#include <cstdint>
#include <string>

namespace LOICollection::utils {
    class Sha256 {
    public:
        Sha256();

        void update(const void* data, size_t size);
        void update(const std::string& data);

        /* Finalizes the stream and returns the 32-byte digest. */
        std::string digest();

        /* Convenience: one-shot digest of a byte string. */
        static std::string compute(const std::string& data);

        /* Lowercase hex encoding of a digest (or any byte string). */
        static std::string toHex(const std::string& bytes);

    private:
        void processBlock(const uint8_t* block);

        uint32_t mState[8];
        uint64_t mTotalLength;
        uint8_t mBuffer[64];
        size_t mBuffered;
    };
}
