// BcryptBlockCipher.h - 基于 Windows CNG/BCrypt 的分组密码抽象基类
// 职责：实现 IEncryptionAlgorithm 的通用 CBC 逻辑（打开算法、导入密钥、
//       BCryptEncrypt/Decrypt、随机 IV、流式块处理），子类只提供算法参数。
// 说明：所有对称加解密均调用 bcrypt.dll，不手写任何轮函数/S-box。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>
#include <bcrypt.h>

#include "core/IEncryptionAlgorithm.h"

namespace backup {

class BcryptBlockCipher : public IEncryptionAlgorithm {
public:
    ~BcryptBlockCipher() override;

    // ---- IEncryptionAlgorithm 通用实现 ----
    size_t blockSize() const override { return blockSize_; }
    size_t keySize() const override { return keySize_; }
    bool valid() const override { return hKey_ != nullptr; }

    std::vector<uint8_t> encrypt(const uint8_t* data, size_t len) const override;
    std::vector<uint8_t> decrypt(const uint8_t* data, size_t len,
                                 bool* success = nullptr) const override;
    std::vector<uint8_t> encryptWithIV(const uint8_t* data, size_t len,
                                       const uint8_t* iv) const override;

    void generateRandomIv(uint8_t* iv) const override;
    void encryptCbcBlocks(const uint8_t* in, size_t blocks,
                          uint8_t* out, uint8_t* prev) const override;
    void decryptCbcBlocks(const uint8_t* in, size_t blocks,
                          uint8_t* out, uint8_t* prev) const override;

protected:
    // 密码构造：内部用 SHA-256(password) 派生密钥，再按 keySize 截取。
    BcryptBlockCipher(LPCWSTR algId, size_t blockSize, size_t keySize,
                      const std::string& password);

    // 原始密钥构造（测试用，NIST 向量验证）：不足 keySize 补零，超长截断。
    BcryptBlockCipher(LPCWSTR algId, size_t blockSize, size_t keySize,
                      const uint8_t* rawKey, size_t rawLen);

    size_t blockSize_ = 0;
    size_t keySize_ = 0;
    BCRYPT_ALG_HANDLE hAlg_ = nullptr;
    BCRYPT_KEY_HANDLE hKey_ = nullptr;

private:
    // 打开 BCrypt 算法提供者（CBC 模式）并导入密钥。
    void init(LPCWSTR algId, const uint8_t* key, size_t keyLen);
};

}  // namespace backup
