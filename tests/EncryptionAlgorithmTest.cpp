// EncryptionAlgorithmTest.cpp - 加密算法接口单元测试（AES-256 / 3DES）
// 覆盖 IEncryptionAlgorithm 两个具体实现：工厂创建、块边界往返、随机 IV、
// 错误密码、畸形长度、篡改检测、接口多态。
// 两个算法块大小不同（AES=16，3DES=8），统一通过接口指针验证泛化逻辑。
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/IEncryptionAlgorithm.h"

using namespace backup;

namespace {

std::vector<uint8_t> makePlain(size_t size) {
    std::vector<uint8_t> data(size);
    for (size_t i = 0; i < size; ++i) data[i] = static_cast<uint8_t>((i * 31 + 7) & 0xFF);
    return data;
}

struct AlgoCase {
    std::string name;
    size_t blockSize;
    size_t keySize;
};

const AlgoCase kAlgoCases[] = {
    {"aes256", 16, 32},
    {"3des", 8, 24},
};

}  // namespace

// 工厂：已知算法正确创建且参数正确，未知算法返回 nullptr
TEST(EncryptionAlgorithm, Factory_KnownAlgorithms_AndUnknown) {
    for (const AlgoCase& c : kAlgoCases) {
        auto cipher = createEncryptionAlgorithm(c.name, "some-password");
        ASSERT_NE(cipher, nullptr);
        EXPECT_EQ(cipher->name(), c.name);
        EXPECT_EQ(cipher->blockSize(), c.blockSize);
        EXPECT_EQ(cipher->keySize(), c.keySize);
        EXPECT_TRUE(cipher->valid());
    }
    EXPECT_EQ(createEncryptionAlgorithm("rsa", "p"), nullptr);
    EXPECT_EQ(createEncryptionAlgorithm("", "p"), nullptr);
}

// 块边界往返：两个算法在所有关键长度下加密/解密一致
TEST(EncryptionAlgorithm, RoundTripAllBlockBoundaries) {
    const size_t sizes[] = {0, 1, 7, 8, 15, 16, 17, 31, 32, 33, 4096, 1024 * 1024 + 3};
    for (const AlgoCase& c : kAlgoCases) {
        auto cipher = createEncryptionAlgorithm(c.name, "boundary-password");
        const size_t bs = c.blockSize;
        for (size_t size : sizes) {
            const std::vector<uint8_t> plain = makePlain(size);
            const std::vector<uint8_t> enc = cipher->encrypt(plain.data(), plain.size());
            ASSERT_FALSE(enc.empty()) << c.name << " size=" << size;
            EXPECT_EQ(enc.size() % bs, size_t(0)) << c.name << " size=" << size;
            EXPECT_EQ(enc.size(), bs + ((size / bs) + 1) * bs) << c.name << " size=" << size;

            bool ok = false;
            const std::vector<uint8_t> out = cipher->decrypt(enc.data(), enc.size(), &ok);
            EXPECT_TRUE(ok) << c.name << " size=" << size;
            EXPECT_EQ(out, plain) << c.name << " size=" << size;
        }
    }
}

// 每次加密 IV 随机且不同
TEST(EncryptionAlgorithm, RandomIvPerEncryption) {
    for (const AlgoCase& c : kAlgoCases) {
        auto cipher = createEncryptionAlgorithm(c.name, "iv-password");
        const std::vector<uint8_t> plain = makePlain(64);
        const std::vector<uint8_t> e1 = cipher->encrypt(plain.data(), plain.size());
        const std::vector<uint8_t> e2 = cipher->encrypt(plain.data(), plain.size());
        ASSERT_EQ(e1.size(), e2.size());
        const std::vector<uint8_t> iv1(e1.begin(), e1.begin() + c.blockSize);
        const std::vector<uint8_t> iv2(e2.begin(), e2.begin() + c.blockSize);
        EXPECT_NE(iv1, iv2) << c.name;
    }
}

// 错误密码无法恢复明文
TEST(EncryptionAlgorithm, WrongPasswordDoesNotRecoverPlaintext) {
    for (const AlgoCase& c : kAlgoCases) {
        auto enc = createEncryptionAlgorithm(c.name, "correct-password");
        const std::vector<uint8_t> plain = makePlain(100);
        const std::vector<uint8_t> ciphertext = enc->encrypt(plain.data(), plain.size());

        auto dec = createEncryptionAlgorithm(c.name, "wrong-password");
        bool ok = false;
        const std::vector<uint8_t> out = dec->decrypt(ciphertext.data(), ciphertext.size(), &ok);
        EXPECT_TRUE(!ok || out != plain) << c.name;
    }
}

// 畸形长度被拒绝：空指针、过短、非块对齐
TEST(EncryptionAlgorithm, RejectsMalformedLengths) {
    for (const AlgoCase& c : kAlgoCases) {
        auto cipher = createEncryptionAlgorithm(c.name, "length-password");
        const size_t bs = c.blockSize;
        bool ok = true;
        EXPECT_TRUE(cipher->decrypt(nullptr, 0, &ok).empty());
        EXPECT_FALSE(ok);

        const std::vector<uint8_t> tooShort(bs + 1, 0);  // 短于 IV+1块
        EXPECT_TRUE(cipher->decrypt(tooShort.data(), tooShort.size(), &ok).empty());
        EXPECT_FALSE(ok);

        std::vector<uint8_t> notAligned(bs * 2 + 1, 0);  // 非块对齐
        EXPECT_TRUE(cipher->decrypt(notAligned.data(), notAligned.size(), &ok).empty());
        EXPECT_FALSE(ok);
    }
}

// 篡改最后一块被检测到
TEST(EncryptionAlgorithm, TamperedCiphertextIsDetected) {
    for (const AlgoCase& c : kAlgoCases) {
        auto cipher = createEncryptionAlgorithm(c.name, "tamper-password");
        const std::vector<uint8_t> plain = makePlain(256);
        std::vector<uint8_t> ciphertext = cipher->encrypt(plain.data(), plain.size());
        ciphertext[ciphertext.size() - 1] ^= 0x5A;

        bool ok = false;
        const std::vector<uint8_t> out = cipher->decrypt(ciphertext.data(), ciphertext.size(), &ok);
        EXPECT_TRUE(!ok || out != plain) << c.name;
    }
}

// 接口多态：通过 IEncryptionAlgorithm 指针完成完整往返（策略模式验证）
TEST(EncryptionAlgorithm, PolymorphicRoundTripViaInterface) {
    const std::vector<uint8_t> plain = makePlain(200);
    for (const AlgoCase& c : kAlgoCases) {
        std::unique_ptr<IEncryptionAlgorithm> cipher =
            createEncryptionAlgorithm(c.name, "poly-password");
        const std::vector<uint8_t> ciphertext = cipher->encrypt(plain.data(), plain.size());
        bool ok = false;
        const std::vector<uint8_t> out = cipher->decrypt(ciphertext.data(), ciphertext.size(), &ok);
        EXPECT_TRUE(ok) << c.name;
        EXPECT_EQ(out, plain) << c.name;
    }
}
