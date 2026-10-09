// AesEncryptorEdgeTest.cpp - AES 分块/异常输入接口测试
#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "core/AesEncryptor.h"

using namespace backup;

namespace {

std::vector<uint8_t> makePlain(size_t size) {
    std::vector<uint8_t> data(size);
    for (size_t i = 0; i < size; ++i) data[i] = static_cast<uint8_t>((i * 31 + 7) & 0xFF);
    return data;
}

}  // namespace

TEST(AesEncryptorEdge, RoundTripAllBlockBoundaries) {
    AesEncryptor aes("boundary-password");
    const size_t sizes[] = {0, 1, 15, 16, 17, 31, 32, 33, 4096, 1024 * 1024 + 3};
    for (size_t size : sizes) {
        const std::vector<uint8_t> plain = makePlain(size);
        const std::vector<uint8_t> cipher = aes.encrypt(plain.data(), plain.size());
        EXPECT_FALSE(cipher.empty()) << "size=" << size;
        EXPECT_EQ(cipher.size() % 16, size_t(0)) << "size=" << size;
        EXPECT_EQ(cipher.size(), 16 + ((size / 16) + 1) * 16) << "size=" << size;

        bool ok = false;
        const std::vector<uint8_t> out = aes.decrypt(cipher.data(), cipher.size(), &ok);
        EXPECT_TRUE(ok) << "size=" << size;
        EXPECT_EQ(out, plain) << "size=" << size;
    }
}

TEST(AesEncryptorEdge, RandomIvPerEncryption) {
    AesEncryptor aes("iv-password");
    const std::vector<uint8_t> plain = makePlain(64);
    const std::vector<uint8_t> c1 = aes.encrypt(plain.data(), plain.size());
    const std::vector<uint8_t> c2 = aes.encrypt(plain.data(), plain.size());
    ASSERT_EQ(c1.size(), c2.size());
    const std::vector<uint8_t> iv1(c1.begin(), c1.begin() + 16);
    const std::vector<uint8_t> iv2(c2.begin(), c2.begin() + 16);
    EXPECT_NE(iv1, iv2);
}

TEST(AesEncryptorEdge, WrongPasswordDoesNotRecoverPlaintext) {
    AesEncryptor enc("correct-password");
    const std::vector<uint8_t> plain = makePlain(100);
    const std::vector<uint8_t> cipher = enc.encrypt(plain.data(), plain.size());

    AesEncryptor dec("wrong-password");
    bool ok = false;
    const std::vector<uint8_t> out = dec.decrypt(cipher.data(), cipher.size(), &ok);
    EXPECT_TRUE(!ok || out != plain);
}

TEST(AesEncryptorEdge, RejectsMalformedLengths) {
    AesEncryptor aes("length-password");
    bool ok = true;
    EXPECT_TRUE(aes.decrypt(nullptr, 0, &ok).empty());
    EXPECT_FALSE(ok);

    const std::vector<uint8_t> tooShort(15, 0);
    EXPECT_TRUE(aes.decrypt(tooShort.data(), tooShort.size(), &ok).empty());
    EXPECT_FALSE(ok);

    std::vector<uint8_t> notBlockAligned = makePlain(33);  // 16(IV) + 17 → 非块对齐
    EXPECT_TRUE(aes.decrypt(notBlockAligned.data(), notBlockAligned.size(), &ok).empty());
    EXPECT_FALSE(ok);
}

TEST(AesEncryptorEdge, TamperedCiphertextIsDetected) {
    AesEncryptor aes("tamper-password");
    const std::vector<uint8_t> plain = makePlain(256);
    std::vector<uint8_t> cipher = aes.encrypt(plain.data(), plain.size());
    cipher[cipher.size() - 1] ^= 0x5A;  // 破坏最后一个块

    bool ok = false;
    const std::vector<uint8_t> out = aes.decrypt(cipher.data(), cipher.size(), &ok);
    EXPECT_TRUE(!ok || out != plain);
}
