#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace LOICollection::utils {
    class Sha256 {
    public:
        Sha256();

        void update(const void* data, size_t size);
        void update(const std::string& data);

        [[nodiscard]] std::string digest() const;

        static std::string compute(const std::string& data);

        static std::string toHex(const std::string& bytes);

    private:
        void processBlock(const uint8_t* block);

        std::string finalize();

        uint32_t mState[8];
        uint64_t mTotalLength;
        uint8_t mBuffer[64];
        size_t mBuffered;
    };
}
