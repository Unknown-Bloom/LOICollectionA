#include <cstring>

#include "LOICollectionA/utils/core/Sha256.h"

namespace LOICollection::utils {
    namespace {
        constexpr uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };

        constexpr uint32_t rotateRight(uint32_t value, uint32_t bits) {
            return (value >> bits) | (value << (32 - bits));
        }
    }

    Sha256::Sha256() : mTotalLength(0), mBuffered(0) {
        this->mState[0] = 0x6a09e667;
        this->mState[1] = 0xbb67ae85;
        this->mState[2] = 0x3c6ef372;
        this->mState[3] = 0xa54ff53a;
        this->mState[4] = 0x510e527f;
        this->mState[5] = 0x9b05688c;
        this->mState[6] = 0x1f83d9ab;
        this->mState[7] = 0x5be0cd19;
    }

    void Sha256::processBlock(const uint8_t* block) {
        uint32_t w[64];
        for (size_t i = 0; i < 16; ++i)
            w[i] = (static_cast<uint32_t>(block[i * 4]) << 24)
                 | (static_cast<uint32_t>(block[i * 4 + 1]) << 16)
                 | (static_cast<uint32_t>(block[i * 4 + 2]) << 8)
                 | static_cast<uint32_t>(block[i * 4 + 3]);

        for (size_t i = 16; i < 64; ++i) {
            uint32_t s0 = rotateRight(w[i - 15], 7) ^ rotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotateRight(w[i - 2], 17) ^ rotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = this->mState[0];
        uint32_t b = this->mState[1];
        uint32_t c = this->mState[2];
        uint32_t d = this->mState[3];
        uint32_t e = this->mState[4];
        uint32_t f = this->mState[5];
        uint32_t g = this->mState[6];
        uint32_t h = this->mState[7];

        for (size_t i = 0; i < 64; ++i) {
            uint32_t s1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t temp1 = h + s1 + ch + K[i] + w[i];
            uint32_t s0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = s0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        this->mState[0] += a;
        this->mState[1] += b;
        this->mState[2] += c;
        this->mState[3] += d;
        this->mState[4] += e;
        this->mState[5] += f;
        this->mState[6] += g;
        this->mState[7] += h;
    }

    void Sha256::update(const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        this->mTotalLength += size;

        while (size > 0) {
            size_t taken = std::min(size, sizeof(this->mBuffer) - this->mBuffered);
            std::memcpy(this->mBuffer + this->mBuffered, bytes, taken);

            this->mBuffered += taken;
            bytes += taken;
            size -= taken;

            if (this->mBuffered == sizeof(this->mBuffer)) {
                this->processBlock(this->mBuffer);
                this->mBuffered = 0;
            }
        }
    }

    void Sha256::update(const std::string& data) {
        this->update(data.data(), data.size());
    }

    std::string Sha256::digest() {
        uint64_t bitLength = this->mTotalLength * 8;

        uint8_t padding = 0x80;
        this->update(&padding, 1);

        uint8_t zero = 0;
        while (this->mBuffered != 56)
            this->update(&zero, 1);

        uint8_t lengthBytes[8];
        for (size_t i = 0; i < 8; ++i)
            lengthBytes[i] = static_cast<uint8_t>(bitLength >> (56 - i * 8));

        this->update(lengthBytes, sizeof(lengthBytes));

        std::string out(32, '\0');
        for (size_t i = 0; i < 8; ++i) {
            out[i * 4] = static_cast<char>(this->mState[i] >> 24);
            out[i * 4 + 1] = static_cast<char>(this->mState[i] >> 16);
            out[i * 4 + 2] = static_cast<char>(this->mState[i] >> 8);
            out[i * 4 + 3] = static_cast<char>(this->mState[i]);
        }

        return out;
    }

    std::string Sha256::compute(const std::string& data) {
        Sha256 hash;
        hash.update(data);
        return hash.digest();
    }

    std::string Sha256::toHex(const std::string& bytes) {
        static const char* digits = "0123456789abcdef";

        std::string out;
        out.reserve(bytes.size() * 2);
        for (unsigned char byte : bytes) {
            out += digits[byte >> 4];
            out += digits[byte & 0x0f];
        }

        return out;
    }
}
