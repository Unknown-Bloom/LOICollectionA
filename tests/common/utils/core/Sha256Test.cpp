#include <gtest/gtest.h>

#include <string>

#include "LOICollectionA/utils/core/Sha256.h"

using LOICollection::utils::Sha256;

namespace sha256_test_support {
    std::string hexOf(const std::string& data) {
        return Sha256::toHex(Sha256::compute(data));
    }

    std::string hexOfChunks(const std::string& data, size_t chunk) {
        Sha256 hash;
        for (size_t i = 0; i < data.size(); i += chunk)
            hash.update(data.substr(i, chunk));

        return Sha256::toHex(hash.digest());
    }

    std::string hexOfBytes(const std::string& data) {
        Sha256 hash;
        for (const char byte : data)
            hash.update(&byte, 1);

        return Sha256::toHex(hash.digest());
    }
}

TEST(Sha256Test, KnownVectors) {
    EXPECT_EQ(sha256_test_support::hexOf(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(sha256_test_support::hexOf("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(
        sha256_test_support::hexOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"
    );
    EXPECT_EQ(
        sha256_test_support::hexOf(
            "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"
        ),
        "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"
    );
}

TEST(Sha256Test, MillionAs) {
    EXPECT_EQ(
        sha256_test_support::hexOf(std::string(1000000, 'a')),
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"
    );
}

TEST(Sha256Test, PaddingBoundariesMatchOneShot) {
    for (size_t length = 0; length <= 130; ++length) {
        const std::string data(length, 'x');
        const std::string expected = sha256_test_support::hexOf(data);

        EXPECT_EQ(sha256_test_support::hexOfChunks(data, 64), expected) << "block-aligned chunks of size " << length;
        EXPECT_EQ(sha256_test_support::hexOfChunks(data, length + 1), expected) << "single chunk of size " << length;
        EXPECT_EQ(sha256_test_support::hexOfBytes(data), expected) << "byte-wise of size " << length;
    }
}

TEST(Sha256Test, SplitPositionDoesNotChangeDigest) {
    const std::string data(200, 'y');
    const std::string expected = sha256_test_support::hexOf(data);

    for (size_t split = 0; split <= data.size(); ++split) {
        Sha256 hash;
        hash.update(data.substr(0, split));
        hash.update(data.substr(split));

        EXPECT_EQ(Sha256::toHex(hash.digest()), expected) << "split at " << split;
    }
}

TEST(Sha256Test, DigestIsThirtyTwoBytes) {
    EXPECT_EQ(Sha256::compute("").size(), 32u);
    EXPECT_EQ(Sha256::compute("abc").size(), 32u);
}

TEST(Sha256Test, RawBufferOverloadMatchesStringOverload) {
    const char raw[] = { 'b', 'i', 'n', 'a', 'r', 'y', '\0', 'p', 'a', 'y' };
    const std::string sized(raw, sizeof(raw));

    Sha256 fromString;
    fromString.update(sized);

    Sha256 fromBuffer;
    fromBuffer.update(raw, sizeof(raw));

    const std::string viaString = Sha256::toHex(fromString.digest());
    const std::string viaBuffer = Sha256::toHex(fromBuffer.digest());

    EXPECT_EQ(viaBuffer, viaString);
    EXPECT_EQ(viaString, sha256_test_support::hexOf(sized));
}

TEST(Sha256Test, DigestIsIdempotentAndDoesNotConsumeState) {
    Sha256 hash;
    hash.update("abc");

    const std::string first = Sha256::toHex(hash.digest());
    const std::string second = Sha256::toHex(hash.digest());

    EXPECT_EQ(first, second);
    EXPECT_EQ(first, sha256_test_support::hexOf("abc"));

    hash.update("def");
    EXPECT_EQ(Sha256::toHex(hash.digest()), sha256_test_support::hexOf("abcdef"));
}

TEST(Sha256Test, EmptyBufferUpdateKeepsState) {
    Sha256 hash;
    hash.update("abc");
    hash.update("", 0);

    EXPECT_EQ(Sha256::toHex(hash.digest()), sha256_test_support::hexOf("abc"));
}

TEST(Sha256Test, ToHexEncodesHighBytes) {
    const char raw[] = {
        0x01, 0x23, 0x45, 0x67,
        static_cast<char>(0x89), static_cast<char>(0xab),
        static_cast<char>(0xcd), static_cast<char>(0xef)
    };

    EXPECT_EQ(Sha256::toHex(""), "");
    EXPECT_EQ(Sha256::toHex(std::string(1, static_cast<char>(0x00))), "00");
    EXPECT_EQ(Sha256::toHex(std::string(1, static_cast<char>(0x80))), "80");
    EXPECT_EQ(Sha256::toHex(std::string(1, static_cast<char>(0xff))), "ff");
    EXPECT_EQ(Sha256::toHex(std::string(raw, sizeof(raw))), "0123456789abcdef");
}
