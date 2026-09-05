// AesEncryptor.h - AES-256-CBC 加密/解密（自实现，零第三方依赖）
// 职责：对文件数据进行 AES-256-CBC 加密与解密，PKCS7 填充。
// 加密文件格式：前 16 字节为随机 IV，后续为加密数据。
// 密钥派生：SHA-256(password) 作为 256 位密钥。
// 对应需求文档 Phase 8（AES 基础加密）。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace backup {

class AesEncryptor {
public:
    // 用密码派生 256 位密钥（SHA-256(password)）。
    explicit AesEncryptor(const std::string& password);

    // 测试用构造：直接使用 32 字节原始密钥（不经过 SHA-256 派生，用于 NIST 标准向量验证）。
    AesEncryptor(const uint8_t* rawKey, size_t keyLen);

    // 加密明文数据，返回 IV(16字节) + 密文(PKCS7填充)。
    // 失败返回空 vector。
    std::vector<uint8_t> encrypt(const uint8_t* plaintext, size_t len) const;

    // 解密数据（输入应为 IV + 密文格式），返回明文。空文件解密返回空 vector。
    // success 不为空时，*success 表示解密是否成功（失败原因：数据损坏/长度不足/填充非法）。
    std::vector<uint8_t> decrypt(const uint8_t* data, size_t len, bool* success = nullptr) const;

    // 测试用：指定 IV 加密（用于 NIST 标准向量验证，生产代码应使用 encrypt 生成随机 IV）。
    std::vector<uint8_t> encryptWithIV(const uint8_t* plaintext, size_t len, const uint8_t iv[16]) const;

    // 便捷重载：加密 string。
    std::vector<uint8_t> encryptString(const std::string& plaintext) const {
        return encrypt(reinterpret_cast<const uint8_t*>(plaintext.data()), plaintext.size());
    }

    // 便捷重载：解密为 string。
    std::string decryptString(const uint8_t* data, size_t len) const {
        std::vector<uint8_t> plain = decrypt(data, len);
        return std::string(reinterpret_cast<const char*>(plain.data()), plain.size());
    }

    // ---- CBC 流式处理（大文件分块加密/解密用）----
    // 生成 16 字节随机 IV。
    static void generateRandomIv(uint8_t iv[16]);
    // 加密若干完整 16 字节块；prev 为 CBC 前一个密文块，调用后更新为最后一个密文块。
    void encryptCbcBlocks(const uint8_t* in, size_t blocks, uint8_t* out, uint8_t prev[16]) const;
    // 解密若干完整 16 字节块；prev 语义同上。
    void decryptCbcBlocks(const uint8_t* in, size_t blocks, uint8_t* out, uint8_t prev[16]) const;

private:
    // AES 块大小（16 字节）
    static constexpr size_t BLOCK_SIZE = 16;
    // AES-256 轮数
    static constexpr int NUM_ROUNDS = 14;
    // AES-256 密钥长度（32 字节）
    static constexpr size_t KEY_SIZE = 32;

    uint8_t key_[KEY_SIZE];          // 256 位密钥
    uint8_t roundKeys_[240];         // 扩展后的轮密钥（15轮 × 16字节）

    // 密钥扩展
    void keyExpansion();

    // 加密单个 16 字节块
    void encryptBlock(const uint8_t in[16], uint8_t out[16]) const;

    // 解密单个 16 字节块
    void decryptBlock(const uint8_t in[16], uint8_t out[16]) const;

    // 生成随机 IV
    static void generateIV(uint8_t iv[16]);
};

}  // namespace backup
